#ifndef KRYONOS_WIFI_SETUP_PORTAL_H
#define KRYONOS_WIFI_SETUP_PORTAL_H

#include "../Boards/Board.h"

// Captive portal do KryonOS sobre o componente Wifi/CaptivePortal: wrapper
// fino que adiciona a tela no display (nome do AP, IP do portal, SKIP) e a
//conexao STA — esta roda no loop do modal, nunca no handler do httpd. O
// resultado chega a pagina via reportConnectionState/GET /status.
class WifiSetupPortal {
public:
    // Abre o portal e bloqueia ate conectar (true; credenciais salvas no
    // NetworkCredentialStore) ou o usuario tocar SKIP (false).
    static bool runBlocking(KryonDisplay* tft);
};

#endif // KRYONOS_WIFI_SETUP_PORTAL_H
