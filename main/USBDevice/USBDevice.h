#ifndef CELER_USB_DEVICE_H
#define CELER_USB_DEVICE_H

#include <stddef.h>
#include <stdint.h>

// Subsistema USB device do CelerOS (exclusivo do ESP32-S3, que tem USB-OTG):
//   CDC0 -> shell interativo (terminal humano: minicom/idf.py monitor)
//   CDC1 -> protocolo binario HostLink (ferramenta celerctl)
// No CYD (ESP32 classico, sem periferico USB device) tudo vira no-op.

class USBDevice {
public:
    static bool init();

    // Escrita no shell (CDC0) e no canal da ferramenta (CDC1). Serializadas
    // internamente por mutex; descartam silenciosamente se o host desmontou.
    static void shellWrite(const char* data, size_t len);
    static void shellPrintf(const char* fmt, ...);
    static bool linkWrite(const uint8_t* data, size_t len);

    static bool isMounted();
};

#endif // CELER_USB_DEVICE_H
