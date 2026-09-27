#ifndef CELER_SERIAL_LINK_H
#define CELER_SERIAL_LINK_H

#include <stddef.h>
#include <stdint.h>

// Transporte do CelerLink sobre a UART do console (UART0, o mesmo canal que
// o CH340 expoe como USB no PC). Multiplexa dois modos no mesmo canal:
//
//   CONSOLE - shell interativo humano (minicom/idf.py monitor): eco, prompt
//             "celer> ", logs ESP_LOG/Serial visiveis
//   LINK    - frames binarios CelerLink (celerctl). Entrada no modo acontece
//             ao receber um frame HELLO (0x43 0x01 ...) - digitacao humana
//             nao consegue produzir essa sequencia. Logs sao suprimidos
//             enquanto o link esta ativo para nao intercalar nos frames.
//
// O link volta ao modo CONSOLE apos ~8s sem frames (restaura baud e logs).
class SerialLink {
public:
    static bool init();

    // Writer/baud-hook instalados no CelerLink
    static bool writeFrame(const uint8_t* data, size_t len);
    static void setBaud(uint32_t baud);

    static bool linkActive();
};

#endif // CELER_SERIAL_LINK_H
