#include "CelerNet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "nimble/nimble_port.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"

#include "../Utils/CelerSettings.h"
#include "CelerLink.h"
#include "NetFrame.h"

// No da malha CelerNet (flood de advertising; ver NetFrame.h e CelerNet.h).
//
// Pecas:
//   task do host NimBLE -> onAdvReport(): testa o magic e enfileira o quadro
//     cru (s_raw). Nada mais roda la.
//   tick() (servico ALWAYS, tasks celerLoop e app): drena s_raw, dedup,
//     presenca, remontagem, agenda repeticoes com jitter e conduz os bursts
//     de TX — tudo sob mutex.
//   meshGapCb(): callback do scanner e dos bursts (ADV_COMPLETE = pacote
//     no ar); os reports seguem para o mesmo onAdvReport().
//
// O burst de TX toma o advertising LEGADO emprestado (so existe 1): para o
// adv do Celer Link/Phone Link, empurra o quadro da malha (ADV_NONCONN,
// 20 ms x 3) e devolve o ar pelo CelerLink::refreshAdvertising(). O
// advRestart do CelerLink ja respeita "adv ativo" (nao atropela o burst);
// as pausas do adv conectavel sao de ~60 ms por pacote, limitadas por
// token bucket (8 pacotes/s).

namespace {

const char* TAG = "celer.net";

// Quadro cru ouvindo no ar (task do host -> tick).
struct RawReport {
    uint8_t len;
    int8_t rssi;
    uint8_t data[netframe::ADV_MAX];
};
constexpr int RAW_DEPTH = 16;

// Quadro esperando o radio: nosso (broadcast/beat) ou repeticao de outro.
struct Pending {
    bool used;
    bool ours;          // conta no txQueued
    uint8_t len;
    uint32_t dueMs;     // jitter da repeticao (nosso = ja valido)
    uint8_t frame[netframe::ADV_MAX];
};
constexpr int PENDING_DEPTH = 16;

struct NodeEntry {
    bool used;
    uint16_t id;
    char name[CelerNet::MAX_NAME + 1];
    int8_t rssi;
    uint8_t hops;
    uint32_t lastMs;
};

// TUDO que e grande vive na PSRAM (fila RX, remontador, fila de TX/relay,
// presenca e dedup): o .bss da malha e ~0 e o boot das placas apertadas
// (watch: o BLE do Phone Link falhava por 881 B com os statics no .bss)
// nem percebe a CelerNet antes dela ser ligada.
struct NetHeap {
    uint8_t head, count;  // ring simples: head + count (tail derivado)
    uint8_t pad[2];
    CelerNet::Msg slots[CelerNet::RX_DEPTH];
    netframe::Reassembler reasm;
    Pending pending[PENDING_DEPTH];
    NodeEntry nodes[CelerNet::NODES_MAX];
    netframe::DedupRing<32> dedup;
};

QueueHandle_t s_raw = nullptr;      // adv reports (host -> tick)
SemaphoreHandle_t s_lock = nullptr; // recursive: tick/poll/start concorrem
NetHeap* s_heap = nullptr;          // PSRAM (ou interna, se falhar)

bool s_active = false;
bool s_relay = true;
uint16_t s_node = 0;               // 2 ultimos bytes da MAC BT
uint16_t s_netId = 0;
uint16_t s_seq = 0;
char s_name[CelerNet::MAX_NAME + 1] = {0};
char s_net[16] = "celer";

uint32_t s_txDropped = 0;
uint32_t s_rxDropped = 0;
uint32_t s_relayed = 0;

// Burst de TX (radio do advertising emprestado).
bool s_advBusy = false;       // adv da malha no ar (ate o ADV_COMPLETE)
volatile bool s_burstDone = false;
bool s_restoreAdv = false;    // paramos o adv do Link: devolver ao final
uint32_t s_nextSlotMs = 0;
// Token bucket: 16 de rajada, 8/s — limita o roubo do advertising conectavel.
uint32_t s_tokens = 16;
uint32_t s_lastRefillMs = 0;
uint32_t s_lastBeatMs = 0;

// Auto-start pelo setting (boot): espera o WiFi/Phone Link acomodarem e
// re-tenta; se falhar por RAM, nao insiste a cada tick.
uint32_t s_lastAutoTryMs = 0;

constexpr uint32_t K_BEAT_MS = 3000;        // presenca a cada 3 s
constexpr uint32_t K_NODE_TTL_MS = 15000;   // no some apos 15 s sem BEAT
constexpr uint32_t K_REASM_TTL_MS = 2000;   // fragmento perdido: slot vence
constexpr uint32_t K_TOKEN_REFILL_PER_S = 8;
constexpr uint32_t K_TOKEN_MAX = 16;
// RAM interna de folga para os restos da malha: so a fila raw (~600 B)
// e state pequeno ficam na interna (RX/remontador/presenca/dedup na PSRAM)
// — o controlador e que respira na interna.
constexpr size_t K_NET_MIN_INTERNAL = 4 * 1024;

SemaphoreHandle_t lock() {
    // init thread-safe (C++11): a 1a chamada cria, as demais devolvem
    static SemaphoreHandle_t s = xSemaphoreCreateRecursiveMutex();
    return s;
}

// ------------------------------------------------------------------ fila TX

bool pendingPush(const uint8_t* frame, uint8_t len, bool ours, uint32_t dueMs) {
    if (s_heap == nullptr) return false;
    for (int i = 0; i < PENDING_DEPTH; i++) {
        if (!s_heap->pending[i].used) {
            Pending* p = &s_heap->pending[i];
            p->used = true;
            p->ours = ours;
            p->len = len;
            p->dueMs = dueMs;
            memcpy(p->frame, frame, len);
            return true;
        }
    }
    return false;
}

// Mensagem -> quadros (DATA unico ou FRAGs) na fila; seq unico por mensagem.
bool enqueueMessage(const uint8_t* data, size_t len, uint8_t ttl, bool ours) {
    if (len == 0 || len > CelerNet::MAX_MSG || ttl == 0) return false;
    const uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint8_t frame[netframe::ADV_MAX];
    netframe::Frame f;
    f.netId = s_netId;
    f.src = s_node;
    f.seq = ++s_seq;
    f.ttl = ttl;
    if (len <= netframe::DATA_MAX) {
        f.type = netframe::TYPE_DATA;
        f.dlen = (uint8_t)len;
        f.data = data;
        size_t n = netframe::encode(f, frame, sizeof(frame));
        if (n == 0 || !pendingPush(frame, (uint8_t)n, ours, now)) return false;
        return true;
    }
    const uint8_t total = (uint8_t)((len + netframe::CHUNK_MAX - 1) / netframe::CHUNK_MAX);
    if (total > netframe::MAX_FRAGS) return false;
    f.type = netframe::TYPE_FRAG;
    f.dlen = netframe::DATA_MAX;  // idx + total + chunk
    uint8_t chunk[netframe::DATA_MAX];
    for (uint8_t i = 0; i < total; i++) {
        size_t off = (size_t)i * netframe::CHUNK_MAX;
        size_t n = len - off;
        if (n > netframe::CHUNK_MAX) n = netframe::CHUNK_MAX;
        chunk[0] = i;
        chunk[1] = total;
        memcpy(chunk + 2, (const uint8_t*)data + off, n);
        f.data = chunk;
        size_t fl = netframe::encode(f, frame, sizeof(frame));
        if (fl == 0 || !pendingPush(frame, (uint8_t)fl, ours, now)) return false;
    }
    return true;
}

// --------------------------------------------------------------------- RX

void rxPush(uint16_t from, const char* fromName, const uint8_t* data, size_t len,
            uint8_t hops, int8_t rssi) {
    if (s_heap == nullptr || len > CelerNet::MAX_MSG) return;
    if (s_heap->count >= CelerNet::RX_DEPTH) {
        // Descarta a MAIS ANTIGA (comando novo vale mais que o velho)
        s_heap->head = (uint8_t)((s_heap->head + 1) % CelerNet::RX_DEPTH);
        s_heap->count--;
        s_rxDropped++;
    }
    CelerNet::Msg* m = &s_heap->slots[(s_heap->head + s_heap->count) % CelerNet::RX_DEPTH];
    memset(m, 0, sizeof(*m));
    m->from = from;
    if (fromName != nullptr && fromName[0] != '\0') {
        snprintf(m->fromName, sizeof(m->fromName), "%s", fromName);
    }
    m->hops = hops;
    m->rssi = rssi;
    m->len = (uint16_t)len;
    memcpy(m->data, data, len);
    s_heap->count++;
}

const char* nodeName(uint16_t id) {
    if (s_heap == nullptr) return "";
    for (int i = 0; i < CelerNet::NODES_MAX; i++) {
        if (s_heap->nodes[i].used && s_heap->nodes[i].id == id) return s_heap->nodes[i].name;
    }
    return "";
}

void nodeTouch(uint16_t id, const char* name, int8_t rssi, uint8_t hops, uint32_t nowMs) {
    if (s_heap == nullptr) return;
    NodeEntry* e = nullptr;
    for (int i = 0; i < CelerNet::NODES_MAX; i++) {
        if (s_heap->nodes[i].used && s_heap->nodes[i].id == id) {
            e = &s_heap->nodes[i];
            break;
        }
    }
    if (e == nullptr && name == nullptr) return;  // sem BEAT: nao inventa no
    if (e == nullptr) {
        for (int i = 0; i < CelerNet::NODES_MAX; i++) {
            if (!s_heap->nodes[i].used) {
                e = &s_heap->nodes[i];
                memset(e, 0, sizeof(*e));
                e->id = id;
                break;
            }
        }
        if (e == nullptr) return;  // tabela cheia: no novo fica de fora
        char idS[8];
        snprintf(idS, sizeof(idS), "%04X", id);
        ESP_LOGI(TAG, "no ouvido: %04X \"%s\" (rssi %d)", id, name != nullptr ? name : "", rssi);
    }
    if (name != nullptr && name[0] != '\0') snprintf(e->name, sizeof(e->name), "%s", name);
    e->used = true;
    e->rssi = rssi;
    e->hops = hops;
    e->lastMs = nowMs;
}

// Quadro decodificado da nossa rede: entrega, presenca e (talvez) repeticao.
void handleFrame(const netframe::Frame& f, int8_t rssi, uint32_t nowMs) {
    if (f.src == s_node) return;  // eco do nosso proprio quadro
    const uint8_t idx = f.type == netframe::TYPE_FRAG && f.dlen >= 2 ? f.data[0] : 0;
    if (s_heap->dedup.seen(f.src, f.seq, idx)) return;

    switch (f.type) {
        case netframe::TYPE_BEAT: {
            char name[CelerNet::MAX_NAME + 1];
            size_t n = f.dlen < CelerNet::MAX_NAME ? f.dlen : CelerNet::MAX_NAME;
            memcpy(name, f.data, n);
            name[n] = '\0';
            nodeTouch(f.src, name, rssi, f.hops, nowMs);
            break;
        }
        case netframe::TYPE_DATA:
            nodeTouch(f.src, nullptr, rssi, f.hops, nowMs);
            rxPush(f.src, nodeName(f.src), f.data, f.dlen, f.hops, rssi);
            break;
        case netframe::TYPE_FRAG: {
            if (f.dlen < 2) return;
            nodeTouch(f.src, nullptr, rssi, f.hops, nowMs);
            uint8_t msg[CelerNet::MAX_MSG];
            size_t len = 0;
            if (s_heap != nullptr &&
                s_heap->reasm.feed(f.src, f.seq, f.data[0], f.data[1], f.data + 2,
                                   (uint8_t)(f.dlen - 2), nowMs, msg, sizeof(msg), &len)) {
                rxPush(f.src, nodeName(f.src), msg, len, f.hops, rssi);
            }
            break;
        }
        default:
            return;
    }

    // Repeticao (flood): ttl-1/hops+1, jitter escalando com o ttl restante.
    if (s_relay && f.ttl > 1) {
        netframe::Frame r = f;
        r.ttl = (uint8_t)(f.ttl - 1);
        r.hops = (uint8_t)(f.hops + 1);
        uint8_t frame[netframe::ADV_MAX];
        size_t n = netframe::encode(r, frame, sizeof(frame));
        if (n > 0) {
            uint32_t jitter = 40 + (uint32_t)f.ttl * 40 + (esp_random() % 200);
            if (pendingPush(frame, (uint8_t)n, false, nowMs + jitter)) s_relayed++;
        }
    }
}

// ------------------------------------------------------------------ radio

int meshGapCb(ble_gap_event* event, void* arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            CelerNet::onAdvReport(event->disc.data, event->disc.length_data, event->disc.rssi);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            s_burstDone = true;
            return 0;
        default:
            return 0;
    }
}

// Scanner continuo (passivo) da malha. O CelerLink::scan do app cancela e
// reassume o radio; o tick religa o nosso quando o app solta.
void scanStart() {
    if (ble_gap_disc_active() || CelerLink::appBusy()) return;
    struct ble_gap_disc_params p;
    memset(&p, 0, sizeof(p));
    p.passive = 1;   // nao pede scan response: quadro cru chega inteiro
    p.itvl = 16;     // 10 ms / 10 ms = janela continua (coex arbitra o WiFi)
    p.window = 16;
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p, meshGapCb, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "scanner: ble_gap_disc rc=%d", rc);
}

// Empurra o proximo quadro devido no advertising (tomado por 60 ms).
void maybeBurst(uint32_t nowMs) {
    if (s_advBusy) {
        if (!s_burstDone) return;
        s_advBusy = false;
        s_burstDone = false;
        s_nextSlotMs = nowMs + 60;  // respiro entre pacotes
    }
    if (nowMs < s_nextSlotMs) return;

    if (s_heap == nullptr) return;
    Pending* best = nullptr;
    for (int i = 0; i < PENDING_DEPTH; i++) {
        if (!s_heap->pending[i].used) continue;
        if ((int32_t)(nowMs - s_heap->pending[i].dueMs) >= 0 &&
            (best == nullptr || (int32_t)(s_heap->pending[i].dueMs - best->dueMs) < 0)) {
            best = &s_heap->pending[i];
        }
    }
    if (best == nullptr) {
        // Fila seca: devolve o advertising ao Celer Link/Phone Link.
        if (s_restoreAdv) {
            s_restoreAdv = false;
            CelerLink::refreshAdvertising();
        }
        return;
    }
    if (s_tokens < 1) return;  // rajada de muitos pacotes: espera refill

    // Scanner e burst NUNCA juntos: scan continuo + reconfiguracao de adv
    // nao-conectavel correm contra o lld_init do controlador (IWDT no
    // btController, coredump do 4848 na bancada 2026-10-06). O tick religa
    // o scanner no ciclo seguinte ao fim da fila.
    if (ble_gap_disc_active()) ble_gap_disc_cancel();
    if (ble_gap_adv_active()) {
        s_restoreAdv = true;
        ble_gap_adv_stop();
    }
    int rc = ble_gap_adv_set_data(best->frame, best->len);
    if (rc == 0) {
        struct ble_gap_adv_params p;
        memset(&p, 0, sizeof(p));
        p.conn_mode = BLE_GAP_CONN_MODE_NON;  // ADV_NONCONN: ninguem conecta
        p.disc_mode = BLE_GAP_DISC_MODE_GEN;
        // 100 ms e o piso da ESPECIFICACAO para ADV_NONCONN (o host aceita
        // 20 ms, o controlador nao: TX silenciosa e, na rajada, IWDT no
        // btController — bancada 2026-10-06 no 4848). Um evento ja cobre os
        // 3 canais de advertising; o ADV_COMPLETE (~150 ms) conduz a fila.
        p.itvl_min = 160;  // 100 ms em unidades de 0,625 ms
        p.itvl_max = 160;
        rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, 250, &p, meshGapCb, nullptr);
    }
    if (rc == 0) {
        best->used = false;
        s_tokens--;
        s_advBusy = true;
        return;
    }
    // EALREADY/EBUSY: radio ocupado agora — o proximo tick tenta de novo
    if (rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "burst: rc=%d", rc);
    if (s_restoreAdv && !ble_gap_adv_active()) {
        s_restoreAdv = false;
        CelerLink::refreshAdvertising();
    }
}

void heapInit() {
    if (s_heap != nullptr) return;
    s_heap = (NetHeap*)heap_caps_malloc(sizeof(NetHeap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_heap == nullptr) {
        s_heap = (NetHeap*)heap_caps_malloc(sizeof(NetHeap), MALLOC_CAP_8BIT);
    }
    if (s_heap != nullptr) memset(s_heap, 0, sizeof(NetHeap));
}

bool tryStart(const char* name, const char* net, bool relay, bool persist) {
    // O pedido (setting) entra ANTES dos gates: com a RAM interna apertada
    // (app aberto numa placa sem BLE no ar) o start() do JS e ACEITO e o
    // tick do servico sobe o no sozinho quando a interna voltar (10 s).
    if (persist) CelerSettings::set("celernet", "1");
    // sem latch: falha de RAM agora nao trava o BLE/Phone Link ate o reboot
    if (!CelerLink::ensureStarted(false)) {  // gate de RAM interna (BLE off)
        s_lastAutoTryMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        return false;
    }
    const size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (freeInt < K_NET_MIN_INTERNAL) {
        static uint8_t s_ramFails = 0;
        if (++s_ramFails >= 6) {  // tick tenta a cada 10 s: loga 1x por minuto
            s_ramFails = 0;
            ESP_LOGW(TAG, "malha aguarda RAM interna (%u < %u)", (unsigned)freeInt,
                     (unsigned)K_NET_MIN_INTERNAL);
        }
        s_lastAutoTryMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        return false;
    }

    // Alocacoes e estado sob mutex: start pode chegar do tick (auto) e do
    // JS ao mesmo tempo — sem isso, duas filas nasceriam e uma vazaria.
    xSemaphoreTakeRecursive(lock(), portMAX_DELAY);
    if (s_raw == nullptr) {
        s_raw = xQueueCreate(RAW_DEPTH, sizeof(RawReport));
    }
    heapInit();
    if (s_raw == nullptr || s_heap == nullptr) {
        xSemaphoreGiveRecursive(lock());
        return false;
    }

    if (name != nullptr && name[0] != '\0') {
        snprintf(s_name, sizeof(s_name), "%.*s", (int)CelerNet::MAX_NAME, name);
        CelerSettings::set("celernet_name", s_name);
    } else {
        std::string cur = CelerSettings::get("celernet_name", "");
        if (!cur.empty()) snprintf(s_name, sizeof(s_name), "%.*s", (int)CelerNet::MAX_NAME, cur.c_str());
    }
    if (net != nullptr && net[0] != '\0') {
        snprintf(s_net, sizeof(s_net), "%.*s", (int)(sizeof(s_net) - 2), net);
        CelerSettings::set("celernet_net", s_net);
    } else {
        std::string cur = CelerSettings::get("celernet_net", "celer");
        snprintf(s_net, sizeof(s_net), "%.*s", (int)(sizeof(s_net) - 2), cur.c_str());
    }
    s_netId = netframe::fnv16(s_net);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    s_node = (uint16_t)((mac[4] << 8) | mac[5]);
    if (s_name[0] == '\0') snprintf(s_name, sizeof(s_name), "Celer-%04X", s_node);
    s_relay = relay;
    s_seq = (uint16_t)(esp_random() & 0xFFFF);

    s_heap->dedup.clear();
    s_heap->head = 0;
    s_heap->count = 0;
    s_heap->reasm.reset();
    for (int i = 0; i < PENDING_DEPTH; i++) s_heap->pending[i].used = false;
    for (int i = 0; i < CelerNet::NODES_MAX; i++) s_heap->nodes[i].used = false;
    xQueueReset(s_raw);
    s_txDropped = s_rxDropped = s_relayed = 0;
    s_tokens = K_TOKEN_MAX;
    s_lastRefillMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    s_lastBeatMs = 0;  // BEAT imediato: presenca aparece logo nos vizinhos
    s_active = true;
    xSemaphoreGiveRecursive(lock());

    scanStart();
    ESP_LOGI(TAG, "malha ligada: \"%s\" (%04X) rede \"%s\" id %04X%s (interna %u)", s_name, s_node,
             s_net, s_netId, s_relay ? ", repetidor" : ", so escuta", (unsigned)freeInt);
    return true;
}

}  // namespace

// ------------------------------------------------------------------- API

void CelerNet::onAdvReport(const uint8_t* data, size_t len, int8_t rssi) {
    if (!s_active || s_raw == nullptr) return;
    if (len < netframe::HDR || len > netframe::ADV_MAX) return;
    if (data[0] != netframe::MAGIC[0] || data[1] != netframe::MAGIC[1]) return;  // quase tudo para aqui
    RawReport r;
    r.len = (uint8_t)len;
    r.rssi = rssi;
    memcpy(r.data, data, len);
    if (xQueueSend(s_raw, &r, 0) != pdTRUE) {
        // cheio: relays vizinhos reenviam, perder um report nao e tragico
    }
}

bool CelerNet::start(const char* name, const char* net, bool relay) {
    if (tryStart(name, net, relay, true)) return true;
    // PEDIDO ACEITO mesmo com o radio adiado: o setting ja vale e o tick
    // sobe o no assim que a RAM interna permitir (status().active confirma)
    return enabledSetting();
}

bool CelerNet::stop() {
    CelerSettings::set("celernet", "0");
    xSemaphoreTakeRecursive(lock(), portMAX_DELAY);
    bool wasActive = s_active;
    s_active = false;
    if (s_advBusy && ble_gap_adv_active()) {
        ble_gap_adv_stop();
        s_advBusy = false;
        s_burstDone = false;
    }
    if (ble_gap_disc_active()) ble_gap_disc_cancel();  // o nosso scanner
    if (s_restoreAdv) {
        s_restoreAdv = false;
        CelerLink::refreshAdvertising();
    }
    if (s_heap != nullptr) {
        for (int i = 0; i < PENDING_DEPTH; i++) s_heap->pending[i].used = false;
    }
    xSemaphoreGiveRecursive(lock());
    if (wasActive) ESP_LOGI(TAG, "malha desligada");
    return true;
}

bool CelerNet::active() {
    return s_active;
}

bool CelerNet::broadcast(const void* data, size_t len, uint8_t ttl) {
    if (!s_active || data == nullptr) return false;
    if (ttl == 0) ttl = TTL_DEFAULT;
    if (ttl > netframe::TTL_MAX) ttl = netframe::TTL_MAX;
    xSemaphoreTakeRecursive(lock(), portMAX_DELAY);
    bool ok = enqueueMessage((const uint8_t*)data, len, ttl, true);
    xSemaphoreGiveRecursive(lock());
    if (!ok) {
        s_txDropped++;
        ESP_LOGW(TAG, "broadcast recusado (%u bytes; fila cheia?)", (unsigned)len);
    }
    return ok;
}

bool CelerNet::poll(Msg* out) {
    if (s_heap == nullptr) return false;
    xSemaphoreTakeRecursive(lock(), portMAX_DELAY);
    bool got = false;
    if (s_heap->count > 0) {
        memcpy(out, &s_heap->slots[s_heap->head], sizeof(*out));
        s_heap->head = (uint8_t)((s_heap->head + 1) % RX_DEPTH);
        s_heap->count--;
        got = true;
    }
    xSemaphoreGiveRecursive(lock());
    return got;
}

int CelerNet::nodes(Node* out, int max) {
    const uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (s_heap == nullptr) return 0;
    xSemaphoreTakeRecursive(lock(), portMAX_DELAY);
    int n = 0;
    for (int i = 0; i < NODES_MAX && n < max; i++) {
        if (!s_heap->nodes[i].used) continue;
        if (nowMs - s_heap->nodes[i].lastMs > K_NODE_TTL_MS) {
            s_heap->nodes[i].used = false;  // expirou (varredura piggyback)
            continue;
        }
        out[n].id = s_heap->nodes[i].id;
        snprintf(out[n].name, sizeof(out[n].name), "%s", s_heap->nodes[i].name);
        out[n].rssi = s_heap->nodes[i].rssi;
        out[n].hops = s_heap->nodes[i].hops;
        out[n].lastSeenMs = nowMs - s_heap->nodes[i].lastMs;
        n++;
    }
    xSemaphoreGiveRecursive(lock());
    for (int i = 1; i < n; i++) {  // mais forte primeiro
        Node t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = t;
    }
    return n;
}

void CelerNet::info(Info* out) {
    memset(out, 0, sizeof(*out));
    xSemaphoreTakeRecursive(lock(), portMAX_DELAY);
    out->active = s_active;
    out->relay = s_relay;
    out->node = s_node;
    out->netId = s_netId;
    snprintf(out->name, sizeof(out->name), "%s", s_name);
    snprintf(out->net, sizeof(out->net), "%s", s_net);
    if (s_heap != nullptr) {
        for (int i = 0; i < PENDING_DEPTH; i++) {
            if (s_heap->pending[i].used && s_heap->pending[i].ours) out->txQueued++;
        }
        for (int i = 0; i < NODES_MAX; i++) {
            if (s_heap->nodes[i].used) out->heard++;
        }
    }
    out->txDropped = s_txDropped;
    out->rxDropped = s_rxDropped;
    out->relayed = s_relayed;
    xSemaphoreGiveRecursive(lock());
}

bool CelerNet::enabledSetting() {
    return CelerSettings::get("celernet", "0") == "1";
}

void CelerNet::tick() {
    if (!s_active) {
        // Protecao de ciclo: boot apos crash/watchdog NAO auto-inicia a
        // malha (o usuario liga pelo app quando quiser). Sem isso um no
        // instavel derruba o device a cada boot+10 s (4848, bancada
        // 2026-10-06: BT controller em IWDT com o churn da malha).
        static bool s_bootChecked = false;
        static bool s_skipAuto = false;
        if (!s_bootChecked) {
            s_bootChecked = true;
            const esp_reset_reason_t r = esp_reset_reason();
            s_skipAuto = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
                         r == ESP_RST_WDT;
            if (s_skipAuto && enabledSetting()) {
                ESP_LOGW(TAG, "malha NAO auto-inicia: boot apos crash (protecao de ciclo)");
            }
        }
        if (s_skipAuto) return;
        // Auto-start pelo setting: so DEPOIS do WiFi/Phone Link acomodarem
        // (o ensureStarted lacha o init em falha — nao pode disparar cedo
        // demais e matar o Phone Link do watch com ele).
        const uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        // Graca no boot (WiFi primeiro): ~60 s no watch (o BLE do Phone
        // Link la depende dela), 10 s nas demais — o 4848 deep-sleepa antes
        // dos 60 s de idle e a malha nunca subiria. Depois que o pedido
        // existiu, o tick tenta a cada 10 s ate a RAM permitir.
#if CONFIG_CELEROS_PHONE_LINK
        constexpr uint32_t kBootGraceMs = 45000;
#else
        constexpr uint32_t kBootGraceMs = 10000;
#endif
        if (enabledSetting() && nowMs > kBootGraceMs &&
            nowMs - s_lastAutoTryMs > (s_lastAutoTryMs == 0 ? 60000 : 10000)) {
            s_lastAutoTryMs = nowMs;
            tryStart(nullptr, nullptr, true, false);  // nao regrava o setting
        }
        return;
    }

    xSemaphoreTakeRecursive(lock(), portMAX_DELAY);
    const uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // O scanner e o bem mais importante da malha: religa sempre (o scan do
    // app ou um reset do host podem ter derrubado).
    scanStart();

    // Fichas do token bucket (limita o roubo do advertising conectavel).
    uint32_t add = (nowMs - s_lastRefillMs) / (1000 / K_TOKEN_REFILL_PER_S);
    if (add > 0) {
        s_lastRefillMs += add * (1000 / K_TOKEN_REFILL_PER_S);
        s_tokens = s_tokens + add > K_TOKEN_MAX ? K_TOKEN_MAX : s_tokens + add;
    }

    // Presenca: BEAT proprio na fila (sequindo as fichas do bucket).
    if (nowMs - s_lastBeatMs >= K_BEAT_MS) {
        s_lastBeatMs = nowMs;
        uint8_t frame[netframe::ADV_MAX];
        netframe::Frame f;
        f.type = netframe::TYPE_BEAT;
        f.netId = s_netId;
        f.src = s_node;
        f.seq = ++s_seq;
        f.dlen = (uint8_t)strlen(s_name);
        f.data = (const uint8_t*)s_name;
        size_t n = netframe::encode(f, frame, sizeof(frame));
        if (n == 0 || !pendingPush(frame, (uint8_t)n, true, nowMs)) {
            // fila cheia: o proximo BEAT (3 s) tenta de novo
        }
    }

    // Drena os reports do ar (task do host so enfileirou).
    RawReport r;
    for (int i = 0; i < 8 && xQueueReceive(s_raw, &r, 0) == pdTRUE; i++) {
        netframe::Frame f;
        if (!netframe::decode(r.data, r.len, &f)) continue;
        if (f.netId != s_netId) continue;  // outra rede por perto
        handleFrame(f, r.rssi, nowMs);
    }

    s_heap->reasm.prune(nowMs, K_REASM_TTL_MS);
    maybeBurst(nowMs);
    xSemaphoreGiveRecursive(lock());
}
