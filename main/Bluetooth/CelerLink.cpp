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

namespace {

const char* TAG = "celer.link";

// Motivo de terminate (HCI 0x13 = Remote User Terminated Connection);
// o header do NimBLE nao exporta a constante para o app.
constexpr uint8_t K_CONN_TERM = 0x13;

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
    char name[24];
    int8_t rssi;
};

// Bits do event group de progresso dos procedimentos.
constexpr EventBits_t EV_FAIL = 1 << 1;
constexpr EventBits_t EV_SVC = 1 << 2;
constexpr EventBits_t EV_CHR = 1 << 3;
constexpr EventBits_t EV_SCAN = 1 << 4;

bool s_started = false;
bool s_initFail = false;
SemaphoreHandle_t s_syncSem = nullptr;
EventGroupHandle_t s_evt = nullptr;
QueueHandle_t s_rxQueue = nullptr;

uint16_t s_chrValHandle = 0;  // handle da nossa caracteristica (GATT preenche)

uint16_t s_conn = 0;       // handle da conexao (valido com s_connActive)
bool s_connActive = false;
bool s_isCentral = false;  // nos conectamos no peer (vs peer conectou em nos)
uint16_t s_peerChrVal = 0;
uint16_t s_svcStart = 0, s_svcEnd = 0;
uint8_t s_peerAddr[6] = {0};
bool s_wantAdvertise = false;
char s_advName[20] = {0};

ScanEntry s_scanCache[K_SCAN_CACHE];
int s_scanCount = 0;

// ------------------------------------------------------------------ varios

// ble_addr.val vem little-endian; o id exibido e big-endian (como o
// nRF Connect e o propio formato "AA:BB:..." mostram).
void formatAddr(const uint8_t val[6], char* out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", val[5], val[4], val[3], val[2], val[1], val[0]);
}

// Espera bits do event group alimentando o watchdog: scan/connect bloqueiam
// a task do app (main ou celerapp) por segundos. Devolve os bits que chegaram.
EventBits_t waitBits(EventBits_t bits, uint32_t ms) {
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ms) + 1;
    for (;;) {
        esp_task_wdt_reset();  // task nao inscrita: no-op inofensivo
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) return 0;
        TickType_t slice = deadline - now;
        if (slice > pdMS_TO_TICKS(250)) slice = pdMS_TO_TICKS(250);
        EventBits_t got = xEventGroupWaitBits(s_evt, bits, pdTRUE, pdFALSE, slice);
        if ((got & bits) != 0) return got & bits;
    }
}

void pushRx(const uint8_t* data, size_t len) {
    if (s_rxQueue == nullptr || len == 0) return;
    Msg m;
    m.len = (uint16_t)(len > CelerLink::MAX_MSG ? CelerLink::MAX_MSG : len);
    memcpy(m.data, data, m.len);
    xQueueSend(s_rxQueue, &m, 0);  // fila cheia: descarta (melhor esforco)
}

int onGapEvent(ble_gap_event* event, void* arg);  // usado pelo advRestart

void advRestart() {
    if (!s_wantAdvertise) return;
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof(f));
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &kSvcUuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&f) != 0) return;

    struct ble_hs_adv_fields r;
    memset(&r, 0, sizeof(r));
    r.name = (const uint8_t*)s_advName;
    r.name_len = (uint8_t)strlen(s_advName);
    r.name_is_complete = 1;
    if (ble_gap_adv_rsp_set_fields(&r) != 0) return;

    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof(p));
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &p, onGapEvent, nullptr);
}

// ------------------------------------------------------------- GATT server

// Escrita do peer na nossa caracteristica = mensagem recebida.
int onChrAccess(uint16_t conn, uint16_t attr, ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    uint8_t buf[CelerLink::MAX_MSG];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) == 0) pushRx(buf, len);
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

// --------------------------------------------------- descoberta (central)

int onDiscSvc(uint16_t conn, const struct ble_gatt_error* err, const struct ble_gatt_svc* svc, void* arg) {
    (void)conn;
    (void)arg;
    if (err->status == BLE_HS_EDONE) {
        xEventGroupSetBits(s_evt, s_svcStart ? EV_SVC : EV_FAIL);
    } else if (err->status == 0 && svc != nullptr) {
        s_svcStart = svc->start_handle;
        s_svcEnd = svc->end_handle;
    } else {
        xEventGroupSetBits(s_evt, EV_FAIL);
    }
    return 0;
}

int onDiscChr(uint16_t conn, const struct ble_gatt_error* err, const struct ble_gatt_chr* chr, void* arg) {
    (void)conn;
    (void)arg;
    if (err->status == BLE_HS_EDONE) {
        xEventGroupSetBits(s_evt, s_peerChrVal ? EV_CHR : EV_FAIL);
    } else if (err->status == 0 && chr != nullptr && ble_uuid_cmp(&chr->uuid.u, &kChrUuid.u) == 0) {
        s_peerChrVal = chr->val_handle;
    } else if (err->status != 0) {
        xEventGroupSetBits(s_evt, EV_FAIL);
    }
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
        if (memcmp(s_scanCache[i].addr, d->addr.val, 6) == 0) {
            e = &s_scanCache[i];
            break;
        }
    }
    if (e == nullptr) {
        if (!ours || s_scanCount >= K_SCAN_CACHE) return;
        e = &s_scanCache[s_scanCount++];
        memset(e, 0, sizeof(*e));
        e->addrType = d->addr.type;
        memcpy(e->addr, d->addr.val, 6);
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
                // falha ao conectar (central) ou requisicao descartada
                s_connActive = false;
                s_isCentral = false;
                xEventGroupSetBits(s_evt, EV_FAIL);
                advRestart();
                return 0;
            }
            s_conn = event->connect.conn_handle;
            s_connActive = true;
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(s_conn, &desc) == 0) memcpy(s_peerAddr, desc.peer_ota_addr.val, 6);
            if (s_wantAdvertise) ble_gap_adv_stop();  // 1 conexao por vez
            if (s_isCentral) {
                ble_gattc_exchange_mtu(s_conn, nullptr, nullptr);
                ble_gattc_disc_svc_by_uuid(s_conn, &kSvcUuid.u, onDiscSvc, nullptr);
            }
            return 0;
        }
        case BLE_GAP_EVENT_DISCONNECT: {
            s_conn = 0;
            s_connActive = false;
            s_peerChrVal = 0;
            if (s_isCentral) {
                s_isCentral = false;
                xEventGroupSetBits(s_evt, EV_FAIL);  // caiu no meio do setup
            }
            advRestart();
            return 0;
        }
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
            // Notificacao do peer (somos central): mensagem recebida
            if (event->notify_rx.attr_handle == s_peerChrVal) {
                uint8_t buf[CelerLink::MAX_MSG];
                uint16_t len = 0;
                if (ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), &len) == 0) {
                    pushRx(buf, len);
                }
            }
            return 0;
        default:
            return 0;
    }
}

// ------------------------------------------------------------ init NimBLE

void onSync() {
    xSemaphoreGive(s_syncSem);
}

void onReset(int reason) {
    ESP_LOGW(TAG, "host NimBLE resetou (reason=%d)", reason);
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

bool resolveTarget(const char* idOrName, ble_addr_t* out) {
    if (strlen(idOrName) == 17 && idOrName[2] == ':' && idOrName[5] == ':' && idOrName[8] == ':' &&
        idOrName[11] == ':' && idOrName[14] == ':') {
        uint8_t b[6];
        for (int i = 0; i < 6; i++) {
            char hex[3] = {idOrName[i * 3], idOrName[i * 3 + 1], '\0'};
            char* end = nullptr;
            long v = strtol(hex, &end, 16);
            if (end == hex || *end != '\0') return false;
            b[i] = (uint8_t)v;
        }
        out->type = BLE_ADDR_PUBLIC;  // CelerOS usa endereco publico
        for (int i = 0; i < 6; i++) out->val[i] = b[5 - i];
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

}  // namespace

// ------------------------------------------------------------------- API

bool CelerLink::ensureStarted() {
    if (s_started) return true;
    if (s_initFail) return false;

    s_syncSem = xSemaphoreCreateBinary();
    s_evt = xEventGroupCreate();
    s_rxQueue = xQueueCreate(8, sizeof(Msg));
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

    if (xSemaphoreTake(s_syncSem, pdMS_TO_TICKS(4000)) != pdTRUE) {
        ESP_LOGE(TAG, "sync do host NimBLE nao chegou");
        s_initFail = true;  // sem deinit: nao derrubar o radio no meio
        return false;
    }
    ble_att_set_preferred_mtu(256);
    s_started = true;
    ESP_LOGI(TAG, "Celer Link pronto (NimBLE, \"%s\")", s_advName);
    return true;
}

bool CelerLink::start(const char* name) {
    if (!ensureStarted()) return false;
    if (name != nullptr && name[0] != '\0') {
        strncpy(s_advName, name, sizeof(s_advName) - 1);
        s_advName[sizeof(s_advName) - 1] = '\0';
        ble_svc_gap_device_name_set(s_advName);
    }
    s_wantAdvertise = true;
    if (!s_connActive) advRestart();
    return true;
}

bool CelerLink::stop() {
    s_wantAdvertise = false;
    if (s_started) ble_gap_adv_stop();
    return true;
}

bool CelerLink::listening() {
    return s_wantAdvertise;
}

int CelerLink::scan(uint32_t ms, Peer* out, int max) {
    if (!ensureStarted() || max <= 0) return 0;

    s_scanCount = 0;
    memset(s_scanCache, 0, sizeof(s_scanCache));

    struct ble_gap_disc_params p;
    memset(&p, 0, sizeof(p));
    p.passive = 0;             // scan ativo: pede a resposta com o nome
    p.filter_duplicates = 1;
    xEventGroupClearBits(s_evt, EV_SCAN);
    if (ble_gap_disc(BLE_OWN_ADDR_PUBLIC, (int32_t)ms, &p, onGapEvent, nullptr) != 0) return 0;
    waitBits(EV_SCAN, ms + 1500);

    int n = 0;
    for (int i = 0; i < s_scanCount && n < max; i++) {
        formatAddr(s_scanCache[i].addr, out[n].id);
        strncpy(out[n].name, s_scanCache[i].name, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = '\0';
        out[n].rssi = s_scanCache[i].rssi;
        n++;
    }
    return n;
}

bool CelerLink::connect(const char* idOrName, uint32_t ms) {
    if (!ensureStarted()) return false;
    if (s_connActive) disconnect();

    ble_addr_t addr;
    if (!resolveTarget(idOrName, &addr)) return false;

    s_isCentral = true;
    s_peerChrVal = 0;
    s_svcStart = s_svcEnd = 0;
    xEventGroupClearBits(s_evt, EV_FAIL | EV_SVC | EV_CHR);
    if (ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, (int32_t)ms, nullptr, onGapEvent, nullptr) != 0) {
        s_isCentral = false;
        return false;
    }

    // connect + descoberta do servico disparam no evento CONNECT
    EventBits_t got = waitBits(EV_SVC | EV_FAIL, ms);
    if (!(got & EV_SVC)) goto fail;
    if (ble_gattc_disc_all_chrs(s_conn, s_svcStart, s_svcEnd, onDiscChr, nullptr) != 0) goto fail;
    got = waitBits(EV_CHR | EV_FAIL, ms);
    if (!(got & EV_CHR)) goto fail;

    {
        // Assina notificacoes: CCCD fica em val_handle+1 (layout do nosso
        // GATT, documentado no ble_gatt.h), escreve 0x0001 = notify.
        const uint16_t cccd = (uint16_t)(s_peerChrVal + 1);
        const uint16_t enable = 0x0001;
        if (ble_gattc_write_flat(s_conn, cccd, &enable, sizeof(enable), nullptr, nullptr) != 0) goto fail;
    }
    return true;

fail:
    if (s_connActive) {
        ble_gap_terminate(s_conn, K_CONN_TERM);
        s_connActive = false;
        s_conn = 0;
    }
    s_isCentral = false;
    s_peerChrVal = 0;
    return false;
}

bool CelerLink::disconnect() {
    if (!s_started || !s_connActive) return false;
    uint16_t c = s_conn;
    s_connActive = false;
    s_conn = 0;
    s_isCentral = false;
    return ble_gap_terminate(c, K_CONN_TERM) == 0;
}

bool CelerLink::connected() {
    return s_connActive;
}

bool CelerLink::send(const void* data, size_t len) {
    if (!s_started || !s_connActive || len == 0 || len > MAX_MSG) return false;
    if (s_isCentral) {
        if (s_peerChrVal == 0) return false;
        return ble_gattc_write_flat(s_conn, s_peerChrVal, data, (uint16_t)len, nullptr, nullptr) == 0;
    }
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (om == nullptr) {
        ESP_LOGE(TAG, "send: sem mbuf (%d bytes)", (int)len);
        return false;
    }
    int rc = ble_gatts_notify_custom(s_conn, s_chrValHandle, om);
    if (rc != 0) ESP_LOGE(TAG, "send: notify_custom rc=%d (conn=%u chr=%u)", rc, s_conn, s_chrValHandle);
    return rc == 0;
}

bool CelerLink::poll(void* buf, size_t cap, size_t* len) {
    if (s_rxQueue == nullptr) return false;
    Msg m;
    if (xQueueReceive(s_rxQueue, &m, 0) != pdTRUE) return false;
    size_t n = m.len < cap ? m.len : cap;
    memcpy(buf, m.data, n);
    *len = n;
    return true;
}

void CelerLink::peerId(char* out, size_t cap) {
    if (cap == 0) return;
    out[0] = '\0';
    if (s_connActive) formatAddr(s_peerAddr, out);
}

void CelerLink::appReset() {
    if (!s_started) return;
    s_wantAdvertise = false;
    ble_gap_adv_stop();
    if (s_connActive) {
        ble_gap_terminate(s_conn, K_CONN_TERM);
        s_connActive = false;
        s_conn = 0;
    }
    s_isCentral = false;
    s_peerChrVal = 0;
    if (s_rxQueue != nullptr) xQueueReset(s_rxQueue);
}
