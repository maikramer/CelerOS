#include "WebServerAppUI.h"
#include "../Display/Layout.h"
#include "../WebManager/WebManager.h"
#include "../File System/FileSystem.h"

KryonDisplay *WebServerAppUI::tftInstance = nullptr;

void WebServerAppUI::init(KryonDisplay *tft) {
    tftInstance = tft;
}

void WebServerAppUI::draw() {
    if (!tftInstance) return;
    
    tftInstance->fillScreen(TFT_BLACK);
    
    // Draw the main border
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    // Header Bar
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_CYAN);
    tftInstance->setTextColor(TFT_CYAN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Web Server Manager", UI::sx(120), UI::sy(21), UI::font(2));

    bool wifiDisabled = FileSystem::exists("/local/nowifi.txt");
    bool serverEnabled = FileSystem::exists("/local/web_on.txt");
    bool isConnected = WebManager::isActive();

    tftInstance->setTextDatum(TL_DATUM);
    
    int y = 45;
    int spacing = 20;

    if (wifiDisabled) {
        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
        tftInstance->drawString("WiFi is DISABLED", UI::sx(15), UI::sy(y), UI::font(2));
        y += spacing + 5;
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Please turn on WiFi", UI::sx(15), UI::sy(y), UI::font(2));
        y += spacing;
        tftInstance->drawString("to use Web Server.", UI::sx(15), UI::sy(y), UI::font(2));
    } else if (!serverEnabled) {
        tftInstance->setTextColor(TFT_ORANGE, TFT_BLACK);
        tftInstance->drawString("Web Server is OFF", UI::sx(15), UI::sy(y), UI::font(2));
        y += spacing + 5;
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Turn it ON to access", UI::sx(15), UI::sy(y), UI::font(2));
        y += spacing;
        tftInstance->drawString("the file manager.", UI::sx(15), UI::sy(y), UI::font(2));
    } else if (!isConnected) {
        tftInstance->setTextColor(TFT_YELLOW, TFT_BLACK);
        tftInstance->drawString("CONNECTION FAILED!", UI::sx(15), UI::sy(y), UI::font(2));
        y += spacing + 5;
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Web server is ON but", UI::sx(15), UI::sy(y), UI::font(2));
        y += spacing;
        tftInstance->drawString("WiFi is not connected.", UI::sx(15), UI::sy(y), UI::font(2));
    } else {
        String ip = WebManager::getIPAddress();
        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->drawString("Status:", UI::sx(15), UI::sy(y), UI::font(2));
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("RUNNING", UI::sx(80), UI::sy(y), UI::font(2));
        y += spacing + 5;

        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->drawString("IP:", UI::sx(15), UI::sy(y), UI::font(2));
        tftInstance->setTextColor(TFT_CYAN, TFT_BLACK);
        tftInstance->drawString(ip, UI::sx(80), UI::sy(y), UI::font(2));
        y += spacing;

        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->drawString("Port:", UI::sx(15), UI::sy(y), UI::font(2));
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("80", UI::sx(80), UI::sy(y), UI::font(2));
        y += spacing + 10;
        
        tftInstance->setTextColor(TFT_ORANGE, TFT_BLACK);
        tftInstance->drawString("Visit the IP address", UI::sx(15), UI::sy(y), UI::font(2));
        y += spacing;
        tftInstance->drawString("in your web browser", UI::sx(15), UI::sy(y), UI::font(2));
    }

    // Toggle Button (bottom area, above footer)
    tftInstance->setTextDatum(MC_DATUM);
    if (!serverEnabled || wifiDisabled) {
        tftInstance->fillRoundRect(UI::sx(60), UI::sy(235), UI::sx(120), UI::sy(35), UI::sx(4), TFT_DARKGREY);
        tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
        tftInstance->drawString("Turn ON", UI::sx(120), UI::sy(252), UI::font(2));
    } else {
        tftInstance->fillRoundRect(UI::sx(60), UI::sy(235), UI::sx(120), UI::sy(35), UI::sx(4), TFT_RED);
        tftInstance->setTextColor(TFT_WHITE, TFT_RED);
        tftInstance->drawString("Turn OFF", UI::sx(120), UI::sy(252), UI::font(2));
    }

    // Touch Footer
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("EXIT", UI::sx(120), UI::sy(300), UI::font(2));
}

void WebServerAppUI::handleTouch(uint16_t x, uint16_t y) {
    extern int currentState;

    // Toggle Button
    if (x >= UI::sx(60) && x <= UI::sx(180) && y >= UI::sy(235) && y <= UI::sy(270)) {
        bool serverEnabled = FileSystem::exists("/local/web_on.txt");
        if (serverEnabled) {
            FileSystem::deleteFile("/local/web_on.txt");
        } else {
            FileSystem::writeTextFile("/local/web_on.txt", "1");
        }
        
        tftInstance->fillScreen(TFT_BLACK);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("Rebooting to Apply...", UI::sx(120), UI::sy(160), UI::font(2));
        delay(1000);
        ESP.restart();
    }

    // Bottom Nav: EXIT
    if (y >= UI::sy(285)) {
        if (x > UI::sx(60) && x < UI::sx(180)) {
            currentState = 0; // STATE_LAUNCHER
        }
    }
}
