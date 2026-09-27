#include "SerialLink.h"
#include "KryonLink.h"
#include "KryonShell.h"
#include "LogSink.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

const char* TAG = "kryon.dbg";

#if !defined(KRYONOS_VERSION)
#define KRYONOS_VERSION "?"
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

// ------------------------------------------------- supressao de logs (link)

int suppressedVprintf(const char* fmt, va_list args) {
    (void)fmt;
    (void)args;
    return 0;
}

void enterLinkMode() {
    if (s_mode == MODE_LINK) return;
    s_mode = MODE_LINK;
    // logs ESP_LOG* sao desviados para o nada enquanto durar a sessao
    s_defaultVprintf = esp_log_set_vprintf(suppressedVprintf);
    s_defaultVprintfSaved = true;
    ESP_LOGI(TAG, "kryonctl conectado (logs seriais suspensos)");
}

void exitLinkMode() {
    if (s_mode != MODE_LINK) return;
    s_mode = MODE_CONSOLE;
    if (s_defaultVprintfSaved) {
        esp_log_set_vprintf(s_defaultVprintf);
        s_defaultVprintfSaved = false;
    }
    uart_set_baudrate(K_UART, K_BAUD_DEFAULT);
    uartPrintRaw("\r\n[kryonctl desconectado]\r\nkryon> ");
}

// ------------------------------------------------------------ console shell

void feedConsole(uint8_t byte) {
    char line[KryonShell::MAX_LINE];
    if (s_editor->feed(byte, line, sizeof(line))) {
        consolePrint(nullptr, "\r\n");
        if (line[0] != '\0') KryonShell::execute(line, consolePrint, nullptr);
        consolePrint(nullptr, "kryon> ");
    }
}

// ------------------------------------------------------------------- task

void linkTask(void*) {
    char banner[96];
    snprintf(banner, sizeof(banner), "\r\nKryonOS %s console (help | kryonctl via USB)\r\nkryon> ", KRYONOS_VERSION);
    uartPrintRaw(banner);

    uint8_t hold = 0;
    bool holding = false;

    for (;;) {
        if (s_mode == MODE_LINK && (xTaskGetTickCount() - s_lastFrame) > K_LINK_IDLE) {
            exitLinkMode();
        }

        uint8_t buf[64];
        int n = uart_read_bytes(K_UART, buf, sizeof(buf), pdMS_TO_TICKS(250));
        for (int i = 0; i < n; i++) {
            uint8_t b = buf[i];
            if (s_mode == MODE_CONSOLE) {
                if (holding) {
                    holding = false;
                    if (b == KL_HELLO) {
                        // sequencia 'K' + HELLO so vem de ferramenta: entra
                        // no modo link e entrega os dois bytes ao parser
                        enterLinkMode();
                        s_lastFrame = xTaskGetTickCount();
                        KryonLink::feed(hold);
                        KryonLink::feed(b);
                        continue;
                    }
                    // nao era frame: o byte retido vira caractere normal
                    feedConsole(hold);
                }
                if (b == 0x4B) {
                    hold = b;
                    holding = true;
                    continue;
                }
                feedConsole(b);
            } else {
                s_lastFrame = xTaskGetTickCount();
                KryonLink::feed(b);
            }
        }
    }
}

}  // namespace

// ------------------------------------------------------------------- API

bool SerialLink::init() {
    // driver RX na UART do console (TX segue por escrita direta/polling)
    esp_err_t err = uart_driver_install(K_UART, 2048, 0, 0, nullptr, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
        return false;
    }

    static LineEditor editor(consolePrint, nullptr);
    s_editor = &editor;

    KryonLink::setWriter(&SerialLink::writeFrame);
    KryonLink::setBaudHook(&SerialLink::setBaud);

    if (xTaskCreate(linkTask, "dbg_link", 8192, nullptr, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task do console/link");
        return false;
    }
    ESP_LOGI(TAG, "console/shell + kryonctl ativos na UART0");
    return true;
}

bool SerialLink::writeFrame(const uint8_t* data, size_t len) {
    uart_write_bytes(K_UART, (const char*)data, len);
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

bool kryon_log_silent(void) {
    return s_mode == MODE_LINK;
}

void kryon_log_vprintf(const char* fmt, va_list args) {
    if (s_mode == MODE_LINK) return;  // nao intercala na sessao do kryonctl
    vprintf(fmt, args);
}
