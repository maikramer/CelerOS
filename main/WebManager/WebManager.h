#pragma once

#include <Arduino.h>
#include <string>
#include <vector>
#include "NetworkManager.h"
#include "WifiConnection.h"

// Resultado de scan para as UIs (SettingsUI/captive portal)
struct KryonScanEntry {
    std::string ssid;
    int32_t rssi = 0;
    bool secure = false;
};

class WebManager {
public:
    // Boot: inicializa o NetworkManager (componente Connection — background
    // task de reconexao/roaming), importa wifi.txt legado para o
    // NetworkCredentialStore (NVS) uma unica vez e conecta na melhor rede
    // conhecida. Sobe o servidor web se existir /local/web_on.txt.
    // Retorna true se conectou.
    static bool init();
    // Boot assincrono: prepara NVS/credenciais/eventos e deixa a task de
    // reconexao do NetworkManager conectar sozinha (elimina a race do scan
    // concorrente do boot antigo). Nunca bloqueia; estado via isActive().
    static bool startAsync();

    // Liga o WiFi em tempo de execucao (mesmo efeito do init() no boot)
    static bool enable();

    // Encerra o servidor e desliga o WiFi em tempo de execucao (sem reboot).
    // O NetworkManager e dono do radio — nao ha esp_wifi_stop() aqui.
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

    // Conecta bloqueando ate timeoutMs. saveCreds=true grava no
    // NetworkCredentialStore (NVS) — wifi.txt nao e mais escrito.
    // Retorna true se conectou.
    static bool connect(const std::string& ssid, const std::string& password,
                        bool saveCreds = true, uint32_t timeoutMs = 15000);

    // Desconecta o STA sem apagar credenciais
    static void disconnect();

    // Scan bloqueante (~2s). Preenche "out" (ate maxN) e retorna a quantidade.
    static int scanNetworks(KryonScanEntry* out, int maxN);

    // Scan assincrono: dispara e entrega o resultado via callback no evento
    // do componente Connection.
    static bool startScanAsync();
    static std::vector<KryonScanEntry> getLastScan();

    // Ha redes salvas? (store NVS ou wifi.txt legado ainda nao importado)
    static bool hasSavedNetworks();

    // Esquece TODAS as redes: limpa o store NVS, apaga wifi.txt (e o
    // .migrated) e desliga o WiFi. Usado pelo FORGET do Settings.
    static void forgetAllNetworks();

    // Acesso direto ao WifiConnection do NetworkManager (portal, casos especiais)
    static WifiConnection& wifi();

    // Servidor web no ar?
    static bool isServerRunning();

    // Sobe o servidor se ainda nao estiver rodando (web_on.txt) — publico:
    // usado pelo binding System.webSetActive do JS
    static void startWebServerIfNeeded();

private:
    static void importLegacyWifiTxt();
    static void onNetworkStateChanged(NetworkState oldState, NetworkState newState);
};
