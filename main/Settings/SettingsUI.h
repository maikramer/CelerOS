#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include <string>

#include "../Boards/Board.h"

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

    static void drawUpdater(bool isBootCheck = false);
    static void handleUpdaterTouch(uint16_t x, uint16_t y);
    static bool checkUpdateSilent();

    // Fluxo bloqueante de flash OTA: barra de progresso, reboot ao concluir
    // ou tela de erro com botao BACK.
    static void runOtaInstall();

    // Security (PIN lock)
    static void drawSecurity();
    static void handleSecurityTouch(uint16_t x, uint16_t y);

    // Display (brilho)
    static void drawDisplaySettings();
    static void handleDisplayTouch(uint16_t x, uint16_t y);

private:
    static KryonDisplay *tftInstance;
    static void otaProgressCb(int percent);
    static int otaProgressLast;

    // PIN: md5 hex em /local/settings_pin.txt; sessao desbloqueada por 60s
    static bool unlockGate();
    static bool isPinSet();
    static bool verifyPin(const std::string& pin);
    static void setPinFlow();
    static std::string pinPrompt(const char* title, int maxLength);
    static std::string md5String(const std::string& input);
    static unsigned long unlockedUntilMs;

    // Slider de brilho: modal de arraste, salva no release
    static void runBrightnessSlider(int x0);
    static void drawBrightnessBar(int level);
};

#endif // SETTINGS_UI_H
