#include "DebugBridge.h"

#if CONFIG_CELEROS_DEBUG_BRIDGE

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"

#include "../Boards/Board.h"
#include "HostLink.h"
#include "NetworkManager.h"

#if !defined(CELEROS_VERSION)
#define CELEROS_VERSION "?"
#endif
#if !defined(CELEROS_API_LEVEL)
#define CELEROS_API_LEVEL 0
#endif

namespace {

const char* TAG = "celer.bridge";

constexpr uint16_t kPort = CONFIG_CELEROS_DEBUG_BRIDGE_PORT;
// Chunks em voo anunciados no HELLO (como a USJ): o TCP ainda tem o controle
// de fluxo do lwip (janela de recepcao) segurando o host enquanto o handler
// grava na flash — nada e descartado por excesso de janela.
constexpr uint8_t kWindow = 4;
constexpr int64_t K_AUTH_TIMEOUT_US = 15000000LL;  // 15 s para o AUTH
constexpr uint8_t K_AUTH_MAX_FAILS = 3;

// Handler do evento de rede (task sys_evt) e o service tick so setam flags:
// TODOS os sockets pertencem a task dbg_bridge — nenhum fd e fechado sob o
// select de outra task.
volatile bool s_netUp = false;
volatile bool s_clientOn = false;
TaskHandle_t s_task = nullptr;
HostLink* s_link = nullptr;  // instancia do canal (parser proprio, como UART/CDC)

int s_listenFd = -1;
int s_udpFd = -1;
int s_clientFd = -1;
bool s_authOk = false;
int64_t s_authDeadline = 0;
uint8_t s_authFails = 0;
char s_authLine[96];
size_t s_authLen = 0;

SemaphoreHandle_t s_writeMutex = nullptr;

// ------------------------------------------------ token (NVS celer/*)
// Mesmo formato do WebAuth: 8 chars do alfabeto sem ambiguos (0/O, 1/l/I).
// Carregado no begin() (boot, single-threaded): shell/info/auth so leem.
char s_token[16] = "";
constexpr const char* kAlphabet = "abcdefghjkmnpqrstuvwxyz23456789";
constexpr size_t kTokenLen = 8;

void tokenGenerate() {
    for (size_t i = 0; i < kTokenLen; i++) {
        s_token[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
    }
    s_token[kTokenLen] = '\0';
}

bool tokenPersist() {
    nvs_handle_t h;
    if (nvs_open("celer", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, "bridge_token", s_token) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

void tokenLoad() {
    nvs_handle_t h;
    if (nvs_open("celer", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_token);
        bool ok = nvs_get_str(h, "bridge_token", s_token, &len) == ESP_OK && len > 1;
        nvs_close(h);
        if (ok) return;
    }
    tokenGenerate();  // 1a leitura: gera e persiste (NVS fora = vale ate reboot)
    if (!tokenPersist()) ESP_LOGW(TAG, "NVS rejeitou o token (vale ate reiniciar)");
}

// Comparacao em bloco fixo (mesma ideia do MD5 do WebAuth): nao entrega
// timing nem comprimento do segredo.
bool tokenEq(const char* attempt, size_t alen) {
    uint8_t d = 0;
    for (size_t i = 0; i < 32; i++) {
        uint8_t a = i < alen ? (uint8_t)attempt[i] : 0;
        uint8_t b = i < sizeof(s_token) ? (uint8_t)s_token[i] : 0;
        d |= (uint8_t)(a ^ b);
    }
    return d == 0;
}

// --------------------------------------------- escrita TCP (WriteFn)
// Frames de resposta (dispatch na task do bridge) e de logcat/debugger
// (tasks arbitarias via HostLink::sendLogFrame/sendDebugFrame) nunca se
// intercalam: 1 mutex + send() continuo por frame. O SO_SNDTIMEO do socket
// garante que um host parado nao segura a task (nem o mutex) para sempre.
bool tcpWrite(const uint8_t* data, size_t len) {
    int fd = s_clientFd;
    if (fd < 0 || !s_authOk) return false;
    if (s_writeMutex != nullptr && xSemaphoreTake(s_writeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    size_t off = 0;
    bool ok = true;
    while (off < len) {
        int n = send(fd, data + off, len - off, 0);
        if (n <= 0) {
            ok = false;
            break;
        }
        off += (size_t)n;
    }
    if (s_writeMutex != nullptr) xSemaphoreGive(s_writeMutex);
    return ok;
}

// ------------------------------------------------------- ciclo de vida

void closeClient() {
    if (s_clientFd >= 0) {
        close(s_clientFd);
        s_clientFd = -1;
    }
    if (s_link != nullptr) s_link->endSession();  // devolve a sessao p/ UART/CDC
    s_authOk = false;
    s_authFails = 0;
    s_authLen = 0;
    s_authLine[0] = '\0';
    s_clientOn = false;
}

void closeListener() {
    closeClient();
    if (s_listenFd >= 0) {
        close(s_listenFd);
        s_listenFd = -1;
    }
    if (s_udpFd >= 0) {
        close(s_udpFd);
        s_udpFd = -1;
    }
}

bool ipNow(char* out, size_t cap) {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) return false;
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) return false;
    snprintf(out, cap, IPSTR, IP2STR(&info.ip));
    return true;
}

bool startListener() {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kPort);

    s_udpFd = socket(AF_INET, SOCK_DGRAM, 0);
    bool ok = s_udpFd >= 0 && bind(s_udpFd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    if (ok) {
        s_listenFd = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(s_listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        ok = s_listenFd >= 0 && bind(s_listenFd, (struct sockaddr*)&addr, sizeof(addr)) == 0 &&
             listen(s_listenFd, 1) == 0;
    }
    if (!ok) {
        ESP_LOGE(TAG, "falha ao abrir o listener na porta %u", (unsigned)kPort);
        closeListener();
        return false;
    }
    char ip[16] = "";
    ipNow(ip, sizeof(ip));
    ESP_LOGI(TAG, "bridge no ar em %s:%u (TCP celerctl + UDP sonda)", ip[0] ? ip : "?",
             (unsigned)kPort);
    return true;
}

// Sonda "CELERPROBE1" do celerctl devices: resposta unicast no mesmo formato
// do HELLO — o IP do device e o endereco de origem do proprio datagrama.
void handleProbe() {
    char buf[32];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    int n = recvfrom(s_udpFd, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &flen);
    if (n < (int)11 || memcmp(buf, "CELERPROBE1", 11) != 0) return;
    char out[96];
    int m = snprintf(out, sizeof(out), "CELEROS %s|%s|api %d|proto 2|tcp %u", CELEROS_VERSION,
                     Board::profile().id, CELEROS_API_LEVEL, (unsigned)kPort);
    sendto(s_udpFd, out, (size_t)m, 0, (struct sockaddr*)&from, flen);
}

void handleAccept() {
    struct sockaddr_in peer;
    socklen_t alen = sizeof(peer);
    int fd = accept(s_listenFd, (struct sockaddr*)&peer, &alen);
    if (fd < 0) return;
    if (s_clientFd >= 0) {  // sessao unica: o 2o cliente sabe na hora
        send(fd, "busy\n", 5, 0);
        close(fd);
        return;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));  // shell/debug sem Nagle
    int tv = 2000;  // ms: TX preso nao segura a task (nem o mutex de escrita)
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char ip[16] = "";
    ipNow(ip, sizeof(ip));
    char banner[112];
    int m = snprintf(banner, sizeof(banner), "CELERBRIDGE 1 %s %s %u\n", Board::profile().id,
                     ip[0] ? ip : "?", (unsigned)kPort);
    if (send(fd, banner, (size_t)m, 0) != m) {
        close(fd);
        return;
    }

    s_clientFd = fd;
    s_authOk = false;
    s_authFails = 0;
    s_authLen = 0;
    s_authLine[0] = '\0';
    s_authDeadline = esp_timer_get_time() + K_AUTH_TIMEOUT_US;
}

// Linha acumulada ate o \n: "AUTH <token>". NO = tenta de novo (ate 3),
// ERR = 3 erros seguidos, close.
void handleAuthLine() {
    while (s_authLen > 0 && (s_authLine[s_authLen - 1] == '\r')) {
        s_authLine[--s_authLen] = '\0';
    }
    if (s_authLen > 5 && strncmp(s_authLine, "AUTH ", 5) == 0 &&
        tokenEq(s_authLine + 5, s_authLen - 5)) {
        s_authOk = true;
        s_clientOn = true;
        send(s_clientFd, "OK\n", 3, 0);
        ESP_LOGI(TAG, "cliente bridge autenticado");
        return;
    }
    if (++s_authFails >= K_AUTH_MAX_FAILS) {
        send(s_clientFd, "ERR\n", 4, 0);
        closeClient();
        return;
    }
    send(s_clientFd, "NO\n", 3, 0);
}

void handleClientRx() {
    if (!s_authOk) {
        char buf[64];
        int n = recv(s_clientFd, buf, sizeof(buf), 0);
        if (n <= 0) {
            closeClient();
            return;
        }
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                handleAuthLine();
                if (s_clientFd < 0) return;
                s_authLen = 0;
                s_authLine[0] = '\0';
            } else if (s_authLen + 1 < sizeof(s_authLine)) {
                s_authLine[s_authLen++] = c;
                s_authLine[s_authLen] = '\0';
            }
        }
        return;
    }
    uint8_t buf[1024];
    int n = recv(s_clientFd, buf, sizeof(buf), 0);
    if (n <= 0) {
        closeClient();
        return;
    }
    // Maquina byte a byte (mesma alimentacao da UART): o dispatch roda aqui,
    // serializado pelo mutex global do HostLink com os canais UART/CDC.
    for (int i = 0; i < n; i++) s_link->feed(buf[i]);
}

void bridgeTask(void*) {
    EXT_RAM_BSS_ATTR static HostLink link(&tcpWrite, nullptr, kWindow);
    s_link = &link;
    TickType_t lastTry = 0;

    for (;;) {
        if (s_netUp && s_listenFd < 0) {
            TickType_t now = xTaskGetTickCount();
            if (lastTry == 0 || now - lastTry >= pdMS_TO_TICKS(5000)) {
                lastTry = now;
                startListener();  // falha (porta ocupada?) = retry com folga
            }
        } else if (!s_netUp && s_listenFd >= 0) {
            closeListener();  // caiu o WiFi: mata sessao e sockets; renasce so
        }
        if (s_listenFd < 0) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s_listenFd, &rfds);
        FD_SET(s_udpFd, &rfds);
        int maxFd = s_udpFd > s_listenFd ? s_udpFd : s_listenFd;
        if (s_clientFd >= 0) {
            FD_SET(s_clientFd, &rfds);
            if (s_clientFd > maxFd) maxFd = s_clientFd;
        }
        struct timeval tv = {0, 200000};  // 200 ms
        if (select(maxFd + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            if (FD_ISSET(s_udpFd, &rfds)) handleProbe();
            if (s_listenFd >= 0 && FD_ISSET(s_listenFd, &rfds)) handleAccept();
            if (s_clientFd >= 0 && FD_ISSET(s_clientFd, &rfds)) handleClientRx();
        }
        // AUTH que nunca chega = cliente morto segurando o slot unico
        if (s_clientFd >= 0 && !s_authOk && esp_timer_get_time() > s_authDeadline) {
            closeClient();
        }
    }
}

}  // namespace

// ------------------------------------------------------------------- API

void DebugBridge::begin() {
    tokenLoad();  // boot, single-threaded: sem corrida na geracao
    s_writeMutex = xSemaphoreCreateMutex();
    static bool bound = false;
    if (!bound) {
        bound = true;
        NetworkManager::instance().onStateChanged.addHandler(
            [](NetworkState, NetworkState n) { s_netUp = (n == NetworkState::Connected); });
    }
    s_netUp = NetworkManager::instance().isConnected();
}

void DebugBridge::tick(bool) {
    if (!s_netUp || s_task != nullptr) return;
    // Nasce na 1a rede de verdade: placa sem WiFi nunca paga a stack da task.
    // (Com CELEROS_APP_TASK os dois pumps podem chamar tick juntos — a flag
    // estreita a janela e o create duplicado e inofensivo: so uma vence.)
    static volatile bool creating = false;
    static TickType_t failAt = 0;
    if (creating) return;
    TickType_t now = xTaskGetTickCount();
    if (failAt != 0 && now - failAt < pdMS_TO_TICKS(5000)) return;
    creating = true;
    if (xTaskCreate(bridgeTask, "dbg_bridge", 8192, nullptr, 4, &s_task) != pdPASS) {
        s_task = nullptr;
        failAt = now;
        ESP_LOGE(TAG, "sem memoria para a task do bridge (tento de novo em 5s)");
    }
    creating = false;
}

bool DebugBridge::sessionActive() { return s_clientOn; }

const char* DebugBridge::token() { return s_token; }

void DebugBridge::tokenReset() {
    // Shell chama (main/app task) fora do caminho do auth: a janela de troca
    // e uma comparacao NO no pior caso — cliente reautentica e passa.
    tokenGenerate();
    if (!tokenPersist()) ESP_LOGW(TAG, "NVS rejeitou o token novo (vale ate reiniciar)");
}

uint16_t DebugBridge::port() { return kPort; }

bool DebugBridge::listening() { return s_listenFd >= 0; }

#else  // CONFIG_CELEROS_DEBUG_BRIDGE

void DebugBridge::begin() {}
void DebugBridge::tick(bool) {}
bool DebugBridge::sessionActive() { return false; }
const char* DebugBridge::token() { return ""; }
void DebugBridge::tokenReset() {}
uint16_t DebugBridge::port() { return 0; }
bool DebugBridge::listening() { return false; }

#endif  // CONFIG_CELEROS_DEBUG_BRIDGE
