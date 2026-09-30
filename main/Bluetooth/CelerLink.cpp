#include "CelerLink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Servico "Celer Link v1": um servico primario UUID128 com UMA
// caracteristica de mensagens (write + write-no-rsp + notify). Quem e
// client escreve nela; quem e server notifica. O peer conectado e unico
// (CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1).
//
//   servico: b3a667a2-fdac-5298-8c13-89c980f2d1f1
//   char:    b3a667a2-fdac-5298-8c13-89c980f2d1f2
//
// Concorrencia: os callbacks rodam na task do host NimBLE; a API roda na
// task do app. Os procedimentos longos (connect/scan) sao dirigidos pela
// task do app, passo a passo, sincronizados por bits de um event group —
// o callback so registra o resultado e sinaliza.

namespace {

const char* TAG = "celer.link";

// Motivo de terminate (HCI 0x13 = Remote User Terminated Connection);
// o header do NimBLE nao exporta a constante para o app.
constexpr uint8_t K_CONN_TERM = 0x13;

// MTU ATT default (antes da troca) e o pedido (sdkconfig: 256).
constexpr uint16_t K_MTU_DEFAULT = 23;
constexpr uint16_t K_MTU_WANT = 256;

// Parametros de conexao do central: intervalo 15..30 ms (resposta de
// controle remoto) e supervision timeout de 2 s (queda detectada rapido,
// ainda tolerante ao time-slicing do coexist com o WiFi).
constexpr uint16_t K_ITVL_MIN = 12;       // x 1.25 ms
constexpr uint16_t K_ITVL_MAX = 24;       // x 1.25 ms
constexpr uint16_t K_SUPERVISION = 200;   // x 10 ms

// Prazo para o evento de desconexao chegar depois do terminate.
constexpr uint32_t K_DISC_WAIT_MS = 1500;
// Prazo de reenvio quando o host esta sem buffers (mbuf/ACL).
constexpr uint32_t K_SEND_RETRY_MS = 120;

const ble_uuid128_t kSvcUuid = BLE_UUID128_INIT(0xf1, 0xd1, 0xf2, 0x80, 0xc9, 0x89, 0x13, 0x8c,
                                                0x98, 0x52, 0xac, 0xfd, 0xa2, 0x67, 0xa6, 0xb3);
const ble_uuid128_t kChrUuid = BLE_UUID128_INIT(0xf2, 0xd1, 0xf2, 0x80, 0xc9, 0x89, 0x13, 0x8c,
                                                0x98, 0x52, 0xac, 0xfd, 0xa2, 0x67, 0xa6, 0xb3);

struct Msg {
    uint16_t len;
    uint8_t data[CelerLink::MAX_MSG];
};

constexpr int K_SCAN_CACHE = 16;
struct ScanEntry {
    uint8_t addr[6];  // crua do ble_addr (little-endian)
    uint8_t addrType;
    char name[CelerLink::MAX_NAME + 1];
    int8_t rssi;
};

// Bits do event group de progresso dos procedimentos.
constexpr EventBits_t EV_CONN = 1 << 0;   // conexao estabelecida
constexpr EventBits_t EV_FAIL = 1 << 1;   // procedimento/conexao falhou
constexpr EventBits_t EV_CHR = 1 << 2;    // descoberta da caracteristica terminou
constexpr EventBits_t EV_SCAN = 1 << 3;   // scan terminou
constexpr EventBits_t EV_MTU = 1 << 4;    // troca de MTU terminou (ok ou nao)
constexpr EventBits_t EV_SUB = 1 << 5;    // escrita do CCCD confirmada
constexpr EventBits_t EV_DISC = 1 << 6;   // conexao caiu/encerrada

bool s_started = false;
bool s_initFail = false;
SemaphoreHandle_t s_syncSem = nullptr;
EventGroupHandle_t s_evt = nullptr;
QueueHandle_t s_rxQueue = nullptr;
Msg s_rxScratch;  // so a task do host usa (pushRx)

uint16_t s_chrValHandle = 0;  // handle da nossa caracteristica (GATT preenche)

volatile uint16_t s_conn = 0;         // handle da conexao (valido com s_connActive)
volatile bool s_connActive = false;   // enlace de pe
volatile bool s_ready = false;        // pronto para send()/connected()
volatile bool s_isCentral = false;    // nos conectamos no peer
volatile bool s_connecting = false;   // connect() em andamento (central)
volatile bool s_peerSubscribed = false;  // central inscrito no nosso notify
volatile uint16_t s_mtu = 0;
volatile uint32_t s_rxDropped = 0;
uint16_t s_peerChrVal = 0;
uint8_t s_peerAddr[6] = {0};
volatile bool s_wantAdvertise = false;
char s_advName[CelerLink::MAX_NAME + 1] = {0};

ScanEntry s_scanCache[K_SCAN_CACHE];
volatile int s_scanCount = 0;

// ------------------------------------------------------------------ varios

// ble_addr.val vem little-endian; o id exibido e big-endian (como o
// nRF Connect e o propio formato "AA:BB:..." mostram).
void formatAddr(const uint8_t val[6], char* out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", val[5], val[4], val[3], val[2], val[1], val[0]);
}

// Espera bits do event group alimentando o watchdog: scan/connect bloqueiam
// a task do app (main ou celerapp) por segundos. Devolve os bits que
// chegaram (e os consome); 0 = prazo esgotado.
EventBits_t waitBits(EventBits_t bits, TickType_t deadline) {
    for (;;) {
        esp_task_wdt_reset();  // task nao inscrita: no-op inofensivo
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0) return 0;
        TickType_t slice = deadline - now;
        if (slice > pdMS_TO_TICKS(250)) slice = pdMS_TO_TICKS(250);
        EventBits_t got = xEventGroupWaitBits(s_evt, bits, pdTRUE, pdFALSE, slice);
        if ((got & bits) != 0) return got & bits;
    }
}

TickType_t deadlineIn(uint32_t ms) {
    return xTaskGetTickCount() + pdMS_TO_TICKS(ms) + 1;
}

// Fila cheia descarta a mensagem MAIS ANTIGA: num controle remoto o
// comando mais novo e o que importa. Roda so na task do host.
void pushRx(const struct os_mbuf* om) {
    if (s_rxQueue == nullptr) return;
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len == 0 || len > CelerLink::MAX_MSG) return;
    if (ble_hs_mbuf_to_flat(om, s_rxScratch.data, sizeof(s_rxScratch.data), &len) != 0) return;
    s_rxScratch.len = len;
    if (xQueueSend(s_rxQueue, &s_rxScratch, 0) != pdTRUE) {
        static Msg drop;
        xQueueReceive(s_rxQueue, &drop, 0);
        xQueueSend(s_rxQueue, &s_rxScratch, 0);
        s_rxDropped = s_rxDropped + 1;
    }
}

void clearConnState() {
    s_connActive = false;
    s_ready = false;
    s_conn = 0;
    s_mtu = 0;
    s_peerChrVal = 0;
    s_peerSubscribed = false;
}

int onGapEvent(ble_gap_event* event, void* arg);  // usado pelo advRestart

// (Re)liga o advertising se o app pediu e o radio esta livre. Chamado
// da task do host (eventos) e da do app (start).
void advRestart() {
    if (!s_wantAdvertise || s_connActive || s_connecting) return;
    if (ble_gap_adv_active()) return;

    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof(f));
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &kSvcUuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields r;
    memset(&r, 0, sizeof(r));
    r.name = (const uint8_t*)s_advName;
    r.name_len = (uint8_t)strlen(s_advName);
    r.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&r);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv_rsp_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof(p));
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &p, onGapEvent, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "adv_start rc=%d", rc);
}

// ------------------------------------------------------------- GATT server

// Escrita do peer na nossa caracteristica = mensagem recebida.
int onChrAccess(uint16_t conn, uint16_t attr, ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    if (OS_MBUF_PKTLEN(ctxt->om) > CelerLink::MAX_MSG) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    pushRx(ctxt->om);
    return 0;
}

// Inicializacao posicional (sem designadores): o -Werror de campos faltando
// nao perdoa designadores parciais. Ordem de ble_gatt_chr_def:
// uuid, access_cb, arg, descriptors, flags, min_key_size, val_handle, cpfd.
const struct ble_gatt_chr_def kChrDefs[] = {
    {&kChrUuid.u, onChrAccess, nullptr, nullptr,
     BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
     0, &s_chrValHandle, nullptr},
    {},  // terminador (uuid NULL)
};

const struct ble_gatt_svc_def kSvcDefs[] = {
    {BLE_GATT_SVC_TYPE_PRIMARY, &kSvcUuid.u, nullptr, kChrDefs},
    {},  // terminador
};

// ------------------------------------------------ procedimentos (central)

int onMtu(uint16_t conn, const struct ble_gatt_error* err, uint16_t mtu, void* arg) {
    (void)conn;
    (void)arg;
    if (err->status == 0) s_mtu = mtu;
    xEventGroupSetBits(s_evt, EV_MTU);  // falha nao e fatal: segue com 23
    return 0;
}

int onDiscChr(uint16_t conn, const struct ble_gatt_error* err, const struct ble_gatt_chr* chr, void* arg) {
    (void)conn;
    (void)arg;
    if (err->status == 0 && chr != nullptr) {
        if (s_peerChrVal == 0) s_peerChrVal = chr->val_handle;
    } else if (err->status == BLE_HS_EDONE) {
        xEventGroupSetBits(s_evt, s_peerChrVal ? EV_CHR : EV_FAIL);
    } else {
        xEventGroupSetBits(s_evt, EV_FAIL);
    }
    return 0;
}

int onCccdWrite(uint16_t conn, const struct ble_gatt_error* err, struct ble_gatt_attr* attr, void* arg) {
    (void)conn;
    (void)attr;
    (void)arg;
    xEventGroupSetBits(s_evt, err->status == 0 ? EV_SUB : EV_FAIL);
    return 0;
}

// ------------------------------------------------------------------- scan

void onDiscAdv(const struct ble_gap_disc_desc* d) {
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0) return;

    bool ours = false;
    for (int i = 0; i < f.num_uuids128; i++) {
        if (ble_uuid_cmp(&f.uuids128[i].u, &kSvcUuid.u) == 0) {
            ours = true;
            break;
        }
    }

    // O UUID vem no ADV; o nome vem na resposta de scan (mesmo endereco,
    // evento separado): cria na primeira e faz merge na segunda.
    ScanEntry* e = nullptr;
    for (int i = 0; i < s_scanCount; i++) {
        if (s_scanCache[i].addrType == d->addr.type && memcmp(s_scanCache[i].addr, d->addr.val, 6) == 0) {
            e = &s_scanCache[i];
            break;
        }
    }
    if (e == nullptr) {
        if (!ours || s_scanCount >= K_SCAN_CACHE) return;
        e = &s_scanCache[s_scanCount];
        memset(e, 0, sizeof(*e));
        e->addrType = d->addr.type;
        memcpy(e->addr, d->addr.val, 6);
        s_scanCount = s_scanCount + 1;
    }
    e->rssi = d->rssi;
    if (f.name != nullptr && f.name_len > 0) {
        size_t n = f.name_len < sizeof(e->name) - 1 ? f.name_len : sizeof(e->name) - 1;
        memcpy(e->name, f.name, n);
        e->name[n] = '\0';
    }
}

// -------------------------------------------------------------------- GAP

int onGapEvent(ble_gap_event* event, void* arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status != 0) {
                // falha ao conectar (central) ou conexao nao estabelecida
                if (s_isCentral) xEventGroupSetBits(s_evt, EV_FAIL);
                advRestart();
                return 0;
            }
            s_conn = event->connect.conn_handle;
            s_connActive = true;
            s_mtu = ble_att_mtu(s_conn);
            if (s_mtu == 0) s_mtu = K_MTU_DEFAULT;
            s_peerSubscribed = false;
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(s_conn, &desc) == 0) memcpy(s_peerAddr, desc.peer_ota_addr.val, 6);
            if (ble_gap_adv_active()) ble_gap_adv_stop();  // 1 conexao por vez
            // Sessao nova: nada da conexao anterior vaza para esta.
            if (s_rxQueue != nullptr) xQueueReset(s_rxQueue);
            if (!s_isCentral) {
                s_ready = true;  // peripheral: pronto assim que conectam
                ESP_LOGI(TAG, "peer conectou (conn=%u)", s_conn);
            }
            xEventGroupSetBits(s_evt, EV_CONN);
            return 0;
        }
        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGI(TAG, "desconectado (reason=0x%x)", event->disconnect.reason);
            clearConnState();
            // durante o connect() quem limpa o papel e o proprio connect
            if (!s_connecting) s_isCentral = false;
            xEventGroupSetBits(s_evt, EV_DISC | EV_FAIL);
            advRestart();
            return 0;
        }
        case BLE_GAP_EVENT_MTU:
            s_mtu = event->mtu.value;
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_chrValHandle) {
                s_peerSubscribed = event->subscribe.cur_notify != 0;
            }
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advRestart();
            return 0;
        case BLE_GAP_EVENT_DISC:
            onDiscAdv(&event->disc);
            return 0;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            xEventGroupSetBits(s_evt, EV_SCAN);
            return 0;
        case BLE_GAP_EVENT_NOTIFY_RX:
            // Notificacao do peer (somos central): mensagem recebida. O
            // NimBLE libera o om depois do callback.
            if (event->notify_rx.attr_handle == s_peerChrVal && s_peerChrVal != 0) {
                pushRx(event->notify_rx.om);
            }
            return 0;
        default:
            return 0;
    }
}

// ------------------------------------------------------------ init NimBLE

void onSync() {
    // Tambem chega depois de um reset do host: retoma o advertising.
    if (s_started) advRestart();
    xSemaphoreGive(s_syncSem);
}

void onReset(int reason) {
    ESP_LOGW(TAG, "host NimBLE resetou (reason=%d)", reason);
    // As conexoes morreram com o controlador; nenhum evento DISCONNECT vem.
    clearConnState();
    if (!s_connecting) s_isCentral = false;
    xEventGroupSetBits(s_evt, EV_DISC | EV_FAIL | EV_SCAN);
}

void hostTask(void*) {
    nimble_port_run();      // roda ate deinit
    nimble_port_freertos_deinit();
}

void defaultName() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_advName, sizeof(s_advName), "Celer-%02X%02X", mac[4], mac[5]);
}

bool parseMac(const char* s, uint8_t out[6]) {
    if (strlen(s) != 17) return false;
    for (int i = 0; i < 6; i++) {
        if (i < 5 && s[i * 3 + 2] != ':') return false;
        char hex[3] = {s[i * 3], s[i * 3 + 1], '\0'};
        char* end = nullptr;
        long v = strtol(hex, &end, 16);
        if (end != hex + 2) return false;
        out[5 - i] = (uint8_t)v;  // exibido big-endian, ble_addr little-endian
    }
    return true;
}

// id "AA:BB:..." ou nome do ultimo scan -> endereco. O tipo do endereco
// vem do cache do scan quando o par esta la (random vs public).
bool resolveTarget(const char* idOrName, ble_addr_t* out) {
    uint8_t mac[6];
    if (parseMac(idOrName, mac)) {
        for (int i = 0; i < s_scanCount; i++) {
            if (memcmp(s_scanCache[i].addr, mac, 6) == 0) {
                out->type = s_scanCache[i].addrType;
                memcpy(out->val, mac, 6);
                return true;
            }
        }
        out->type = BLE_ADDR_PUBLIC;  // CelerOS usa endereco publico
        memcpy(out->val, mac, 6);
        return true;
    }
    for (int i = 0; i < s_scanCount; i++) {
        if (strcmp(s_scanCache[i].name, idOrName) == 0) {
            out->type = s_scanCache[i].addrType;
            memcpy(out->val, s_scanCache[i].addr, 6);
            return true;
        }
    }
    return false;
}

// Encerra a conexao atual e ESPERA o evento de desconexao: sem isso o
// proximo connect/advertising bate no limite de 1 conexao.
bool dropConnection(uint32_t waitMs) {
    if (!s_connActive) return true;
    uint16_t c = s_conn;
    xEventGroupClearBits(s_evt, EV_DISC);
    int rc = ble_gap_terminate(c, K_CONN_TERM);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        // ja nao existe (ENOTCONN): so limpa o estado local
        clearConnState();
        return true;
    }
    if (waitBits(EV_DISC, deadlineIn(waitMs)) == 0) {
        ESP_LOGW(TAG, "desconexao sem confirmacao em %u ms", (unsigned)waitMs);
        clearConnState();
        return false;
    }
    return true;
}

// Os procedimentos do host devolvem ENOMEM/EBUSY quando faltam buffers
// ou ha procedimento ATT pendente: tenta de novo por um prazo curto.
bool retryable(int rc) {
    return rc == BLE_HS_ENOMEM || rc == BLE_HS_EBUSY || rc == BLE_HS_EAGAIN;
}

}  // namespace

// ------------------------------------------------------------------- API

bool CelerLink::ensureStarted() {
    if (s_started) return true;
    if (s_initFail) return false;

    s_syncSem = xSemaphoreCreateBinary();
    s_evt = xEventGroupCreate();
    s_rxQueue = xQueueCreate(RX_DEPTH, sizeof(Msg));
    if (s_syncSem == nullptr || s_evt == nullptr || s_rxQueue == nullptr) {
        ESP_LOGE(TAG, "sem memoria para as primitivas do link");
        s_initFail = true;
        return false;
    }

    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init falhou");
        s_initFail = true;
        return false;
    }
    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.reset_cb = onReset;
    int rc = ble_gatts_count_cfg(kSvcDefs);
    if (rc == 0) rc = ble_gatts_add_svcs(kSvcDefs);
    if (rc != 0) {
        ESP_LOGE(TAG, "registro GATT falhou (rc=%d)", rc);
        s_initFail = true;
        return false;
    }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    defaultName();
    ble_svc_gap_device_name_set(s_advName);
    nimble_port_freertos_init(hostTask);

    // Espera o sync em fatias, alimentando o watchdog da task do app.
    bool synced = false;
    for (int i = 0; i < 16 && !synced; i++) {
        esp_task_wdt_reset();
        synced = xSemaphoreTake(s_syncSem, pdMS_TO_TICKS(250)) == pdTRUE;
    }
    if (!synced) {
        ESP_LOGE(TAG, "sync do host NimBLE nao chegou");
        s_initFail = true;  // sem deinit: nao derrubar o radio no meio
        return false;
    }
    ble_att_set_preferred_mtu(K_MTU_WANT);
    s_started = true;
    ESP_LOGI(TAG, "Celer Link pronto (NimBLE, \"%s\")", s_advName);
    return true;
}

bool CelerLink::start(const char* name) {
    if (!ensureStarted()) return false;
    bool rename = false;
    if (name != nullptr && name[0] != '\0' && strncmp(name, s_advName, MAX_NAME) != 0) {
        snprintf(s_advName, sizeof(s_advName), "%s", name);
        ble_svc_gap_device_name_set(s_advName);
        rename = true;
    }
    s_wantAdvertise = true;
    // Nome novo so entra no ar reiniciando o advertising.
    if (rename && ble_gap_adv_active()) ble_gap_adv_stop();
    advRestart();
    return true;
}

bool CelerLink::stop() {
    s_wantAdvertise = false;
    if (s_started && ble_gap_adv_active()) ble_gap_adv_stop();
    return true;
}

bool CelerLink::listening() {
    return s_wantAdvertise;
}

int CelerLink::scan(uint32_t ms, Peer* out, int max) {
    if (!ensureStarted() || max <= 0 || s_connecting) return 0;
    if (ble_gap_disc_active()) ble_gap_disc_cancel();

    s_scanCount = 0;
    memset(s_scanCache, 0, sizeof(s_scanCache));

    struct ble_gap_disc_params p;
    memset(&p, 0, sizeof(p));
    p.passive = 0;             // scan ativo: pede a resposta com o nome
    p.filter_duplicates = 1;
    xEventGroupClearBits(s_evt, EV_SCAN);
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, (int32_t)ms, &p, onGapEvent, nullptr);
    if (rc != 0) {
        ESP_LOGW(TAG, "scan: ble_gap_disc rc=%d", rc);
        return 0;
    }
    if (waitBits(EV_SCAN, deadlineIn(ms + 1000)) == 0) {
        // o controlador nao fechou o scan: cancela antes de ler o cache
        ble_gap_disc_cancel();
    }

    int total = s_scanCount;
    int n = 0;
    for (int i = 0; i < total && n < max; i++) {
        formatAddr(s_scanCache[i].addr, out[n].id);
        snprintf(out[n].name, sizeof(out[n].name), "%s", s_scanCache[i].name);
        out[n].rssi = s_scanCache[i].rssi;
        n++;
    }
    // Mais forte primeiro (insertion sort: no maximo 16 itens).
    for (int i = 1; i < n; i++) {
        Peer t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = t;
    }
    return n;
}

bool CelerLink::connect(const char* idOrName, uint32_t ms) {
    if (!ensureStarted() || s_connecting) return false;

    ble_addr_t addr;
    if (!resolveTarget(idOrName, &addr)) {
        ESP_LOGW(TAG, "connect: alvo desconhecido \"%s\" (scan antes)", idOrName);
        return false;
    }

    const TickType_t deadline = deadlineIn(ms);
    if (s_connActive) dropConnection(K_DISC_WAIT_MS);
    if (ble_gap_disc_active()) ble_gap_disc_cancel();

    s_connecting = true;
    s_isCentral = true;
    s_ready = false;
    s_peerChrVal = 0;
    if (ble_gap_adv_active()) ble_gap_adv_stop();  // volta no fail/disconnect
    xEventGroupClearBits(s_evt, EV_CONN | EV_FAIL | EV_CHR | EV_MTU | EV_SUB | EV_DISC);

    struct ble_gap_conn_params cp;
    memset(&cp, 0, sizeof(cp));
    cp.scan_itvl = 0x0010;
    cp.scan_window = 0x0010;
    cp.itvl_min = K_ITVL_MIN;
    cp.itvl_max = K_ITVL_MAX;
    cp.latency = 0;
    cp.supervision_timeout = K_SUPERVISION;

    const char* step = "gap_connect";
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, (int32_t)ms, &cp, onGapEvent, nullptr);
    if (rc != 0) {
        ESP_LOGW(TAG, "connect: ble_gap_connect rc=%d", rc);
        goto fail;
    }

    // 1) enlace
    step = "conexao";
    if (!(waitBits(EV_CONN | EV_FAIL, deadline) & EV_CONN)) {
        // sem conexao no prazo: cancela o procedimento pendente, senao ela
        // pode surgir depois como "conexao zumbi" sem descoberta
        if (!s_connActive && ble_gap_conn_cancel() == 0) {
            // o cancelamento fecha com um CONNECT de status != 0; se a
            // conexao completou no mesmo instante, o fail a derruba
            waitBits(EV_CONN | EV_FAIL, deadlineIn(300));
        }
        goto fail;
    }

    // 2) MTU antes de qualquer outro pedido ATT (um por vez no bearer)
    step = "mtu";
    if (ble_gattc_exchange_mtu(s_conn, onMtu, nullptr) == 0) {
        EventBits_t got = waitBits(EV_MTU | EV_DISC, deadline);
        if (!(got & EV_MTU)) goto fail;
    }

    // 3) caracteristica de mensagens (busca direta por UUID, sem passar
    // pela descoberta do servico: uma ida e volta a menos)
    step = "descoberta";
    if (ble_gattc_disc_chrs_by_uuid(s_conn, 1, 0xFFFF, &kChrUuid.u, onDiscChr, nullptr) != 0) goto fail;
    if (!(waitBits(EV_CHR | EV_FAIL, deadline) & EV_CHR)) goto fail;

    // 4) assina notificacoes: o CCCD fica em val_handle+1 (layout do GATT
    // do NimBLE para caracteristica com NOTIFY). Escrita COM resposta:
    // so volta pronto depois do peer confirmar.
    step = "inscricao";
    {
        const uint16_t cccd = (uint16_t)(s_peerChrVal + 1);
        const uint16_t enable = 0x0001;
        if (ble_gattc_write_flat(s_conn, cccd, &enable, sizeof(enable), onCccdWrite, nullptr) != 0) goto fail;
        if (!(waitBits(EV_SUB | EV_FAIL, deadline) & EV_SUB)) goto fail;
    }

    s_ready = true;
    s_connecting = false;
    ESP_LOGI(TAG, "conectado em \"%s\" (mtu=%u)", idOrName, s_mtu);
    return true;

fail:
    ESP_LOGW(TAG, "connect \"%s\" falhou na etapa %s", idOrName, step);
    if (s_connActive) dropConnection(K_DISC_WAIT_MS);
    s_connecting = false;
    s_isCentral = false;
    clearConnState();
    advRestart();
    return false;
}

bool CelerLink::disconnect() {
    if (!s_started || !s_connActive) return false;
    bool ok = dropConnection(K_DISC_WAIT_MS);
    s_isCentral = false;
    return ok;
}

bool CelerLink::connected() {
    return s_ready;
}

bool CelerLink::send(const void* data, size_t len) {
    if (!s_started || !s_ready || len == 0 || len > MAX_MSG) return false;
    // O ATT trunca em silencio o que passa do MTU: recusa em vez de
    // entregar JSON cortado.
    uint16_t mtu = s_mtu ? s_mtu : K_MTU_DEFAULT;
    if (len > (size_t)(mtu - 3)) {
        ESP_LOGW(TAG, "send: %u bytes nao cabem no MTU %u", (unsigned)len, mtu);
        return false;
    }
    if (!s_isCentral && !s_peerSubscribed) return false;  // peer nao ouve notify

    const TickType_t deadline = deadlineIn(K_SEND_RETRY_MS);
    for (;;) {
        int rc;
        if (s_isCentral) {
            // write sem resposta: o enlace BLE ja confirma cada pacote e
            // nao ocupa um procedimento GATT por mensagem
            rc = ble_gattc_write_no_rsp_flat(s_conn, s_peerChrVal, data, (uint16_t)len);
        } else {
            struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
            rc = om == nullptr ? BLE_HS_ENOMEM : ble_gatts_notify_custom(s_conn, s_chrValHandle, om);
            // notify_custom consome o om em qualquer resultado
        }
        if (rc == 0) return true;
        if (!retryable(rc) || !s_ready || (int32_t)(deadline - xTaskGetTickCount()) <= 0) {
            ESP_LOGW(TAG, "send: rc=%d (%u bytes)", rc, (unsigned)len);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

bool CelerLink::poll(void* buf, size_t cap, size_t* len) {
    if (s_rxQueue == nullptr) return false;
    static Msg m;  // fora da pilha do app (so a task do app chama poll)
    if (xQueueReceive(s_rxQueue, &m, 0) != pdTRUE) return false;
    size_t n = m.len < cap ? m.len : cap;
    memcpy(buf, m.data, n);
    *len = n;
    return true;
}

void CelerLink::peerId(char* out, size_t cap) {
    if (cap == 0) return;
    out[0] = '\0';
    if (s_connActive && cap >= 18) formatAddr(s_peerAddr, out);
}

void CelerLink::info(Info* out) {
    memset(out, 0, sizeof(*out));
    out->connected = s_ready;
    out->listening = s_wantAdvertise;
    out->central = s_ready && s_isCentral;
    peerId(out->peer, sizeof(out->peer));
    snprintf(out->name, sizeof(out->name), "%s", s_advName);
    if (s_connActive) {
        out->mtu = s_mtu;
        int8_t rssi = 0;
        if (ble_gap_conn_rssi(s_conn, &rssi) == 0) out->rssi = rssi;
    }
    out->pending = s_rxQueue ? (uint16_t)uxQueueMessagesWaiting(s_rxQueue) : 0;
    out->dropped = s_rxDropped;
}

void CelerLink::appReset() {
    if (!s_started) return;
    s_wantAdvertise = false;
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    if (ble_gap_disc_active()) ble_gap_disc_cancel();
    if (s_connActive) dropConnection(K_DISC_WAIT_MS);
    s_isCentral = false;
    clearConnState();
    // O nome dado por um app (start("Celer-Dog")) nao vaza para o proximo.
    defaultName();
    ble_svc_gap_device_name_set(s_advName);
    s_rxDropped = 0;
    if (s_rxQueue != nullptr) xQueueReset(s_rxQueue);
}
