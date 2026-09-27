#include <Arduino.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Boards/Board.h"
#include "Display/Layout.h"
#include "Display/Theme.h"
#include "Display/Backlight.h"
#include "FileSystem/FileSystem.h"
#include "Launcher/LauncherUI.h"
#include "Launcher/InstallerUI.h"
#include "Settings/TouchCalibrator.h"
#include "WebManager/WebManager.h"
#include "WebManager/WifiSetupPortal.h"
#include "Runtime/JSBindings.h"
#include "Kernel/Core/HarixKernel.h"
#include "WebServerApp/WebServerAppUI.h"
#include "Kernel/TimeManager.h"
#include "Launcher/AppStoreUI.h"
#include "Launcher/HelpCenterUI.h"
#include "Launcher/Screens.h"
#include "USBDevice/SerialLink.h"
#include "Boards/Board.h"
#if CONFIG_KRYONOS_USB_NATIVE
#include "USBDevice/USBDevice.h"
#endif

// Define states
#define STATE_LAUNCHER 0
#define STATE_SETTINGS 1
#define STATE_RUN_APP 2
#define STATE_INSTALLER 3
#define STATE_CALIBRATOR 4
#define STATE_WEB_APP 5
#define STATE_SETTINGS_WIFI 6
#define STATE_SETTINGS_ABOUT 7
#define STATE_SETTINGS_APPS 8
#define STATE_SETTINGS_TIME 9
#define STATE_SETTINGS_TIME_MANUAL 10
#define STATE_UPDATER_BOOT 11
#define STATE_UPDATER_MANUAL 12
#define STATE_APP_STORE 13
#define STATE_HELP_CENTER 14
#define STATE_SETTINGS_SECURITY 15
#define STATE_SETTINGS_DISPLAY 16

int currentState = STATE_LAUNCHER;
KryonDisplay& tft = Board::display();
static LauncherScreen s_launcher;  // base da pilha do Navigator

// Splash de boot no tema: logo + status + trilho da barra de progresso (a
// barra e preenchida pelo scan de apps em (sx(20), sy(200), sx(200), sy(10))).
static void bootSplash(const char* status) {
    tft.fillScreen(THEME_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(THEME_TEXT);
    tft.drawString("KryonOS", UI::cx(), UI::sy(140), UI::big ? &fonts::FreeSansBold24pt7b : &fonts::FreeSansBold18pt7b);
    tft.setTextColor(THEME_TEXT_DIM);
    tft.drawString(status, UI::cx(), UI::sy(175), UI::font(1));
    tft.fillRoundRect(UI::sx(20), UI::sy(200), UI::sx(200), UI::sy(10), UI::sy(5), THEME_CARD);
}

static void kryonSetup() {
    Serial.begin(115200);
    Serial.println("\n--- KryonOS Booting ---");
    Serial.printf("board: %s\n", Board::profile().name);
    if (Board::profile().hasPsram) {
        Serial.printf("PSRAM: %u bytes (free %u)\n", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
    }

    // Init TFT (HAL da placa)
    Board::init();
    tft.setRotation(0);
    UI::init(tft.width(), tft.height());
    ESP_LOGI("kryon.lcd", "depth=%d rot=%d w=%d h=%d",
             (int)tft.getColorDepth(), (int)tft.getRotation(), tft.width(), tft.height());

    bootSplash("Iniciando...");

    // Initialize File Systems (LittleFS & SD)
    if (!FileSystem::init()) {
        Serial.println("File System Warning: One or more FS failed to mount.");
        tft.setTextColor(THEME_WARN);
        tft.drawString("Aviso: falha ao montar FS", UI::cx(), UI::sy(230), UI::font(1));
        delay(1000);
    }

    // Console/shell + canal kryonctl na UART do console (CH340 no PC)
    SerialLink::init();
#if CONFIG_KRYONOS_USB_NATIVE
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
    Serial.printf("DEBUG: Free heap before Kernel: %u\n", (unsigned)ESP.getFreeHeap());

    // Initialize JS Runtime
    Serial.println("DEBUG: Starting HarixKernel...");
    HarixKernel::init(&tft);
    Serial.println("HarixKernel initialized successfully.");
    Serial.printf("DEBUG: Free heap after Kernel: %u\n", (unsigned)ESP.getFreeHeap());

    // Init UI Components
    LauncherUI::init(&tft);
    InstallerUI::init(&tft);
    TouchCalibrator::init(&tft);
    WebServerAppUI::init(&tft);
    AppStoreUI::init(&tft);
    HelpCenterUI::init(&tft);

    // Initial App Scan (barra da splash: LauncherUI::scanLocalApps preenche)
    Serial.println("DEBUG: Scanning Local Apps...");
    bootSplash("Carregando apps...");
    LauncherUI::scanLocalApps();
    LauncherUI::needsRescan = false;
    Serial.println("DEBUG: Local Apps Scanned.");

    // Touch resistivo sem calibracao salva: roda o calibrador antes da UI.
    // Placas com touch capacitivo (GT911) pulam a calibracao.
    if (!Board::profile().capacitiveTouch) {
        uint16_t calData[5];
        if (!FileSystem::readCalData(calData)) {
            Serial.println("No calibration data. Entering calibrator.");
            TouchCalibrator::runCalibration();
        } else {
            tft.setTouch(calData);
        }
    }

    // UI nova: launcher e a base da pilha do Navigator
    kui::Navigator::begin(tft);
    kui::Navigator::push(&s_launcher);
    currentState = STATE_LAUNCHER;
    Serial.println("DEBUG: Setup complete, entering loop!");
}

// Guardiao da transicao: telas antigas ainda navegam escrevendo em
// currentState; aqui a mudanca e refletida na pilha do Navigator.
static void pumpLegacyNavigation() {
    static int last = STATE_LAUNCHER;
    if (currentState == last) return;
    last = currentState;

    if (currentState == STATE_LAUNCHER) {
        kui::Navigator::home();
        return;
    }
    if (currentState == STATE_RUN_APP || currentState == STATE_CALIBRATOR) return;  // tratados fora
    LegacyScreen* s = LegacyScreen::forState(currentState);
    if (s != nullptr) {
        // substitui o topo se for tela antiga diferente; senao empilha
        kui::Screen* top = kui::Navigator::top();
        if (top != nullptr && top != (kui::Screen*)&s_launcher && kui::Navigator::depth() > 1) {
            kui::Navigator::replace(s);
        } else {
            kui::Navigator::push(s);
        }
    }
}

static void kryonLoop() {
    // UI (input + redraw)
    kui::Navigator::tick();

    // Reboot diferido do upload web de firmware (/update)
    WebManager::tick();

    // Navegacao das telas antigas via currentState -> pilha do Navigator
    pumpLegacyNavigation();

    delay(5);
}

// Entry point ESP-IDF: setup + loop na main task (stack 32KB via sdkconfig)
extern "C" void app_main(void) {
    kryonSetup();
    while (true) {
        kryonLoop();
    }
}
