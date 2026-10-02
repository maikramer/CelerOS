#include "SerialLink.h"
#include "HostLink.h"
#include "CelerShell.h"
#include "LogSink.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_CELEROS_LINK_ON_USJ
// Canal do SerialLink sobre a USB-Serial/JTAG nativa do S3 (ex.: dog, cuja
// unica USB e a USJ e a UART0 nao tem conector): MESMA multiplexacao
// console/link da UART, na mesma porta dos logs de console — sem abrir mao
// do console, do OpenOCD e do esptool ROM (o OTG/TinyUSB do
// CELEROS_USB_NATIVE exigiria tirar o console daqui).
#include "driver/usb_serial_jtag.h"
// Anel >= janela x chunk (4 x 8190) + folga: sem controle de fluxo no
// canal, frames a mais que o anel nao segura sao DESCARTADOS pelo driver e
// o push morre em "sem ACK" — inclusive os reenvios, que repetem a rajada
// inteira. O CDC (TinyUSB) escapa disso por NAK de hardware.
constexpr size_t kRxRing = 49152;
bool chanInit() {
    // IDF 6: install() pega a config sem const. TX precisa de ringbuffer
    // proprio (>0 e exigido pelo driver; 0 = "TX buffer is not prepared" e
    // o canal inteiro morre). O anel TX precisa comportar UM FRAME MAXIMO
    // inteiro (8 + 8192): com anel menor, write_bytes de um frame de
    // resposta do READ espera espaco com timeout de 500 ms, devolve escrita
    // CURTA e o host recebe o frame pela metade (timeout no pull/cat).
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 16384,
        .rx_buffer_size = kRxRing,
    };
    return usb_serial_jtag_driver_install(&cfg) == ESP_OK;
}
int chanRead(uint8_t* buf, size_t len, TickType_t ticks) {
    return usb_serial_jtag_read_bytes(buf, len, ticks);
}
void chanWrite(const void* data, size_t len) {
    usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(500));
}
void chanSetBaud(uint32_t) {}  // USB nao tem baud
#else
#include "driver/uart.h"
constexpr uart_port_t K_UART = UART_NUM_0;
// Ring RX: precisa segurar a janela de WRITE inteira (4 x 8190 = ~32KB no
// S3) enquanto o task grava um chunk na flash — ver comentario do USJ acima.
#if CONFIG_IDF_TARGET_ESP32S3
constexpr int kRxRing = 49152;
#else
// ESP32 classico (CYD): heap sem PSRAM nao da folga — janela cai para 1 no
// ctor do HostLink e o anel segura um frame inteiro (4098 bytes).
constexpr int kRxRing = 4096;
#endif
bool chanInit() {
    esp_err_t err = uart_driver_install(K_UART, kRxRing, 0, 0, nullptr, 0);
    return err == ESP_OK || err == ESP_ERR_INVALID_STATE;
}
int chanRead(uint8_t* buf, size_t len, TickType_t ticks) {
    return uart_read_bytes(K_UART, buf, len, ticks);
}
void chanWrite(const void* data, size_t len) {
    uart_write_bytes(K_UART, data, len);
}
void chanSetBaud(uint32_t baud) {
    uart_flush_input(K_UART);  // descarta lixo que chegou no baud antigo
    uart_set_baudrate(K_UART, baud);
}
#endif

constexpr uint32_t K_BAUD_DEFAULT = 115200;
constexpr TickType_t K_LINK_IDLE = pdMS_TO_TICKS(8000);  // sem frames -> sai do modo link

namespace {

const char* TAG = "celer.dbg";

#if !defined(CELEROS_VERSION)
#define CELEROS_VERSION "?"
#endif

enum Mode { MODE_CONSOLE, MODE_LINK };
volatile Mode s_mode = MODE_CONSOLE;
volatile TickType_t s_lastFrame = 0;
bool s_defaultVprintfSaved = false;
vprintf_like_t s_defaultVprintf = nullptr;

LineEditor* s_editor = nullptr;

// instancia do protocolo deste canal (criada no init; o parser e por canal)
HostLink* s_link = nullptr;

// ---- ring de logs + logcat ------------------------------------------------
constexpr size_t K_LOG_RING = 2048;  // era 8K: 6KB fazem falta no heap da CYD (sem PSRAM); logcat segue funcional (janela menor)
char s_logRing[K_LOG_RING];
volatile size_t s_logHead = 0;  // posicao de escrita
volatile size_t s_logTail = 0;  // posicao de leitura
SemaphoreHandle_t s_logMutex = nullptr;
SemaphoreHandle_t s_writeMutex = nullptr;
bool s_logcat = false;

// ------------------------------------------------------------ saida UART

void uartPrintRaw(const char* s) {
    chanWrite(s, strlen(s));
}

void consolePrint(void* ctx, const char* fmt, ...) {
    (void)ctx;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
        chanWrite(buf, (size_t)n);
    }
}

// ------------------------------------------------- hook permanente de logs

// Hook instalado no init: todo ESP_LOG* passa pelo LogSink (ring/logcat e,
// fora de sessoes celerctl, segue para a UART como antes).
int logHookVprintf(const char* fmt, va_list args) {
    celer_log_vprintf(fmt, args);
    return 0;
}

void enterLinkMode() {
    if (s_mode == MODE_LINK) return;
    s_mode = MODE_LINK;
    ESP_LOGI(TAG, "celerctl conectado (logs acumulando no ring; \"celerctl logcat --dump\" absorve)");
}

void exitLinkMode() {
    if (s_mode != MODE_LINK) return;
    s_mode = MODE_CONSOLE;
    if (s_link != nullptr) s_link->endSession();  // libera a sessao para outro canal
    // sessao encerrada sem LOG_OFF (timeout/Crash da tool): para o stream
    if (s_logMutex != nullptr) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    s_logcat = false;
    if (s_logMutex != nullptr) xSemaphoreGive(s_logMutex);
    chanSetBaud(K_BAUD_DEFAULT);  // restaura o console (no-op na USJ)
    uartPrintRaw("\r\n[celerctl desconectado]\r\nceler> ");
}

// ------------------------------------------------------------ console shell

void feedConsole(uint8_t byte) {
    char line[CelerShell::MAX_LINE];
    if (s_editor->feed(byte, line, sizeof(line))) {
        consolePrint(nullptr, "\r\n");
        if (line[0] != '\0') CelerShell::execute(line, consolePrint, nullptr);
        consolePrint(nullptr, "celer> ");
    }
}

// ------------------------------------------------------------------- task

void linkTask(void*) {
    char banner[96];
    snprintf(banner, sizeof(banner), "\r\nCelerOS %s console (help | celerctl via USB)\r\nceler> ", CELEROS_VERSION);
    uartPrintRaw(banner);

    uint8_t hold = 0;
    bool holding = false;

    for (;;) {
        if (s_mode == MODE_LINK && (xTaskGetTickCount() - s_lastFrame) > K_LINK_IDLE) {
            exitLinkMode();
        }

        // Espera 1 byte (bloqueante) e drena o restante sem bloquear: pedir N
        // bytes de uma vez faz o driver esperar o timeout inteiro ate completa-los.
        uint8_t buf[64];
        int n = chanRead(buf, 1, pdMS_TO_TICKS(250));
        if (n > 0) {
            int more = chanRead(buf + 1, sizeof(buf) - 1, 0);
            if (more > 0) n += more;
        }
        for (int i = 0; i < n; i++) {
            uint8_t b = buf[i];
            if (s_mode == MODE_CONSOLE) {
                if (holding) {
                    holding = false;
                    if (b == KL_HELLO) {
                        // sequencia 0x43 + HELLO so vem de ferramenta: entra
                        // no modo link e entrega os dois bytes ao parser
                        enterLinkMode();
                        s_lastFrame = xTaskGetTickCount();
                        s_link->feed(hold);
                        s_link->feed(b);
                        continue;
                    }
                    // nao era frame: o byte retido vira caractere normal
                    feedConsole(hold);
                }
                if (b == 0x43) {
                    hold = b;
                    holding = true;
                    continue;
                }
                feedConsole(b);
            } else {
                s_lastFrame = xTaskGetTickCount();
                s_link->feed(b);
            }
        }
    }
}

}  // namespace

// ------------------------------------------------------------------- API

bool SerialLink::init() {
    // driver RX do canal (TX segue por escrita direta/polling)
    if (!chanInit()) {
        ESP_LOGE(TAG, "falha ao instalar o driver do canal console/link");
        return false;
    }

    static LineEditor editor(consolePrint, nullptr);
    s_editor = &editor;
    s_writeMutex = xSemaphoreCreateMutex();
    s_logMutex = xSemaphoreCreateMutex();
    if (s_writeMutex == nullptr || s_logMutex == nullptr) {
        ESP_LOGE(TAG, "sem memoria para mutexes do console");
        return false;
    }

    // hook permanente: ESP_LOG* passa pelo LogSink (ring + logcat), mantendo
    // a saida normal na UART fora de sessoes celerctl
    s_defaultVprintf = esp_log_set_vprintf(logHookVprintf);
    s_defaultVprintfSaved = true;

#if CONFIG_IDF_TARGET_ESP32S3
    static HostLink link(&SerialLink::writeFrame, &SerialLink::setBaud, 4);
#else
    // ESP32 classico (CYD): ring/heap curtos — stop-and-wait (janela 1);
    // o anel de 4KB nao segura dois chunks em voo
    static HostLink link(&SerialLink::writeFrame, &SerialLink::setBaud, 1);
#endif
    s_link = &link;

    if (xTaskCreate(linkTask, "dbg_link", 8192, nullptr, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task do console/link");
        return false;
    }
#if CONFIG_CELEROS_LINK_ON_USJ
    ESP_LOGI(TAG, "console/shell + celerctl ativos na USB-Serial/JTAG");
#else
    ESP_LOGI(TAG, "console/shell + celerctl ativos na UART0");
#endif
    return true;
}

bool SerialLink::writeFrame(const uint8_t* data, size_t len) {
    // escrita atomica por chamada: frames de resposta (task link) e de
    // logcat (tasks arbitrarias) nunca se intercalam
    if (s_writeMutex != nullptr && xSemaphoreTake(s_writeMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }
    chanWrite(data, len);
    if (s_writeMutex != nullptr) xSemaphoreGive(s_writeMutex);
    return true;
}

void SerialLink::setBaud(uint32_t baud) {
    chanSetBaud(baud);  // no-op na USJ (USB nao tem baud)
}

bool SerialLink::linkActive() {
    return s_mode == MODE_LINK;
}

// ------------------------------------------------------------------ LogSink

bool celer_log_silent(void) {
    return s_mode == MODE_LINK;
}

namespace {

void ringPush(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        s_logRing[s_logHead] = s[i];
        s_logHead = (s_logHead + 1) % K_LOG_RING;
        if (s_logHead == s_logTail) s_logTail = (s_logTail + 1) % K_LOG_RING;  // sobrescreve antigo
    }
}

}  // namespace

void celer_log_vprintf(const char* fmt, va_list args) {
    char line[256];
    int n = vsnprintf(line, sizeof(line), fmt, args);
    if (n <= 0) return;
    if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
    // normaliza fim de linha
    if (n == 0 || line[n - 1] != '\n') {
        if (n < (int)sizeof(line) - 1) line[n++] = '\n';
    }

    if (s_logMutex != nullptr) xSemaphoreTake(s_logMutex, portMAX_DELAY);
    if (s_logcat) {
        // KL_LOG_DATA sai pelo canal ATIVO da sessao (UART0 ou CDC1 nativa)
        HostLink::sendLogFrame(line, (size_t)n);
    } else {
        ringPush(line, (size_t)n);
    }
    if (s_logMutex != nullptr) xSemaphoreGive(s_logMutex);

    if (s_mode != MODE_LINK) printf("%.*s", n, line);
}

void celer_log_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    celer_log_vprintf(fmt, args);
    va_end(args);
}

void celer_log_println(const char* s) { celer_log_printf("%s\n", s); }
void celer_log_print(const char* s) { celer_log_printf("%s", s); }

void celer_logcat_set(bool on) {
    if (s_logMutex == nullptr) return;
    xSemaphoreTake(s_logMutex, portMAX_DELAY);
    if (on) {
        // drena o ring acumulado antes de ligar o stream ao vivo
        while (s_logTail != s_logHead) {
            char chunk[1024];
            size_t n = 0;
            while (s_logTail != s_logHead && n < sizeof(chunk) - 1) {
                chunk[n++] = s_logRing[s_logTail];
                s_logTail = (s_logTail + 1) % K_LOG_RING;
            }
            HostLink::sendLogFrame(chunk, n);
        }
    }
    s_logcat = on;
    xSemaphoreGive(s_logMutex);
}

bool celer_logcat_active(void) {
    return s_logcat;
}
