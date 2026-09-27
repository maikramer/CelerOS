#include "WifiSetupPortal.h"
#include "WebManager.h"
#include "CaptivePortal.h"   // componente Wifi/
#include "NetworkManager.h"
#include "../Utils/StrUtils.h"

#include "esp_mac.h"

// Estado compartilhado entre a task do httpd (handlers do componente) e o
// poll. Credenciais sao copiadas antes da flag; races benignas.
static CaptivePortal s_portal;
static volatile bool s_credsPending = false;
static std::string s_pendingSsid;
static std::string s_pendingPass;
static std::string s_apSsid;
static bool s_handlersBound = false;

bool WifiSetupPortal::begin() {
    WebManager::stopWebServer();  // a porta 80 e do portal enquanto ele vive

    // STA primeiro (o init do WifiConnection forca modo STA); o componente
    // promove para APSTA ao subir o AP (patch do WifiAP)
    if (NetworkManager::instance().init(true) != CommonErrorCodes::None) return false;

    // Sufixo do AP: 4 ultimos hex do MAC (softAP)
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    s_apSsid = "KryonOS-Setup-" + kstr::fmt("%X", mac[4]) + kstr::fmt("%X", mac[5]);

    CaptivePortalConfig cfg;
    cfg.apSsid = s_apSsid;
    cfg.title = "KryonOS WiFi Setup";
    cfg.deviceName = "KryonOS";
    cfg.scanOnStart = true;
    s_portal.setConfig(cfg);

    if (!s_handlersBound) {
        s_handlersBound = true;
        // Handler roda na task do httpd: so copia e seta flag
        s_portal.onCredentialsReceived.addHandler([](const WiFiCredentials& creds) {
            s_pendingSsid = creds.ssid;
            s_pendingPass = creds.password;
            s_credsPending = true;
        });
    }

    s_credsPending = false;
    if (!s_portal.start()) {
        Serial.println("Falha ao iniciar captive portal");
        return false;
    }
    return true;
}

WifiSetupPortal::State WifiSetupPortal::poll(std::string& detail) {
    if (!s_credsPending) return Waiting;

    s_credsPending = false;
    detail = s_pendingSsid;
    s_portal.reportConnectionState(PortalConnState::Connecting);

    // Conecta aqui no contexto do chamador (bloqueante ~15s): o httpd do
    // portal segue vivo na task dele respondendo /status
    NetworkManager& nm = NetworkManager::instance();
    nm.getWifiConnection()->setConnectionTimeout(15000);
    ErrorCode err = nm.connect(s_pendingSsid, s_pendingPass, true);  // salva no NVS
    nm.getWifiConnection()->setConnectionTimeout(10000);

    if (err == CommonErrorCodes::None && nm.isConnected()) {
        s_portal.reportConnectionState(PortalConnState::Connected, nm.getIpAddress());
        detail = nm.getIpAddress();
        s_portal.stop();  // APSTA -> STA (conexao preservada)
        return Connected;
    }

    s_portal.reportConnectionState(PortalConnState::Failed);
    return Failed;
}

void WifiSetupPortal::end() {
    s_portal.stop();
}

const char* WifiSetupPortal::apSsid() {
    return s_apSsid.c_str();
}
