#ifndef KRYONOS_WIFI_SETUP_PORTAL_H
#define KRYONOS_WIFI_SETUP_PORTAL_H

#include <string>

// Captive portal do KryonOS sobre o componente Wifi/CaptivePortal.
// Ciclo nao-bloqueante (a tela Kui dirige do loop principal):
//   begin()  -> sobe o AP + portal (nao bloqueia)
//   poll()   -> processa credenciais entregues pela pagina; quando ha
//               credenciais, CONECTA (~15s bloqueantes com o portal vivo)
//               e devolve o estado resultante
//   end()    -> derruba o AP (STA preservado se conectado)
class WifiSetupPortal {
public:
    enum State { Waiting, Connecting, Connected, Failed };

    static bool begin();
    static State poll(std::string& detail);
    static void end();
    static const char* apSsid();
};

#endif  // KRYONOS_WIFI_SETUP_PORTAL_H
