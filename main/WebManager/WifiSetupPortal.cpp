#include "WifiSetupPortal.h"
#include "WebManager.h"
#include "CaptivePortal.h"   // componente Wifi/
#include "NetworkManager.h"
#include "../Display/Layout.h"
#include "../Utils/StrUtils.h"

#include "esp_mac.h"

// ---------------------------------------------------------------------------
// Estado compartilhado entre a task do httpd (handlers do componente) e o
// loop do modal. Credenciais sao copiadas antes da flag; races benignas.
static CaptivePortal s_portal;
static volatile bool s_credsPending = false;
static std::string s_pendingSsid;
static std::string s_pendingPass;
static bool s_handlersBound = false;

static void portalStatus(KryonDisplay* tft, const std::string& l1, const std::string& l2 = "") {
    tft->fillRect(UI::sx(6), UI::sy(210), UI::sx(228), UI::sy(56), TFT_BLACK);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(l1.c_str(), UI::sx(120), UI::sy(222), UI::font(2));
    if (!l2.empty()) {
        tft->drawString(l2.c_str(), UI::sx(120), UI::sy(244), UI::font(2));
    }
}

bool WifiSetupPortal::runBlocking(KryonDisplay* tft) {
    WebManager::stopWebServer();  // a porta 80 e do portal enquanto ele vive

    // STA primeiro (o init do WifiConnection forca modo STA); o componente
    // promove para APSTA ao subir o AP (patch do WifiAP)
    if (NetworkManager::instance().init(true) != CommonErrorCodes::None) return false;

    // Sufixo do AP: 4 ultimos hex do MAC (softAP)
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::string apSsid = "KryonOS-Setup-" + kstr::fmt("%X", mac[4]) + kstr::fmt("%X", mac[5]);

    CaptivePortalConfig cfg;
    cfg.apSsid = apSsid;
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

    // Tela de espera
    tft->fillScreen(TFT_BLACK);
    tft->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tft->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tft->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tft->setTextColor(TFT_GREEN, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("WiFi Setup Portal", UI::sx(120), UI::sy(21), UI::font(2));

    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString("1. Connect to the AP:", UI::sx(120), UI::sy(70), UI::font(2));
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->drawString(apSsid.c_str(), UI::sx(120), UI::sy(92), UI::font(2));
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString("2. Open a browser:", UI::sx(120), UI::sy(122), UI::font(2));
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->drawString("http://192.168.4.1", UI::sx(120), UI::sy(144), UI::font(2));
    tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft->drawString("Waiting for setup...", UI::sx(120), UI::sy(180), UI::font(2));

    tft->fillRoundRect(UI::sx(70), UI::sy(280), UI::sx(100), UI::sy(30), UI::sx(5), TFT_DARKGREY);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->drawString("SKIP", UI::sx(120), UI::sy(295), UI::font(2));

    while (true) {
        if (s_credsPending) {
            s_credsPending = false;
            std::string ssid = s_pendingSsid;
            std::string pass = s_pendingPass;
            s_portal.reportConnectionState(PortalConnState::Connecting);
            portalStatus(tft, "Connecting to:", ssid);

            // Conecta aqui no loop do modal (bloqueante ~15s): o httpd do
            // portal segue vivo na task dele respondendo /status
            NetworkManager& nm = NetworkManager::instance();
            nm.getWifiConnection()->setConnectionTimeout(15000);
            ErrorCode err = nm.connect(ssid, pass, true);  // salva no NVS
            nm.getWifiConnection()->setConnectionTimeout(10000);

            if (err == CommonErrorCodes::None && nm.isConnected()) {
                s_portal.reportConnectionState(PortalConnState::Connected, nm.getIpAddress());
                portalStatus(tft, "Connected!", nm.getIpAddress());
                delay(1500);
                s_portal.stop();  // APSTA -> STA (conexao preservada)
                return true;
            }

            s_portal.reportConnectionState(PortalConnState::Failed);
            portalStatus(tft, "Connection failed!", "Try again from the page");
        }

        uint16_t tx = 0, ty = 0;
        if (tft->getTouch(&tx, &ty)) {
            if (ty >= UI::sy(270) && tx >= UI::sx(60) && tx <= UI::sx(180)) {
                while (tft->getTouch(&tx, &ty)) { delay(10); }
                s_portal.stop();
                return false;
            }
            while (tft->getTouch(&tx, &ty)) { delay(10); }
        }

        delay(10);
    }
}
