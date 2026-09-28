#include <Arduino.h>
#include "USBDevice/LogSink.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Boards/Board.h"
#include "Display/Layout.h"
#include "Display/Theme.h"
#include "Display/Backlight.h"
#include "FileSystem/FileSystem.h"
#include "Launcher/LauncherUI.h"
#include "Settings/TouchCalibrator.h"
#include "WebManager/WebManager.h"
#include "WebManager/WifiSetupPortal.h"
#include "Runtime/JSBindings.h"
#include "Assets/SplashLogo.h"
#include "Kernel/Core/CelerKernel.h"
#include "Kernel/TimeManager.h"
#include "Launcher/Screens.h"
#include "USBDevice/SerialLink.h"
#include "Boards/Board.h"
#if CONFIG_CELEROS_USB_NATIVE
#include "USBDevice/USBDevice.h"
#endif

// Telas de sistema (Settings, App Store, Installer, Help, Web Server) sao
// apps JS em /local/apps (W8) — o firmware carrega so o core + launcher.
// currentState sobrevive apenas para o TouchCalibrator sinalizar "voltar
// ao launcher" no boot.
#define STATE_LAUNCHER 0

int currentState = STATE_LAUNCHER;
CelerDisplay& tft = Board::display();
static LauncherScreen s_launcher;  // base da pilha do Navigator

// Splash de boot no tema: logo embutida (gerada por tools/make_splash.py) +
// status + trilho da barra de progresso (a barra e preenchida pelo scan de
// apps em (sx(20), sy(200), sx(200), sy(10))). O blit usa zoom do design
// (ex.: 2x no 480x480), entao o asset fica so na resolucao 240.
static void bootSplash(const char* status) {
    tft.fillScreen(THEME_BG);
    const float zoom = (float)UI::W / 240.0f;
    tft.drawPng(kSplashLogoPng, sizeof(kSplashLogoPng),
                UI::cx() - (int32_t)(SPLASH_LOGO_W * zoom / 2.0f),
                UI::sy(115) - (int32_t)(SPLASH_LOGO_H * zoom / 2.0f),
                0, 0, 0, 0, zoom, zoom);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(THEME_TEXT_DIM);
    tft.drawString(status, UI::cx(), UI::sy(175), UI::font(1));
    tft.fillRoundRect(UI::sx(20), UI::sy(200), UI::sx(200), UI::sy(10), UI::sy(5), THEME_CARD);
}

static void celerSetup() {
    Serial.begin(115200);
    celer_log_println("\n--- CelerOS Booting ---");
    celer_log_printf("board: %s\n", Board::profile().name);
    if (Board::profile().hasPsram) {
        celer_log_printf("PSRAM: %u bytes (free %u)\n", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
    }

    // Init TFT (HAL da placa)
    Board::init();
    tft.setRotation(0);
    UI::init(tft.width(), tft.height());
    ESP_LOGI("celer.lcd", "depth=%d rot=%d w=%d h=%d",
             (int)tft.getColorDepth(), (int)tft.getRotation(), tft.width(), tft.height());

    bootSplash("Iniciando...");

    // Initialize File Systems (LittleFS & SD)
    if (!FileSystem::init()) {
        celer_log_println("File System Warning: One or more FS failed to mount.");
        tft.setTextColor(THEME_WARN);
        tft.drawString("Aviso: falha ao montar FS", UI::cx(), UI::sy(230), UI::font(1));
        delay(1000);
    }

    // Console/shell + canal celerctl na UART do console (CH340 no PC)
    SerialLink::init();
#if CONFIG_CELEROS_USB_NATIVE
    // USB nativo (TinyUSB): so para placas com GPIO19/20 livres
    USBDevice::init();
#endif

    // Brilho do backlight (depois do FS: le /local/brightness.txt)
    Backlight::init(&tft);
    
    // Initialize Time Manager
    TimeManager::init();
    
    // WiFi 100% assincrono: prepara credenciais/eventos e a task de
    // reconexao do NetworkManager conecta sozinha (sem race de scan —
    // o boot antigo concorria com a task e caia na tela "WiFi Setup").
    // Sem rede, o launcher mostra o banner "WiFi offline".
    WebManager::startAsync();
    celer_log_printf("DEBUG: Free heap before Kernel: %u\n", (unsigned)ESP.getFreeHeap());

    // Initialize JS Runtime
    celer_log_println("DEBUG: Starting CelerKernel...");
    CelerKernel::init(&tft);
    celer_log_println("CelerKernel initialized successfully.");
    celer_log_printf("DEBUG: Free heap after Kernel: %u\n", (unsigned)ESP.getFreeHeap());

    // Init UI Components
    LauncherUI::init(&tft);
    TouchCalibrator::init(&tft);

    // Initial App Scan (barra da splash: LauncherUI::scanLocalApps preenche)
    celer_log_println("DEBUG: Scanning Local Apps...");
    bootSplash("Carregando apps...");
    LauncherUI::scanLocalApps();
    LauncherUI::needsRescan = false;
    celer_log_println("DEBUG: Local Apps Scanned.");

    // Touch resistivo sem calibracao salva: roda o calibrador antes da UI.
    // Placas com touch capacitivo (GT911) pulam a calibracao.
    if (!Board::profile().capacitiveTouch) {
        uint16_t calData[5];
        if (!FileSystem::readCalData(calData)) {
            celer_log_println("No calibration data. Entering calibrator.");
            TouchCalibrator::runCalibration();
        } else {
            tft.setTouch(calData);
        }
    }

    // UI nova: launcher e a base da pilha do Navigator
    kui::Navigator::begin(tft);
    kui::Navigator::push(&s_launcher);
    currentState = STATE_LAUNCHER;
    celer_log_println("DEBUG: Setup complete, entering loop!");
}

static void celerLoop() {
    // UI (input + redraw)
    kui::Navigator::tick();

    // Reboot diferido do upload web de firmware (/update)
    WebManager::tick();
    TimeManager::tick(WebManager::isActive());

    delay(5);
}

// Entry point ESP-IDF: setup + loop na main task (stack 32KB via sdkconfig)
extern "C" void app_main(void) {
    celerSetup();
    while (true) {
        celerLoop();
    }
}
