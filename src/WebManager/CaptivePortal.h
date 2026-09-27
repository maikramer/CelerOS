#ifndef KRYONOS_CAPTIVE_PORTAL_H
#define KRYONOS_CAPTIVE_PORTAL_H

#include "../Display/Display.h"

// Portal de configuracao WiFi (captive portal): AP "KryonOS-Setup-XXXX" +
// DNS wildcard + pagina web de scan/conexao. Conceito portado do
// satisfaction-hub (esp_components/Wifi/CaptivePortal), reescrito sobre o
// ESPAsyncWebServer existente + DNSServer do core Arduino (zero libs novas).
//
// A tentativa de conexao STA roda fora dos handlers do servidor (no loop do
// modal), para nao travar a task do AsyncTCP; a pagina acompanha o resultado
// via GET /status.
class CaptivePortal {
public:
    // Abre o portal e bloqueia ate:
    //   - conectar (retorna true; credenciais salvas em /sd|local/wifi.txt)
    //   - o usuario tocar SKIP no display (retorna false)
    // "tft" recebe a tela de espera com nome do AP e IP do portal.
    static bool runBlocking(KryonDisplay* tft);
};

#endif // KRYONOS_CAPTIVE_PORTAL_H
