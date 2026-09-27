#include <Arduino.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Boards/Board.h"
#include "Display/Layout.h"
#include "Display/Backlight.h"
#include "FileSystem/FileSystem.h"
#include "Launcher/LauncherUI.h"
#include "Settings/SettingsUI.h"
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

static void kryonSetup() {
    Serial.begin(115200);
    delay(1000);
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

    // Cartao de teste de cores (~1.2s): R G B W Y C na metade de cima.
    // Diagnostico visual da pipeline de cor do painel RGB.
    {
        int bw = tft.width() / 6;
        uint32_t bars[6] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0xFFFF00, 0x00FFFF};
        for (int i = 0; i < 6; i++) {
            tft.fillRect(i * bw, 0, bw, tft.height() / 2, bars[i]);
        }
        tft.setTextColor(0x000000, 0xFFFFFF);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("COLOR TEST", 8, 8, 4);
        delay(1200);
    }

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Booting KryonOS...", UI::cx(), UI::cy(), UI::font(2));

    // Initialize File Systems (LittleFS & SD)
    if (!FileSystem::init()) {
        Serial.println("File System Warning: One or more FS failed to mount.");
        tft.drawString("FS Mount Warning!", UI::cx(), UI::sy(180), UI::font(2));
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
    SettingsUI::init(&tft);
    InstallerUI::init(&tft);
    TouchCalibrator::init(&tft);
    WebServerAppUI::init(&tft);
    AppStoreUI::init(&tft);
    HelpCenterUI::init(&tft);

    // Initial App Scan (with loading bar)
    Serial.println("DEBUG: Scanning Local Apps...");
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Loading Apps...", UI::cx(), UI::cy(), UI::font(2));
    tft.drawRect(UI::sx(18), UI::sy(198), UI::sx(204), UI::sy(14), TFT_WHITE); // Loading bar outline
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
