#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include "../Display/Display.h"

class SettingsUI {
public:
    static void init(KryonDisplay *tft);
    static void draw();
    static void handleTouch(uint16_t x, uint16_t y);

    static void drawAbout();
    static void handleAboutTouch(uint16_t x, uint16_t y);

    static void drawWiFi();
    static void handleWiFiTouch(uint16_t x, uint16_t y);

    static void drawApps();
    static void handleAppsTouch(uint16_t x, uint16_t y);

    static void drawTimeSettings();
    static void handleTimeTouch(uint16_t x, uint16_t y);

    static void drawTimeManual();
    static void handleTimeManualTouch(uint16_t x, uint16_t y);

    static void scanAndConnectWiFi();

    static void drawUpdater(bool isBootCheck = false);
    static void handleUpdaterTouch(uint16_t x, uint16_t y);
    static bool checkUpdateSilent();

    // Fluxo bloqueante de flash OTA: barra de progresso, reboot ao concluir
    // ou tela de erro com botao BACK.
    static void runOtaInstall();

private:
    static KryonDisplay *tftInstance;
    static void otaProgressCb(int percent);
    static int otaProgressLast;
};

#endif // SETTINGS_UI_H
