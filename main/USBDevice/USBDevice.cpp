#include "USBDevice.h"

#if CONFIG_CELEROS_USB_NATIVE

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tusb.h"

#include "CelerShell.h"
#include "HostLink.h"
#include "esp_attr.h"

// Descritores de string do esp_tinyusb (usb_descriptors.c). Trocamos a string
// de serial (indice 3) pela MAC do chip antes de instalar o driver, para o
// celerctl diferenciar varios dispositivos na mesma maquina.
extern const char* descriptor_str_default[];

namespace {

const char* TAG = "celer.usb";

constexpr tinyusb_cdcacm_itf_t CDC_SHELL = TINYUSB_CDC_ACM_0;  // terminal humano
constexpr tinyusb_cdcacm_itf_t CDC_LINK = TINYUSB_CDC_ACM_1;   // protocolo celerctl

StreamBufferHandle_t s_shellRx = nullptr;
StreamBufferHandle_t s_linkRx = nullptr;
SemaphoreHandle_t s_writeMutex = nullptr;

// instancia do protocolo deste canal (criada no init; o parser e por canal)
HostLink* s_link = nullptr;

// ------------------------------------------------------------- escrita CDC

void cdcWrite(tinyusb_cdcacm_itf_t itf, const uint8_t* data, size_t len) {
    if (!tud_ready()) return;
    if (xSemaphoreTake(s_writeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    size_t sent = 0;
    while (sent < len) {
        size_t n = tinyusb_cdcacm_write_queue(itf, data + sent, len - sent);
        esp_err_t err = tinyusb_cdcacm_write_flush(itf, pdMS_TO_TICKS(500));
        sent += n;
        if (err != ESP_OK && n == 0) break;  // host sumiu ou FIFO travou
    }
    xSemaphoreGive(s_writeMutex);
}

// --------------------------------------------------------- callbacks de RX

void cdcRxCallback(int itf, cdcacm_event_t* event) {
    uint8_t buf[128];
    size_t rx = 0;
    tinyusb_cdcacm_itf_t port = (tinyusb_cdcacm_itf_t)itf;
    while (tinyusb_cdcacm_read(port, buf, sizeof(buf), &rx) == ESP_OK && rx > 0) {
        StreamBufferHandle_t target = (itf == (int)CDC_SHELL) ? s_shellRx : s_linkRx;
        if (target != nullptr) xStreamBufferSend(target, buf, rx, 0);
    }
}

// ------------------------------------------------------------------- tasks

void shellPrint(void* ctx, const char* fmt, ...) {
    (void)ctx;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
        cdcWrite(CDC_SHELL, (const uint8_t*)buf, (size_t)n);
    }
}

// Shell interativo na CDC0 (eco/backspace via LineEditor compartilhado).
void usbShellTask(void*) {
    static LineEditor editor(shellPrint, nullptr);
    char line[CelerShell::MAX_LINE];

    vTaskDelay(pdMS_TO_TICKS(100));  // deixa o terminal do host subir
    shellPrint(nullptr, "\r\nCelerOS shell (digite help)\r\nceler> ");

    for (;;) {
        uint8_t byte;
        if (xStreamBufferReceive(s_shellRx, &byte, 1, portMAX_DELAY) == 0) continue;
        if (editor.feed(byte, line, sizeof(line))) {
            shellPrint(nullptr, "\r\n");
            if (line[0] != '\0') CelerShell::execute(line, shellPrint, nullptr);
            shellPrint(nullptr, "celer> ");
        }
    }
}

void usbLinkTask(void*) {
    if (s_link != nullptr) s_link->run(s_linkRx);
    vTaskDelete(nullptr);  // inalcancavel: run() nunca retorna
}

}  // namespace

// ------------------------------------------------------------------- API

bool USBDevice::init() {
    // serial number derivado da MAC (descriptor_str_default[3] e o serial)
    static char serial[16];
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(serial, sizeof(serial), "K%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    descriptor_str_default[3] = serial;

    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(err));
        return false;
    }

    const tinyusb_config_cdcacm_t shellCfg = {
        .cdc_port = CDC_SHELL,
        .callback_rx = &cdcRxCallback,
        .callback_rx_wanted_char = nullptr,
        .callback_line_state_changed = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    const tinyusb_config_cdcacm_t linkCfg = {
        .cdc_port = CDC_LINK,
        .callback_rx = &cdcRxCallback,
        .callback_rx_wanted_char = nullptr,
        .callback_line_state_changed = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    if (tinyusb_cdcacm_init(&shellCfg) != ESP_OK || tinyusb_cdcacm_init(&linkCfg) != ESP_OK) {
        ESP_LOGE(TAG, "falha ao iniciar CDCs");
        return false;
    }

    s_shellRx = xStreamBufferCreate(2048, 1);
    s_linkRx = xStreamBufferCreate(HostLink::MAX_PAYLOAD * 2, 1);
    s_writeMutex = xSemaphoreCreateMutex();
    if (s_shellRx == nullptr || s_linkRx == nullptr || s_writeMutex == nullptr) {
        ESP_LOGE(TAG, "sem memoria para buffers USB");
        return false;
    }

    // CDC: sem hook de baud (USB nao tem baud). Janela 2: o NAK do USB
    // cobre o FIFO do periferico, mas o STREAM BUFFER acima (2x MAX_PAYLOAD)
    // NAO — o driver CDC descarta pacote quando ele enche, e com janela 8
    // (8x8190 em voo) a OTA de 2,4 MB perdia bytes e o parser respondia
    // "payload grande demais" (bancada 2026-10-02). Janela = buffer/payload.
    EXT_RAM_BSS_ATTR static HostLink link(&USBDevice::linkWrite, nullptr, 2);
    s_link = &link;

    // Pilhas em RAM interna (o watch e apertado): pico medido ~1,7 KB no
    // shell e ~2,7 KB no link (o payload de 8 KB vive no HostLink, PSRAM)
    if (xTaskCreate(usbShellTask, "usb_shell", 6144, nullptr, 3, nullptr) != pdPASS ||
        xTaskCreate(usbLinkTask, "usb_link", 8192, nullptr, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar tasks USB");
        return false;
    }

    ESP_LOGI(TAG, "USB device pronto: CDC0 shell + CDC1 celerctl");
    return true;
}

void USBDevice::shellWrite(const char* data, size_t len) {
    cdcWrite(CDC_SHELL, (const uint8_t*)data, len);
}

void USBDevice::shellPrintf(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
        cdcWrite(CDC_SHELL, (const uint8_t*)buf, (size_t)n);
    }
}

bool USBDevice::linkWrite(const uint8_t* data, size_t len) {
    cdcWrite(CDC_LINK, data, len);
    return true;
}

bool USBDevice::isMounted() {
    return tud_ready();
}

#else  // USB nativo desativado: tudo no-op (celerctl roda na UART via SerialLink)

bool USBDevice::init() {
    return false;
}
void USBDevice::shellWrite(const char*, size_t) {}
void USBDevice::shellPrintf(const char*, ...) {}
bool USBDevice::linkWrite(const uint8_t*, size_t) {
    return false;
}
bool USBDevice::isMounted() {
    return false;
}

#endif  // CONFIG_CELEROS_USB_NATIVE
