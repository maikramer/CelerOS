#include "SerialLink.h"
#include "HostLink.h"
#include "CelerShell.h"
#include "LogSink.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

const char* TAG = "celer.dbg";

#if !defined(CELEROS_VERSION)
#define CELEROS_VERSION "?"
#endif

constexpr uart_port_t K_UART = UART_NUM_0;
constexpr uint32_t K_BAUD_DEFAULT = 115200;
constexpr TickType_t K_LINK_IDLE = pdMS_TO_TICKS(8000);  // sem frames -> sai do modo link

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
    uart_write_bytes(K_UART, s, strlen(s));
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
        uart_write_bytes(K_UART, buf, (size_t)n);
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
    uart_set_baudrate(K_UART, K_BAUD_DEFAULT);
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
        int n = uart_read_bytes(K_UART, buf, 1, pdMS_TO_TICKS(250));
        if (n > 0) {
            int more = uart_read_bytes(K_UART, buf + 1, sizeof(buf) - 1, 0);
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
    // driver RX na UART do console (TX segue por escrita direta/polling).
    // O ring segura os chunks em voo da janela do proto 2 enquanto a task
    // escreve no flash: S3 tem DRAM de sobra; CYD fica com 4KB (heap).
#if CONFIG_IDF_TARGET_ESP32S3
    constexpr int kRxRing = 16384;
#else
    constexpr int kRxRing = 4096;
#endif
    esp_err_t err = uart_driver_install(K_UART, kRxRing, 0, 0, nullptr, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
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
    // ESP32 classico (CYD): ring/heap curtos — janela menor de chunks em voo
    static HostLink link(&SerialLink::writeFrame, &SerialLink::setBaud, 2);
#endif
    s_link = &link;

    if (xTaskCreate(linkTask, "dbg_link", 8192, nullptr, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task do console/link");
        return false;
    }
    ESP_LOGI(TAG, "console/shell + celerctl ativos na UART0");
    return true;
}

bool SerialLink::writeFrame(const uint8_t* data, size_t len) {
    // escrita atomica por chamada: frames de resposta (task link) e de
    // logcat (tasks arbitrarias) nunca se intercalam
    if (s_writeMutex != nullptr && xSemaphoreTake(s_writeMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }
    uart_write_bytes(K_UART, (const char*)data, len);
    if (s_writeMutex != nullptr) xSemaphoreGive(s_writeMutex);
    return true;
}

void SerialLink::setBaud(uint32_t baud) {
    // descarta lixo que chegou no baud antigo
    uart_flush_input(K_UART);
    uart_set_baudrate(K_UART, baud);
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
