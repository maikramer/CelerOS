#pragma once

#include <Arduino.h>
#include <string>
#include <vector>
#include "WifiConnection.h"

// Resultado de scan para as UIs (SettingsUI/captive portal)
struct KryonScanEntry {
    std::string ssid;
    int32_t rssi = 0;
    bool secure = false;
};

class WebManager {
public:
    // Boot: inicializa WiFi (WifiConnection) com as credenciais de
    // /sd/wifi.txt (fallback /local/wifi.txt) e sobe o servidor web se
    // existir /local/web_on.txt. Retorna true se conectou.
    static bool init();

    // Liga o WiFi em tempo de execucao (mesmo efeito do init() no boot)
    static bool enable();

    // Encerra o servidor e desliga o WiFi em tempo de execucao (sem reboot)
    static void disable();

    // Encerra apenas o servidor web, mantendo o WiFi ligado — usado antes de
    // abrir o captive portal, que precisa da porta 80
    static void stopWebServer();

    // Deve ser chamado no loop principal: consumir o reboot diferido do
    // upload web de firmware
    static void tick();

    // WiFi conectado?
    static bool isActive();
    static bool isWifiConnected();  // alias de isActive()

    // IP atual como string ("" se desconectado)
    static std::string getIPAddress();

    // --- API estendida usada pelas UIs (substitui as chamadas WiFi.* do Arduino) ---

    // Conecta bloqueando ate timeoutMs. saveCreds=true grava /wifi.txt
    // (SD se montado, senao LittleFS). Retorna true se conectou.
    static bool connect(const std::string& ssid, const std::string& password,
                        bool saveCreds = true, uint32_t timeoutMs = 15000);

    // Desconecta o STA sem apagar credenciais
    static void disconnect();

    // Scan bloqueante (~2s). Preenche "out" (ate maxN) e retorna a quantidade.
    static int scanNetworks(KryonScanEntry* out, int maxN);

    // Scan assincrono: dispara e entrega o resultado via callback no evento
    // do componente Wifi.
    static bool startScanAsync();
    static std::vector<KryonScanEntry> getLastScan();

    // Acesso direto ao componente WifiConnection (portal, casos especiais)
    static WifiConnection& wifi();

    // Servidor web no ar?
    static bool isServerRunning();

private:
    static void startWebServerIfNeeded();
    static void onStateChanged(WifiConnection* conn, WiFiConnectionState oldState, WiFiConnectionState newState);
};
