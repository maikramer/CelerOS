#include "SettingsUI.h"
#include "../Display/Layout.h"
#include "../Display/Theme.h"
#include "../Display/Icon.h"
#include "../FileSystem/FileSystem.h"
#include "../Kernel/TimeManager.h"
#include "../Keyboard/MyKeyboard.h"
#include "../WebManager/WebManager.h"
#include "../Launcher/LauncherUI.h"
#include "../OTA/OtaManager.h"
#include "../WebManager/WifiSetupPortal.h"
#include "esp_rom_md5.h"
#include "../Display/Backlight.h"
#include "../Utils/StrUtils.h"


KryonDisplay *SettingsUI::tftInstance = nullptr;
bool showResetDialog = false;

void SettingsUI::init(KryonDisplay *tft) {
    tftInstance = tft;
}

std::string formatBytes(uint64_t bytes) {
    if (bytes < 1024) return std::to_string((uint32_t)bytes) + " B";
    else if (bytes < (1024 * 1024)) return std::to_string((uint32_t)(bytes / 1024)) + " KB";
    else if (bytes < (1024 * 1024 * 1024)) return std::to_string((uint32_t)(bytes / (1024 * 1024))) + " MB";
    else return std::to_string((uint32_t)(bytes / (1024 * 1024 * 1024))) + " GB";
}

// ----------------------------------------------------
// MAIN SETTINGS MENU
// ----------------------------------------------------

void SettingsUI::draw() {
    if (!tftInstance) return;

    // Gate de PIN: bloqueia o acesso aos Settings quando ha PIN salvo e a
    // sessao expirou (porte do PasswordScreen do satisfaction-hub)
    if (!unlockGate()) {
        extern int currentState;
        currentState = 0;  // cancelado: volta ao launcher
        return;
    }

    tftInstance->fillScreen(THEME_BG);

    // Header no estilo do launcher
    int hdr = UI::sy(56);
    tftInstance->fillRect(0, 0, UI::W, hdr, THEME_CARD);
    tftInstance->fillRect(0, hdr - UI::sy(3), UI::W, UI::sy(3), THEME_ACCENT);
    tftInstance->setTextColor(THEME_TEXT, THEME_CARD);
    tftInstance->setTextDatum(ML_DATUM);
    tftInstance->drawString("Settings", UI::sx(14), hdr / 2, UI::font(4));

    // Itens do menu (calibrador so existe em placas resistivas)
    const char* icons[8] = { "wifi_on", "settings", "app", "time", "about", "update", "settings", "web" };
    const char* labels[8] = { "WiFi", "Calibrator", "Apps", "Time & Region", "About", "Updates", "Security", "Display" };
    int count = 8;
#ifdef KRYONOS_TOUCH_CAPACITIVE
    icons[1] = "settings";  // placeholder oculto abaixo
    count = 7;
    const int mapIdx[7] = { 0, 2, 3, 4, 5, 6, 7 };  // sem o calibrador
#else
    const int mapIdx[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#endif

    int n = count;
    int cols = 2;
    int rows = (n + cols - 1) / cols;
    int gap = UI::sx(12);
    int gridTop = hdr + UI::sy(10);
    int areaH = UI::H - gridTop - UI::sy(14);
    int cellW = (UI::W - gap * (cols + 1)) / cols;
    int cellH = (areaH - gap * (rows + 1)) / rows;

    for (int i = 0; i < n; i++) {
        int col = i % cols;
        int row = i / cols;
        int cx = gap + col * (cellW + gap);
        int cy = gridTop + gap + row * (cellH + gap);

        tftInstance->fillRoundRect(cx, cy, cellW, cellH, UI::sx(10), THEME_CARD);
        tftInstance->drawRoundRect(cx, cy, cellW, cellH, UI::sx(10), THEME_STROKE);

        int iconY = cy + (cellH - Icon::SIZE - (UI::big ? 24 : 14)) / 2;
        Icon::draw(tftInstance, icons[mapIdx[i]], cx + (cellW - Icon::SIZE) / 2, iconY);

        tftInstance->setTextColor(THEME_TEXT, THEME_CARD);
        tftInstance->setTextDatum(TC_DATUM);
        tftInstance->drawString(labels[mapIdx[i]], cx + cellW / 2, iconY + Icon::SIZE + (UI::big ? 5 : 2), UI::font(2));
    }
}

void SettingsUI::handleTouch(uint16_t x, uint16_t y) {
    extern int currentState;

    int hdr = UI::sy(56);
    int gap = UI::sx(12);
    int gridTop = hdr + UI::sy(10);
    int n = 8;
#ifdef KRYONOS_TOUCH_CAPACITIVE
    n = 7;
#endif
    int cols = 2;
    int rows = (n + cols - 1) / cols;
    int areaH = UI::H - gridTop - UI::sy(14);
    int cellW = (UI::W - gap * (cols + 1)) / cols;
    int cellH = (areaH - gap * (rows + 1)) / rows;

    if (x >= gap && y >= gridTop + gap) {
        int col = (x - gap) / (cellW + gap);
        int row = (y - gridTop - gap) / (cellH + gap);
        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            int i = row * cols + col;
            if (i < n) {
#ifdef KRYONOS_TOUCH_CAPACITIVE
                int dests[7] = { 6, 8, 9, 7, 12, 15, 16 };      // WiFi, Apps, Time, About, Updates, Security, Display
#else
                int dests[8] = { 6, 4, 8, 9, 7, 12, 15, 16 };   // WiFi, Calibrator, Apps, Time, About, Updates, Security, Display
#endif
                currentState = dests[i];
                return;
            }
        }
    }

    // BACK: toque no header volta ao launcher
    if (y < hdr) {
        currentState = 0;
    }
}

// ----------------------------------------------------
// WIFI OPTIONS MENU
// ----------------------------------------------------

void SettingsUI::drawWiFi() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    // Header Bar
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("WiFi Options", UI::sx(120), UI::sy(21), UI::font(2));

    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Enable or Disable", UI::sx(120), UI::sy(80), UI::font(2));
    tftInstance->drawString("WiFi and FTP System.", UI::sx(120), UI::sy(100), UI::font(2));

    bool wifiDisabled = FileSystem::exists("/local/nowifi.txt");
    if (wifiDisabled) {
        tftInstance->fillRoundRect(UI::sx(60), UI::sy(140), UI::sx(120), UI::sy(40), UI::sx(4), TFT_DARKGREY);
        tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
        tftInstance->drawString("WiFi: OFF", UI::sx(120), UI::sy(160), UI::font(2));
    } else {
        tftInstance->fillRoundRect(UI::sx(60), UI::sy(140), UI::sx(120), UI::sy(40), UI::sx(4), TFT_BLUE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLUE);
        tftInstance->drawString("WiFi: ON", UI::sx(120), UI::sy(160), UI::font(2));
        
        bool hasWifiCredentials = WebManager::hasSavedNetworks();
        if (hasWifiCredentials) {
            tftInstance->fillRoundRect(UI::sx(10), UI::sy(195), UI::sx(108), UI::sy(30), UI::sx(4), TFT_RED);
            tftInstance->setTextColor(TFT_WHITE, TFT_RED);
            tftInstance->drawString("FORGET", UI::sx(64), UI::sy(210), UI::font(2));

            tftInstance->fillRoundRect(UI::sx(122), UI::sy(195), UI::sx(108), UI::sy(30), UI::sx(4), TFT_DARKCYAN);
            tftInstance->setTextColor(TFT_WHITE, TFT_DARKCYAN);
            tftInstance->drawString("PORTAL", UI::sx(176), UI::sy(210), UI::font(2));
        } else {
            tftInstance->fillRoundRect(UI::sx(40), UI::sy(195), UI::sx(160), UI::sy(30), UI::sx(4), TFT_DARKCYAN);
            tftInstance->setTextColor(TFT_WHITE, TFT_DARKCYAN);
            tftInstance->drawString("WiFi Portal", UI::sx(120), UI::sy(210), UI::font(2));
        }
        
        tftInstance->fillRoundRect(UI::sx(40), UI::sy(240), UI::sx(160), UI::sy(30), UI::sx(4), TFT_ORANGE);
        tftInstance->setTextColor(TFT_WHITE, TFT_ORANGE);
        tftInstance->drawString("Start Web Server", UI::sx(120), UI::sy(255), UI::font(2));
    }

    // Touch Footer
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));
}

void SettingsUI::handleWiFiTouch(uint16_t x, uint16_t y) {
    extern int currentState;

    if (x >= UI::sx(60) && x <= UI::sx(180) && y >= UI::sy(140) && y <= UI::sy(180)) {
        bool wifiDisabled = FileSystem::exists("/local/nowifi.txt");
        if (wifiDisabled) {
            // User is turning WiFi ON
            FileSystem::deleteFile("/local/nowifi.txt");

            // Check if wifi credentials exist
            bool hasWifiCredentials = WebManager::hasSavedNetworks();
            if (!hasWifiCredentials) {
                // Launch the Visual WiFi Scanner
                scanAndConnectWiFi();
                return;
            }

            tftInstance->fillScreen(TFT_BLACK);
            tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
            tftInstance->setTextDatum(MC_DATUM);
            tftInstance->drawString("Starting WiFi...", UI::sx(120), UI::sy(160), UI::font(2));
            WebManager::enable();
        } else {
            // User is turning WiFi OFF (em tempo de execucao, sem reboot)
            FileSystem::writeTextFile("/local/nowifi.txt", "1");
            WebManager::disable();
        }

        drawWiFi();
        return;
    }

    // Forget / WiFi Portal row
    if (y >= UI::sy(195) && y <= UI::sy(225)) {
        bool wifiDisabled = FileSystem::exists("/local/nowifi.txt");
        if (!wifiDisabled) {
            bool hasWifiCredentials = WebManager::hasSavedNetworks();

            // Captive portal: metade direita (com credenciais) ou linha inteira
            bool hitPortal = hasWifiCredentials
                ? (x >= UI::sx(122) && x <= UI::sx(230))
                : (x >= UI::sx(40) && x <= UI::sx(200));
            if (hitPortal) {
                WebManager::stopWebServer();  // libera a porta 80 para o portal
                WifiSetupPortal::runBlocking(tftInstance);
                WebManager::enable();         // restaura STA + servidor conforme o resultado
                drawWiFi();
                return;
            }

            if (hasWifiCredentials && x >= UI::sx(10) && x <= UI::sx(118)) {
                WebManager::forgetAllNetworks();  // limpa NVS + wifi.txt legado

                tftInstance->fillScreen(TFT_BLACK);
                tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
                tftInstance->setTextDatum(MC_DATUM);
                tftInstance->drawString("Network Forgot!", UI::sx(120), UI::sy(160), UI::font(2));
                delay(1000);
                drawWiFi();
                return;
            }
        }
    }

    // Web Server Button
    if (x >= UI::sx(40) && x <= UI::sx(200) && y >= UI::sy(240) && y <= UI::sy(270)) {
        bool wifiDisabled = FileSystem::exists("/local/nowifi.txt");
        if (!wifiDisabled) {
            currentState = 5; // STATE_WEB_APP
            return;
        }
    }

    // Bottom Nav: BACK
    if (y >= UI::sy(285)) {
        if (x > UI::sx(60) && x < UI::sx(180)) {
            currentState = 1; // STATE_SETTINGS
        }
    }
}

// ----------------------------------------------------
// ABOUT DEVICE MENU
// ----------------------------------------------------

void SettingsUI::drawAbout() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    // Header Bar
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("About Device", UI::sx(120), UI::sy(21), UI::font(2));

    // Get Storage Info
    uint64_t fsTotal = FileSystem::getTotalSpace("/local");
    uint64_t fsUsed = FileSystem::getUsedSpace("/local");
    uint64_t fsFree = fsTotal - fsUsed;

    uint64_t sdTotal = FileSystem::getTotalSpace("/sd");
    uint64_t sdUsed = FileSystem::getUsedSpace("/sd");
    uint64_t sdFree = sdTotal - sdUsed;

    tftInstance->setTextColor(TFT_CYAN, TFT_BLACK);
    tftInstance->setTextDatum(TL_DATUM);
    tftInstance->drawString("Internal Memory (LittleFS)", UI::sx(15), UI::sy(40), UI::font(2));
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString(("Total: " + formatBytes(fsTotal)).c_str(), UI::sx(25), UI::sy(60), UI::font(2));
    tftInstance->drawString(("Used:  " + formatBytes(fsUsed)).c_str(), UI::sx(25), UI::sy(80), UI::font(2));
    tftInstance->drawString(("Free:  " + formatBytes(fsFree)).c_str(), UI::sx(25), UI::sy(100), UI::font(2));

    tftInstance->setTextColor(TFT_ORANGE, TFT_BLACK);
    tftInstance->drawString("External Memory (SD Card)", UI::sx(15), UI::sy(125), UI::font(2));
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    if (sdTotal > 0) {
        tftInstance->drawString(("Total: " + formatBytes(sdTotal)).c_str(), UI::sx(25), UI::sy(145), UI::font(2));
        tftInstance->drawString(("Used:  " + formatBytes(sdUsed)).c_str(), UI::sx(25), UI::sy(165), UI::font(2));
        tftInstance->drawString(("Free:  " + formatBytes(sdFree)).c_str(), UI::sx(25), UI::sy(185), UI::font(2));
    } else {
        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
        tftInstance->drawString("SD Card not mounted!", UI::sx(25), UI::sy(145), UI::font(2));
    }
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString(kstr::fmt("Free Heap: %u KB", (unsigned)(ESP.getFreeHeap() / 1024)).c_str(), 15, 205, 2);

    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->drawString((std::string("KryonOS ") + KRYONOS_VERSION).c_str(), UI::sx(15), UI::sy(222), UI::font(2));

    // Reset Apps Button
    tftInstance->fillRoundRect(UI::sx(60), UI::sy(250), UI::sx(120), UI::sy(30), UI::sx(4), TFT_RED);
    tftInstance->setTextColor(TFT_WHITE, TFT_RED);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Reset App Data", UI::sx(120), UI::sy(265), UI::font(2));

    extern bool showResetDialog;
    if (showResetDialog) {
        tftInstance->fillRoundRect(UI::sx(10), UI::sy(80), UI::sx(220), UI::sy(160), UI::sx(8), TFT_DARKGREY);
        tftInstance->setTextColor(TFT_YELLOW, TFT_DARKGREY);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("WARNING!", UI::sx(120), UI::sy(110), UI::font(4));
        tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
        tftInstance->drawString("Format LittleFS &", UI::sx(120), UI::sy(140), UI::font(2));
        tftInstance->drawString("Delete all Apps?", UI::sx(120), UI::sy(160), UI::font(2));
        
        tftInstance->fillRoundRect(UI::sx(30), UI::sy(190), UI::sx(70), UI::sy(30), UI::sx(4), TFT_RED);
        tftInstance->setTextColor(TFT_WHITE, TFT_RED);
        tftInstance->drawString("Yes", UI::sx(65), UI::sy(205), UI::font(2));
        
        tftInstance->fillRoundRect(UI::sx(140), UI::sy(190), UI::sx(70), UI::sy(30), UI::sx(4), TFT_GREEN);
        tftInstance->setTextColor(TFT_BLACK, TFT_GREEN);
        tftInstance->drawString("No", UI::sx(175), UI::sy(205), UI::font(2));
    }

    // Touch Footer
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));
}

void SettingsUI::handleAboutTouch(uint16_t x, uint16_t y) {
    extern int currentState;
    extern bool showResetDialog;

    if (showResetDialog) {
        if (y >= UI::sy(190) && y <= UI::sy(220)) {
            if (x >= UI::sx(30) && x <= UI::sx(100)) { // Yes
                tftInstance->fillScreen(TFT_BLACK);
                tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
                tftInstance->setTextDatum(MC_DATUM);
                tftInstance->drawString("Formatting...", UI::sx(120), UI::sy(160), UI::font(4));
                
                FileSystem::formatLittleFS();
                
                tftInstance->drawString("Rebooting...", UI::sx(120), UI::sy(200), UI::font(4));
                delay(1000);
                ESP.restart();
            } else if (x >= UI::sx(140) && x <= UI::sx(210)) { // No
                showResetDialog = false;
                drawAbout();
            }
        }
        return;
    }

    // Reset Button Touched
    if (x >= UI::sx(60) && x <= UI::sx(180) && y >= UI::sy(250) && y <= UI::sy(280)) {
        showResetDialog = true;
        drawAbout();
        return;
    }

    // Bottom Nav: BACK
    if (y >= UI::sy(285)) {
        if (x > UI::sx(60) && x < UI::sx(180)) {
            currentState = 1; // STATE_SETTINGS
        }
    }
}

// ----------------------------------------------------
// MANAGE APPS MENU
// ----------------------------------------------------

static FileEntry appEntries[50];
static int totalApps = -1;
static int appScroll = 0;
static int appSelected = -1;
static bool appMenuOpen = false;
static bool defaultInstallSD = false; // Loaded lazily

static void loadAppInstallPreference() {
    if (FileSystem::exists("/local/config_install_sd.txt")) {
        defaultInstallSD = true;
    } else {
        defaultInstallSD = false;
    }
}

static void saveAppInstallPreference() {
    if (defaultInstallSD) {
        FileSystem::writeTextFile("/local/config_install_sd.txt", "1");
    } else {
        FileSystem::deleteFile("/local/config_install_sd.txt");
    }
}

void SettingsUI::drawApps() {
    if (!tftInstance) return;
    
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    // Header
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Manage Apps", UI::sx(120), UI::sy(21), UI::font(2));

    // Default Install Location Toggle Button
    if (totalApps == -1) loadAppInstallPreference();
    
    tftInstance->fillRoundRect(UI::sx(10), UI::sy(40), UI::sx(220), UI::sy(30), UI::sx(4), TFT_DARKGREY);
    tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tftInstance->drawString(defaultInstallSD ? "Default Install: SD" : "Default Install: LFS", UI::sx(120), UI::sy(55), UI::font(2));

    // Load Apps
    if (totalApps == -1) {
        totalApps = 0;
        int c1 = FileSystem::listDirectory("/local/apps/", appEntries, 25);
        totalApps += c1;
        
        // Also list /sd/apps/
        int c2 = FileSystem::listDirectory("/sd/apps/", appEntries + totalApps, 25);
        totalApps += c2;
    }

    int yPos = 80;
    int itemsPerPage = 6;
    tftInstance->setTextDatum(TL_DATUM);

    for (int i = 0; i < itemsPerPage; i++) {
        int listIndex = appScroll + i;
        if (listIndex >= totalApps) break;
        
        FileEntry entry = appEntries[listIndex];
        
        uint32_t color = TFT_WHITE;
        if (listIndex == appSelected) {
            tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(30), TFT_BLUE);
        } else {
            tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(30), TFT_BLACK);
        }
        
        tftInstance->setTextColor(color);
        // Show Name
        std::string displayName = entry.name;
        if (entry.isDir) {
            std::string appJsonPath = entry.path;
            if (!kstr::endsWith(appJsonPath, "/")) appJsonPath += "/";
            appJsonPath += "app.json";
            if (FileSystem::exists(appJsonPath.c_str())) {
                std::string jsonContent = FileSystem::readTextFile(appJsonPath.c_str());
                std::string parsedName = FileSystem::parseJsonValue(jsonContent, "name");
                if (parsedName.length() > 0) displayName = parsedName;
            }
        }
        tftInstance->drawString(displayName.c_str(), UI::sx(15), UI::sy(yPos + 8), UI::font(2));

        // Show Drive Marker
        std::string drive = kstr::startsWith(entry.path, "/sd") ? "[SD]" : "[LFS]";
        tftInstance->setTextColor(TFT_YELLOW);
        tftInstance->drawString(drive.c_str(), UI::sx(190), UI::sy(yPos + 8), UI::font(2));
        
        yPos += 35;
    }

    // Scroll buttons
    if (appScroll > 0) {
        tftInstance->fillTriangle(UI::sx(220), UI::sy(85), UI::sx(230), UI::sy(100), UI::sx(210), UI::sy(100), TFT_WHITE);
    }
    if (appScroll + itemsPerPage < totalApps) {
        tftInstance->fillTriangle(UI::sx(220), UI::sy(275), UI::sx(210), UI::sy(260), UI::sx(230), UI::sy(260), TFT_WHITE);
    }

    // Footer
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));

    // Draw Pop-Up Menu
    if (appMenuOpen && appSelected != -1) {
        FileEntry sel = appEntries[appSelected];
        bool isSD = kstr::startsWith(sel.path, "/sd");
        
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(80), UI::sx(200), UI::sy(150), UI::sx(5), TFT_DARKGREY);
        tftInstance->drawRoundRect(UI::sx(20), UI::sy(80), UI::sx(200), UI::sy(150), UI::sx(5), TFT_WHITE);
        
        tftInstance->setTextColor(TFT_YELLOW, TFT_DARKGREY);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("App Actions", UI::sx(120), UI::sy(95), UI::font(2));
        
        // Button: Uninstall
        tftInstance->fillRoundRect(UI::sx(30), UI::sy(110), UI::sx(180), UI::sy(30), UI::sx(4), TFT_RED);
        tftInstance->setTextColor(TFT_WHITE, TFT_RED);
        tftInstance->drawString("Uninstall", UI::sx(120), UI::sy(125), UI::font(2));
        
        // Button: Move
        tftInstance->fillRoundRect(UI::sx(30), UI::sy(150), UI::sx(180), UI::sy(30), UI::sx(4), TFT_ORANGE);
        tftInstance->setTextColor(TFT_BLACK, TFT_ORANGE);
        tftInstance->drawString(isSD ? "Move to LFS" : "Move to SD", UI::sx(120), UI::sy(165), UI::font(2));
        
        // Button: Cancel
        tftInstance->fillRoundRect(UI::sx(30), UI::sy(190), UI::sx(180), UI::sy(30), UI::sx(4), TFT_BLACK);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Cancel", UI::sx(120), UI::sy(205), UI::font(2));
    }
}

void SettingsUI::handleAppsTouch(uint16_t x, uint16_t y) {
    extern int currentState;

    if (appMenuOpen) {
        if (x >= UI::sx(30) && x <= UI::sx(210)) {
            FileEntry sel = appEntries[appSelected];
            bool isSD = kstr::startsWith(sel.path, "/sd");
            
            if (y >= UI::sy(110) && y <= UI::sy(140)) {
                // UNINSTALL
                if (sel.isDir) {
                    FileEntry existingFiles[50];
                    int existingCount = FileSystem::listDirectory(sel.path.c_str(), existingFiles, 50);
                    for (int i = 0; i < existingCount; i++) {
                        if (!existingFiles[i].isDir) {
                            FileSystem::deleteFile(existingFiles[i].path.c_str());
                        }
                    }
                    FileSystem::rmdir(sel.path.c_str());
                } else {
                    FileSystem::deleteFile(sel.path.c_str());
                }
                totalApps = -1; // Refresh list
                appMenuOpen = false;
                appSelected = -1;
                drawApps();
            } else if (y >= UI::sy(150) && y <= UI::sy(180)) {
                // MOVE
                std::string destDir = isSD ? "/local/apps/" : "/sd/apps/";
                FileSystem::mkdir(destDir.c_str()); // Ensure dir exists
                std::string destPath = destDir + sel.name;
                
                tftInstance->fillRoundRect(UI::sx(40), UI::sy(130), UI::sx(160), UI::sy(40), UI::sx(5), TFT_BLACK);
                tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
                tftInstance->setTextDatum(MC_DATUM);
                tftInstance->drawString("Moving...", UI::sx(120), UI::sy(150), UI::font(2));
                
                if (sel.isDir) {
                    if (FileSystem::copyDirectory(sel.path.c_str(), destPath.c_str())) {
                        FileEntry existingFiles[50];
                        int existingCount = FileSystem::listDirectory(sel.path.c_str(), existingFiles, 50);
                        for (int i = 0; i < existingCount; i++) {
                            if (!existingFiles[i].isDir) {
                                FileSystem::deleteFile(existingFiles[i].path.c_str());
                            }
                        }
                        FileSystem::rmdir(sel.path.c_str());
                    }
                } else {
                    if (FileSystem::copyFile(sel.path.c_str(), destPath.c_str())) {
                        FileSystem::deleteFile(sel.path.c_str());
                    }
                }
                
                totalApps = -1; // Refresh list
                appMenuOpen = false;
                appSelected = -1;
                drawApps();
            } else if (y >= UI::sy(190) && y <= UI::sy(220)) {
                // CANCEL
                appMenuOpen = false;
                drawApps();
            }
        }
        return;
    }

    // Default Install Toggle
    if (y >= UI::sy(40) && y <= UI::sy(70)) {
        defaultInstallSD = !defaultInstallSD;
        saveAppInstallPreference();
        drawApps();
        return;
    }

    // Scroll Buttons
    if (x >= UI::sx(200) && y >= UI::sy(80) && y <= UI::sy(110)) {
        if (appScroll > 0) {
            appScroll--;
            drawApps();
        }
        return;
    }
    if (x >= UI::sx(200) && y >= UI::sy(250) && y <= UI::sy(280)) {
        if (appScroll + 6 < totalApps) {
            appScroll++;
            drawApps();
        }
        return;
    }

    // List Selection
    if (y >= UI::sy(80) && y <= UI::sy(280)) {
        int indexClicked = appScroll + ((y - 80) / 35);
        if (indexClicked < totalApps) {
            appSelected = indexClicked;
            appMenuOpen = true;
            drawApps();
        }
        return;
    }

    // Bottom Nav: BACK
    if (y >= UI::sy(285)) {
        if (x > UI::sx(60) && x < UI::sx(180)) {
            totalApps = -1; // Reset state for next visit
            appScroll = 0;
            appSelected = -1;
            appMenuOpen = false;
            currentState = 1; // STATE_SETTINGS
        }
    }
}

// ----------------------------------------------------
// TIME & REGION MENU
// ----------------------------------------------------

static int tzScroll = 0;
static bool tzSelectMode = false;

struct TZEntry {
    const char* label;
    const char* value;
};

static TZEntry tzList[] = {
    {"UTC-12 Baker Is", "UTC12"},
    {"UTC-11 Midway", "UTC11"},
    {"UTC-10 Hawaii", "UTC10"},
    {"UTC-9 Alaska", "UTC9"},
    {"UTC-8 PST", "UTC8"},
    {"UTC-7 MST", "UTC7"},
    {"UTC-6 CST", "UTC6"},
    {"UTC-5 EST", "UTC5"},
    {"UTC-4 AST", "UTC4"},
    {"UTC-3 BRT", "UTC3"},
    {"UTC-2", "UTC2"},
    {"UTC-1 AZOT", "UTC1"},
    {"UTC+0 GMT", "UTC0"},
    {"UTC+1 CET", "UTC-1"},
    {"UTC+2 EET", "UTC-2"},
    {"UTC+3 MSK", "UTC-3"},
    {"UTC+4 GST", "UTC-4"},
    {"UTC+5 PKT", "UTC-5"},
    {"UTC+5:30 IST", "UTC-5:30"},
    {"UTC+6 BST", "UTC-6"},
    {"UTC+7 ICT", "UTC-7"},
    {"UTC+8 CST/AWST", "UTC-8"},
    {"UTC+9 JST", "UTC-9"},
    {"UTC+10 AEST", "UTC-10"},
    {"UTC+11 AEDT", "UTC-11"},
    {"UTC+12 NZST", "UTC-12"}
};
const int tzCount = sizeof(tzList) / sizeof(TZEntry);

void SettingsUI::drawTimeSettings() {
    if (!tftInstance) return;
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    // Header
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_CYAN);
    tftInstance->setTextColor(TFT_CYAN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Time & Region", UI::sx(120), UI::sy(21), UI::font(2));

    if (tzSelectMode) {
        // Draw TZ Selection Menu
        tftInstance->setTextColor(TFT_YELLOW, TFT_BLACK);
        tftInstance->drawString("Select Timezone", UI::sx(120), UI::sy(45), UI::font(2));
        
        int yPos = 60;
        int itemsPerPage = 6;
        tftInstance->setTextDatum(TL_DATUM);
        
        for (int i = 0; i < itemsPerPage; i++) {
            int listIndex = tzScroll + i;
            if (listIndex >= tzCount) break;
            
            if (tzList[listIndex].value == TimeManager::currentTimezone) {
                tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(30), TFT_BLUE);
                tftInstance->setTextColor(TFT_WHITE);
            } else {
                tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(30), TFT_BLACK);
                tftInstance->setTextColor(TFT_WHITE);
            }
            
            tftInstance->drawString(tzList[listIndex].label, UI::sx(15), UI::sy(yPos + 8), UI::font(2));
            yPos += 35;
        }
        
        // Scroll buttons
        if (tzScroll > 0) tftInstance->fillTriangle(UI::sx(220), UI::sy(65), UI::sx(230), UI::sy(80), UI::sx(210), UI::sy(80), TFT_WHITE);
        if (tzScroll + itemsPerPage < tzCount) tftInstance->fillTriangle(UI::sx(220), UI::sy(260), UI::sx(210), UI::sy(245), UI::sx(230), UI::sy(245), TFT_WHITE);
        
    } else {
        // Draw Main Settings
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Current Time:", UI::sx(120), UI::sy(50), UI::font(2));
        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->drawString(TimeManager::getFormattedTime().c_str(), UI::sx(120), UI::sy(75), UI::font(4));
        
        int y = 110;
        
        // NTP Toggle
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(y), UI::sx(200), UI::sy(35), UI::sx(5), TFT_DARKGREY);
        tftInstance->setTextColor(TimeManager::ntpEnabled ? TFT_GREEN : TFT_RED, TFT_DARKGREY);
        tftInstance->drawString(TimeManager::ntpEnabled ? "NTP Sync: ON" : "NTP Sync: OFF", UI::sx(120), UI::sy(y + 17), UI::font(2));
        y += 45;
        
        // Region Button
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(y), UI::sx(200), UI::sy(35), UI::sx(5), TFT_BLUE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLUE);
        std::string r = "Region: " + TimeManager::currentTimezone;
        tftInstance->drawString(r.c_str(), UI::sx(120), UI::sy(y + 17), UI::font(2));
        y += 45;
        
        // Format Button
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(y), UI::sx(200), UI::sy(35), UI::sx(5), TFT_ORANGE);
        tftInstance->setTextColor(TFT_WHITE, TFT_ORANGE);
        tftInstance->drawString(TimeManager::use24hFormat ? "Format: 24h" : "Format: 12h", UI::sx(120), UI::sy(y + 17), UI::font(2));
        y += 45;
        
        // Manual Time Button (Only active if NTP OFF)
        if (!TimeManager::ntpEnabled) {
            tftInstance->fillRoundRect(UI::sx(20), UI::sy(y), UI::sx(200), UI::sy(35), UI::sx(5), TFT_PURPLE);
            tftInstance->setTextColor(TFT_WHITE, TFT_PURPLE);
            tftInstance->drawString("Set Manual Time", UI::sx(120), UI::sy(y + 17), UI::font(2));
        }
    }
    
    // Footer
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));
}

void SettingsUI::handleTimeTouch(uint16_t x, uint16_t y) {
    extern int currentState;
    
    if (tzSelectMode) {
        if (x >= UI::sx(200) && y >= UI::sy(60) && y <= UI::sy(90) && tzScroll > 0) {
            tzScroll--; drawTimeSettings(); return;
        }
        if (x >= UI::sx(200) && y >= UI::sy(230) && y <= UI::sy(260) && tzScroll + 6 < tzCount) {
            tzScroll++; drawTimeSettings(); return;
        }
        
        if (y >= UI::sy(60) && y <= UI::sy(270)) {
            int idx = tzScroll + ((y - 60) / 35);
            if (idx < tzCount) {
                TimeManager::setTimezone(tzList[idx].value);
                tzSelectMode = false;
                drawTimeSettings();
            }
        }
        
        if (y >= UI::sy(285) && x > UI::sx(60) && x < UI::sx(180)) {
            tzSelectMode = false;
            drawTimeSettings();
        }
        return;
    }

    if (x >= UI::sx(20) && x <= UI::sx(220)) {
        if (y >= UI::sy(110) && y <= UI::sy(145)) {
            TimeManager::setNTPEnabled(!TimeManager::ntpEnabled);
            drawTimeSettings();
        } else if (y >= UI::sy(155) && y <= UI::sy(190)) {
            tzSelectMode = true;
            drawTimeSettings();
        } else if (y >= UI::sy(200) && y <= UI::sy(235)) {
            TimeManager::setTimeFormat(!TimeManager::use24hFormat);
            drawTimeSettings();
        } else if (y >= UI::sy(245) && y <= UI::sy(280) && !TimeManager::ntpEnabled) {
            currentState = 10; // STATE_SETTINGS_TIME_MANUAL
        }
    }
    
    if (y >= UI::sy(285) && x > UI::sx(60) && x < UI::sx(180)) {
        currentState = 1; // STATE_SETTINGS
    }
}

// ----------------------------------------------------
// MANUAL TIME MENU
// ----------------------------------------------------

static int mDay = 1, mMonth = 1, mYear = 2026, mHour = 12, mMinute = 0;
static bool loadedManual = false;

void SettingsUI::drawTimeManual() {
    if (!tftInstance) return;
    
    if (!loadedManual) {
        mYear = TimeManager::getYear();
        mMonth = TimeManager::getMonth();
        mDay = TimeManager::getDay();
        
        time_t now; time(&now);
        struct tm tinfo; localtime_r(&now, &tinfo);
        mHour = tinfo.tm_hour;
        mMinute = tinfo.tm_min;
        loadedManual = true;
    }
    
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    // Header
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_PURPLE);
    tftInstance->setTextColor(TFT_PURPLE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Set Time", UI::sx(120), UI::sy(21), UI::font(2));

    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    
    // Helper lambda to draw an up/down section
    auto drawSection = [](int x, int y, int w, std::string val) {
        tftInstance->fillTriangle(UI::sx(x + w/2), UI::sy(y), UI::sx(x + w - 5), UI::sy(y + 15), UI::sx(x + 5), UI::sy(y + 15), TFT_GREEN);
        tftInstance->fillRoundRect(UI::sx(x), UI::sy(y + 20), UI::sx(w), UI::sy(30), UI::sx(4), TFT_DARKGREY);
        tftInstance->drawString(val.c_str(), UI::sx(x + w/2), UI::sy(y + 35), UI::font(2));
        tftInstance->fillTriangle(UI::sx(x + 5), UI::sy(y + 55), UI::sx(x + w - 5), UI::sy(y + 55), UI::sx(x + w/2), UI::sy(y + 70), TFT_RED);
    };

    // Date Line
    drawSection(10, 60, 50, std::to_string(mDay));
    tftInstance->drawString("/", UI::sx(70), UI::sy(95), UI::font(2));
    drawSection(80, 60, 50, std::to_string(mMonth));
    tftInstance->drawString("/", UI::sx(140), UI::sy(95), UI::font(2));
    drawSection(150, 60, 70, std::to_string(mYear));

    // Time Line
    drawSection(40, 160, 60, std::to_string(mHour));
    tftInstance->drawString(":", UI::sx(120), UI::sy(195), UI::font(4));
    char mBuf[8]; snprintf(mBuf, sizeof(mBuf), "%02d", mMinute);
    drawSection(140, 160, 60, std::string(mBuf));
    
    // Save Footer
    tftInstance->fillRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_BLACK, TFT_GREEN);
    tftInstance->drawString("SAVE & BACK", UI::sx(120), UI::sy(300), UI::font(2));
}

void SettingsUI::handleTimeManualTouch(uint16_t x, uint16_t y) {
    extern int currentState;
    
    auto checkClick = [&](int bx, int by, int bw, int &val, int minV, int maxV) {
        if (x >= bx && x <= bx + bw) {
            if (y >= by && y <= by + 20) { val++; if (val > maxV) val = minV; drawTimeManual(); }
            if (y >= by + 50 && y <= by + 75) { val--; if (val < minV) val = maxV; drawTimeManual(); }
        }
    };

    // Date Line
    checkClick(10, 60, 50, mDay, 1, 31);
    checkClick(80, 60, 50, mMonth, 1, 12);
    checkClick(150, 60, 70, mYear, 2000, 2100);
    
    // Time Line
    checkClick(40, 160, 60, mHour, 0, 23);
    checkClick(140, 160, 60, mMinute, 0, 59);

    if (y >= UI::sy(285)) {
        TimeManager::setManualTime(mYear, mMonth, mDay, mHour, mMinute);
        loadedManual = false;
        currentState = 9; // STATE_SETTINGS_TIME
    }
}

// ----------------------------------------------------
// WIFI SCANNER AND CONNECT UI
// ----------------------------------------------------

void SettingsUI::scanAndConnectWiFi() {
    extern int currentState;
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("WiFi Scanner", UI::sx(120), UI::sy(21), UI::font(2));

    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Scanning for networks...", UI::sx(120), UI::sy(160), UI::font(2));

    // Scan via WebManager (bloqueante ~2s, dedup por SSID, ordem por RSSI)
    KryonScanEntry nets[20];
    int n = WebManager::scanNetworks(nets, 20);

    if (n == 0) {
        tftInstance->fillScreen(TFT_BLACK);
        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
        tftInstance->drawString("No networks found.", UI::sx(120), UI::sy(160), UI::font(2));
        delay(2000);
        // Revert WiFi ON request
        FileSystem::writeTextFile("/local/nowifi.txt", "1");
        drawWiFi();
        return;
    }

    int currentPage = 0;
    int networksPerPage = 5;
    int totalPages = (n + networksPerPage - 1) / networksPerPage;
    
    while (true) {
        tftInstance->fillScreen(TFT_BLACK);
        tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
        tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
        tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("Select Network", UI::sx(120), UI::sy(21), UI::font(2));

        int startIdx = currentPage * networksPerPage;
        int endIdx = startIdx + networksPerPage;
        if (endIdx > n) endIdx = n;

        int yPos = 50;
        tftInstance->setTextDatum(TL_DATUM);
        for (int i = startIdx; i < endIdx; i++) {
            // Draw button
            tftInstance->fillRoundRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(40), UI::sx(5), TFT_DARKGREY);
            
            std::string ssid = nets[i].ssid;
            if (ssid.length() > 18) ssid = ssid.substr(0, 15) + "..."; // Truncate long SSIDs

            tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
            tftInstance->drawString(ssid.c_str(), UI::sx(20), UI::sy(yPos + 10), UI::font(2));

            // Draw lock icon or open text
            if (!nets[i].secure) {
                tftInstance->setTextColor(TFT_GREEN, TFT_DARKGREY);
                tftInstance->drawString("OPEN", UI::sx(180), UI::sy(yPos + 10), UI::font(2));
            } else {
                tftInstance->setTextColor(TFT_RED, TFT_DARKGREY);
                tftInstance->drawString("SECURE", UI::sx(170), UI::sy(yPos + 10), UI::font(2));
            }
            
            yPos += 45;
        }

        // Draw pagination or Cancel
        tftInstance->fillRoundRect(UI::sx(10), UI::sy(275), UI::sx(100), UI::sy(35), UI::sx(5), TFT_RED);
        tftInstance->setTextColor(TFT_WHITE, TFT_RED);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("Cancel", UI::sx(60), UI::sy(292), UI::font(2));

        if (totalPages > 1) {
            tftInstance->fillRoundRect(UI::sx(130), UI::sy(275), UI::sx(100), UI::sy(35), UI::sx(5), TFT_BLUE);
            tftInstance->setTextColor(TFT_WHITE, TFT_BLUE);
            tftInstance->drawString("Next Page", UI::sx(180), UI::sy(292), UI::font(2));
        }

        // Touch handling loop for this screen
        uint16_t tx = 0, ty = 0;
        bool touched = false;
        while (!touched) {
            if (tftInstance->getTouch(&tx, &ty)) {
                // Debounce
                while (tftInstance->getTouch(&tx, &ty)) { delay(10); }
                touched = true;
            }
            delay(50);
        }

        // Check if Cancel tapped
        if (ty >= UI::sy(275) && ty <= UI::sy(310) && tx >= UI::sx(10) && tx <= UI::sx(110)) {
            // Revert WiFi ON request
            FileSystem::writeTextFile("/local/nowifi.txt", "1");
            drawWiFi();
            return;
        }

        // Check if Next Page tapped
        if (totalPages > 1 && ty >= UI::sy(275) && ty <= UI::sy(310) && tx >= UI::sx(130) && tx <= UI::sx(230)) {
            currentPage++;
            if (currentPage >= totalPages) currentPage = 0;
            continue; // redraw
        }

        // Check if a network was tapped
        int tappedIndex = -1;
        int checkY = 50;
        for (int i = startIdx; i < endIdx; i++) {
            if (ty >= UI::sy(checkY) && ty <= UI::sy(checkY + 40) && tx >= UI::sx(10) && tx <= UI::sx(230)) {
                tappedIndex = i;
                break;
            }
            checkY += 45;
        }

        if (tappedIndex != -1) {
            std::string selectedSSID = nets[tappedIndex].ssid;
            selectedSSID = kstr::trim(selectedSSID);
            std::string password = "";
            bool connected = false;

            while (!connected) {
                if (nets[tappedIndex].secure) {
                    // Ask for password
                    std::string promptMsg = "Password for " + selectedSSID;
                    password = MyKeyboard::getString("", promptMsg, 64);
                    password = kstr::trim(password);
                    if (password.length() == 0) {
                        // Canceled typing password
                        break; 
                    }
                }

                tftInstance->fillScreen(TFT_BLACK);
                tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
                tftInstance->setTextDatum(MC_DATUM);
                tftInstance->drawString("Testing Connection...", UI::sx(120), UI::sy(160), UI::font(2));

                // Conecta bloqueando ate 15s e grava /wifi.txt (SD ou
                // LittleFS) somente se conectar — mesma sequencia do fluxo
                // antigo de testar antes de salvar
                if (WebManager::connect(selectedSSID, password)) {
                    connected = true;
                } else {
                    if (!nets[tappedIndex].secure) {
                        tftInstance->fillScreen(TFT_BLACK);
                        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
                        tftInstance->drawString("Failed to Connect!", UI::sx(120), UI::sy(160), UI::font(2));
                        delay(2000);
                        break;
                    } else {
                        tftInstance->fillScreen(TFT_BLACK);
                        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
                        tftInstance->drawString("Wrong Password!", UI::sx(120), UI::sy(140), UI::font(2));
                        tftInstance->drawString("Please try again.", UI::sx(120), UI::sy(160), UI::font(2));
                        delay(2000);
                        // Loop continues and asks for password again
                    }
                }
            }

            if (!connected) {
                continue; // Go back to scanning list
            }

            // Credenciais ja gravadas em /wifi.txt pelo WebManager::connect()

            tftInstance->fillScreen(TFT_BLACK);
            tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
            tftInstance->setTextDatum(MC_DATUM);
            tftInstance->drawString("Connected!", UI::sx(120), UI::sy(160), UI::font(2));
            delay(1000);
            WebManager::enable();
            LauncherUI::requestRescan();
            currentState = 0; // volta ao launcher
            return;
        }
    }
}
// ----------------------------------------------------
// SYSTEM UPDATER
// ----------------------------------------------------

static bool updaterIsFromBoot = false;

int SettingsUI::otaProgressLast = -1;

bool SettingsUI::checkUpdateSilent() {
    return OtaManager::checkForUpdates();
}

void SettingsUI::otaProgressCb(int percent) {
    if (percent == otaProgressLast || !tftInstance) return;
    otaProgressLast = percent;
    int progressWidth = map(percent, 0, 100, 0, 176);
    tftInstance->fillRect(UI::sx(32), UI::sy(162), UI::sx(progressWidth), UI::sy(16), TFT_GREEN);
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString((std::to_string(percent) + "%").c_str(), UI::sx(120), UI::sy(150), UI::font(1));
}

void SettingsUI::drawUpdater(bool isBootCheck) {
    updaterIsFromBoot = isBootCheck;
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);

    if (!WebManager::isWifiConnected()) {
        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("No WiFi Connection!", UI::sx(120), UI::sy(140), UI::font(2));
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Please turn on WiFi", UI::sx(120), UI::sy(160), UI::font(2));
        tftInstance->drawString("first in Settings.", UI::sx(120), UI::sy(180), UI::font(2));

        tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString(isBootCheck ? "CLOSE" : "BACK", UI::sx(120), UI::sy(300), UI::font(2));
        return;
    }

    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Checking for updates...", UI::sx(120), UI::sy(160), UI::font(2));

    checkUpdateSilent();

    if (isBootCheck && (!OtaManager::info.available || OtaManager::info.fetchFailed)) {
        extern int currentState;
        currentState = 0; // STATE_LAUNCHER
        return;
    }

    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);

    if (OtaManager::info.fetchFailed) {
        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("Failed to check", UI::sx(120), UI::sy(140), UI::font(2));
        tftInstance->drawString("for updates!", UI::sx(120), UI::sy(160), UI::font(2));
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Check your connection", UI::sx(120), UI::sy(190), UI::font(2));
    } else if (!OtaManager::info.available) {
        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("System is up to date!", UI::sx(120), UI::sy(160), UI::font(2));
    } else {
        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->setTextDatum(TC_DATUM);
        tftInstance->drawString(OtaManager::info.type.c_str(), UI::sx(120), UI::sy(15), UI::font(2));

        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString((std::string(KRYONOS_VERSION) + " -> " + OtaManager::info.version).c_str(), UI::sx(120), UI::sy(35), UI::font(2));

        int y = 60;
        tftInstance->setTextColor(TFT_YELLOW, TFT_BLACK);
        tftInstance->setTextDatum(TL_DATUM);
        tftInstance->drawString("What's New:", UI::sx(15), UI::sy(y), UI::font(2)); y += 16;

        tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        int start = 0;
        while (start < (int)OtaManager::info.changelog.length() && y < 180) {
            int nl = kstr::indexOf(OtaManager::info.changelog, '\n', start);
            std::string line;
            if (nl == -1) { line = OtaManager::info.changelog.substr(start); start = OtaManager::info.changelog.length(); }
            else { line = OtaManager::info.changelog.substr(start, nl - start); start = nl + 1; }

            int lStart = 0;
            while(lStart < (int)line.length() && y < 180) {
                int lEnd = lStart + 30;
                if(lEnd >= (int)line.length()) lEnd = line.length();
                else { size_t sp = line.rfind(' ', (size_t)lEnd); if(sp != std::string::npos && (int)sp > lStart) lEnd = (int)sp; }
                tftInstance->drawString(line.substr(lStart, lEnd - lStart).c_str(), UI::sx(15), UI::sy(y), UI::font(2));
                y += 16;
                lStart = lEnd;
                if(lStart < (int)line.length() && line[lStart]==' ') lStart++;
            }
        }

        y += 5;
        tftInstance->setTextColor(TFT_YELLOW, TFT_BLACK);
        tftInstance->drawString("How to Install:", UI::sx(15), UI::sy(y), UI::font(2)); y += 16;

        tftInstance->setTextColor(TFT_CYAN, TFT_BLACK);
        start = 0;
        while (start < (int)OtaManager::info.guide.length() && y < 275) {
            int nl = kstr::indexOf(OtaManager::info.guide, '\n', start);
            std::string line;
            if (nl == -1) { line = OtaManager::info.guide.substr(start); start = OtaManager::info.guide.length(); }
            else { line = OtaManager::info.guide.substr(start, nl - start); start = nl + 1; }

            int lStart = 0;
            while(lStart < (int)line.length() && y < 275) {
                int lEnd = lStart + 30;
                if(lEnd >= (int)line.length()) lEnd = line.length();
                else { size_t sp = line.rfind(' ', (size_t)lEnd); if(sp != std::string::npos && (int)sp > lStart) lEnd = (int)sp; }
                tftInstance->drawString(line.substr(lStart, lEnd - lStart).c_str(), UI::sx(15), UI::sy(y), UI::font(2));
                y += 16;
                lStart = lEnd;
                if(lStart < (int)line.length() && line[lStart]==' ') lStart++;
            }
        }
    }

    // Rodape: INSTALL + BACK quando o canal publica firmware; so BACK caso
    // contrario (update.json sem firmware_url -> guia manual legado)
    if (OtaManager::info.available && OtaManager::info.hasFirmware) {
        tftInstance->fillRoundRect(UI::sx(10), UI::sy(285), UI::sx(105), UI::sy(30), UI::sx(5), TFT_GREEN);
        tftInstance->setTextColor(TFT_BLACK, TFT_GREEN);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("INSTALL", UI::sx(62), UI::sy(300), UI::font(2));

        tftInstance->drawRoundRect(UI::sx(125), UI::sy(285), UI::sx(105), UI::sy(30), UI::sx(5), TFT_WHITE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString(isBootCheck ? "CLOSE" : "BACK", UI::sx(177), UI::sy(300), UI::font(2));
    } else {
        tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString(isBootCheck ? "CLOSE" : "BACK", UI::sx(120), UI::sy(300), UI::font(2));
    }
}

void SettingsUI::handleUpdaterTouch(uint16_t x, uint16_t y) {
    extern int currentState;

    if (OtaManager::info.available && OtaManager::info.hasFirmware &&
        y >= UI::sy(285) && x >= UI::sx(10) && x <= UI::sx(115)) {
        runOtaInstall();
        return;
    }

    bool fullBackButton = !(OtaManager::info.available && OtaManager::info.hasFirmware);
    bool hitBack = fullBackButton
        ? (y >= UI::sy(285) && x > UI::sx(60) && x < UI::sx(180))
        : (y >= UI::sy(285) && x >= UI::sx(125) && x <= UI::sx(230));

    if (hitBack) {
        if (updaterIsFromBoot) {
            currentState = 0; // STATE_LAUNCHER
        } else {
            currentState = 1; // STATE_SETTINGS
        }
    }
}

void SettingsUI::runOtaInstall() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);

    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Updating System", UI::sx(120), UI::sy(60), UI::font(2));

    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString((std::string(KRYONOS_VERSION) + " -> " + OtaManager::info.version).c_str(), UI::sx(120), UI::sy(85), UI::font(2));

    tftInstance->setTextColor(TFT_RED, TFT_BLACK);
    tftInstance->drawString("Do not power off!", UI::sx(120), UI::sy(115), UI::font(2));

    tftInstance->drawRect(UI::sx(30), UI::sy(160), UI::sx(180), UI::sy(20), TFT_WHITE);

    otaProgressLast = -1;
    bool ok = OtaManager::performUpdate(OtaManager::info.firmwareUrl, SettingsUI::otaProgressCb);

    if (ok) {
        tftInstance->fillScreen(TFT_BLACK);
        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("Update Installed!", UI::sx(120), UI::sy(140), UI::font(2));
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Rebooting...", UI::sx(120), UI::sy(165), UI::font(2));
        delay(2000);
        ESP.restart();
        return;
    }

    // Falha: o slot atual permanece intacto — basta voltar
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_RED, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Update Failed!", UI::sx(120), UI::sy(100), UI::font(2));

    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->setTextDatum(TL_DATUM);
    std::string err = OtaManager::lastError;
    int y = 130;
    int lStart = 0;
    while (lStart < (int)err.length() && y < 270) {
        int lEnd = lStart + 28;
        if (lEnd >= (int)err.length()) lEnd = err.length();
        else { size_t sp = err.rfind(' ', (size_t)lEnd); if(sp != std::string::npos && (int)sp > lStart) lEnd = (int)sp; }
        tftInstance->drawString(err.substr(lStart, lEnd - lStart).c_str(), UI::sx(15), UI::sy(y), UI::font(2));
        y += 16;
        lStart = lEnd;
        if (lStart < (int)err.length() && err[lStart] == ' ') lStart++;
    }

    tftInstance->fillRoundRect(UI::sx(10), UI::sy(285), UI::sx(220), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_BLACK, TFT_WHITE);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));

    // Espera tocar em BACK para retornar a tela do updater
    uint16_t tx = 0, ty = 0;
    while (true) {
        if (tftInstance->getTouch(&tx, &ty)) {
            if (ty >= UI::sy(275)) break;
            while (tftInstance->getTouch(&tx, &ty)) { delay(10); }
        }
        delay(30);
    }
    drawUpdater(updaterIsFromBoot);
}

// ----------------------------------------------------
// SECURITY (PIN LOCK) — porte do PasswordScreen do satisfaction-hub,
// na convencao KryonOS: md5 em arquivo, numpad modal, sessao de 60s
// ----------------------------------------------------

static const char* SETTINGS_PIN_FILE = "/local/settings_pin.txt";
unsigned long SettingsUI::unlockedUntilMs = 0;

std::string SettingsUI::md5String(const std::string& input) {
    // MD5 via ROM da Espressif (mesmo digest; mbedtls 3.x privatizou md5.h)
    md5_context_t ctx;
    esp_rom_md5_init(&ctx);
    esp_rom_md5_update(&ctx, input.c_str(), (uint32_t)input.length());
    uint8_t hash[16];
    esp_rom_md5_final(hash, &ctx);

    char out[33];
    for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", hash[i]);
    out[32] = '\0';
    return std::string(out);
}

bool SettingsUI::isPinSet() {
    return FileSystem::exists(SETTINGS_PIN_FILE);
}

bool SettingsUI::verifyPin(const std::string& pin) {
    std::string stored = FileSystem::readTextFile(SETTINGS_PIN_FILE);
    stored = kstr::trim(stored);
    return stored.length() == 32 && stored == md5String(pin);
}

// Numpad 3x4 modal. Retorna o PIN digitado ou "" se cancelado.
std::string SettingsUI::pinPrompt(const char* title, int maxLength) {
    std::string pin = "";
    const char* keys[4][3] = {
        { "1", "2", "3" },
        { "4", "5", "6" },
        { "7", "8", "9" },
        { "CLR", "0", "OK" },
    };

    while (true) {
        tftInstance->fillScreen(TFT_BLACK);
        tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
        tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString(title, UI::sx(120), UI::sy(21), UI::font(2));

        // Pontos do PIN digitado
        std::string dots = "";
        for (unsigned int i = 0; i < pin.length(); i++) dots += "* ";
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString(dots.c_str(), UI::sx(120), UI::sy(68), UI::font(2));

        // Teclado 3x4: colunas x = 15/90/165 (w 60), linhas y = 100/146/192/238 (h 38)
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 3; c++) {
                int bx = 15 + c * 75;
                int by = 100 + r * 46;
                uint32_t col = TFT_DARKGREY;
                if (r == 3 && c == 2) col = TFT_GREEN;
                if (r == 3 && c == 0) col = TFT_MAROON;
                tftInstance->fillRoundRect(UI::sx(bx), UI::sy(by), UI::sx(60), UI::sy(38), UI::sx(5), col);
                tftInstance->setTextColor(TFT_WHITE, col);
                tftInstance->drawString(keys[r][c], UI::sx(bx + 30), UI::sy(by + 19), UI::font(2));
            }
        }

        // CANCEL no rodape
        tftInstance->fillRoundRect(UI::sx(70), UI::sy(280), UI::sx(100), UI::sy(30), UI::sx(5), TFT_RED);
        tftInstance->setTextColor(TFT_WHITE, TFT_RED);
        tftInstance->drawString("CANCEL", UI::sx(120), UI::sy(295), UI::font(2));

        uint16_t tx = 0, ty = 0;
        while (true) {
            if (tftInstance->getTouch(&tx, &ty)) {
                // CANCEL
                if (ty >= UI::sy(275) && tx >= UI::sx(60) && tx <= UI::sx(180)) {
                    while (tftInstance->getTouch(&tx, &ty)) { delay(10); }
                    return "";
                }
                // teclas do grid
                int col = -1, row = -1;
                for (int c = 0; c < 3; c++) {
                    int bx = 15 + c * 75;
                    if (tx >= UI::sx(bx) && tx <= UI::sx(bx + 60)) { col = c; break; }
                }
                for (int r = 0; r < 4; r++) {
                    int by = 100 + r * 46;
                    if (ty >= UI::sy(by) && ty <= UI::sy(by + 38)) { row = r; break; }
                }
                if (col >= 0 && row >= 0) {
                    while (tftInstance->getTouch(&tx, &ty)) { delay(10); }
                    const char* k = keys[row][col];
                    if (k[0] >= '0' && k[0] <= '9' && (int)pin.length() < maxLength) {
                        pin += k[0];
                        break;  // redesenha com o novo digito
                    }
                    if (strcmp(k, "CLR") == 0) {
                        pin = "";
                        break;
                    }
                    if (strcmp(k, "OK") == 0 && pin.length() >= 4) {
                        return pin;
                    }
                }
                while (tftInstance->getTouch(&tx, &ty)) { delay(10); }
            }
            delay(20);
        }
    }
}

// Chamado no draw() do menu: true = pode entrar em Settings
bool SettingsUI::unlockGate() {
    if (!isPinSet()) return true;
    if (millis() < unlockedUntilMs) return true;

    while (true) {
        std::string pin = pinPrompt("Enter Settings PIN", 8);
        if (pin.length() == 0) return false;  // cancelado
        if (verifyPin(pin)) {
            unlockedUntilMs = millis() + 60000UL;  // sessao de 60s
            return true;
        }
        tftInstance->fillScreen(TFT_BLACK);
        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("Wrong PIN!", UI::sx(120), UI::sy(150), UI::font(2));
        tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tftInstance->drawString("Try again or cancel.", UI::sx(120), UI::sy(175), UI::font(2));
        delay(1200);
    }
}

void SettingsUI::setPinFlow() {
    std::string p1 = pinPrompt("New PIN (4-8 digits)", 8);
    if (p1.length() == 0) return;
    std::string p2 = pinPrompt("Confirm PIN", 8);
    if (p2.length() == 0) return;

    if (p1 != p2 || p1.length() < 4) {
        tftInstance->fillScreen(TFT_BLACK);
        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("PINs don't match!", UI::sx(120), UI::sy(150), UI::font(2));
        delay(1500);
        return;
    }
    FileSystem::writeTextFile(SETTINGS_PIN_FILE, md5String(p1).c_str());

    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("PIN Saved!", UI::sx(120), UI::sy(150), UI::font(2));
    delay(1200);
}

void SettingsUI::drawSecurity() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Security", UI::sx(120), UI::sy(21), UI::font(2));

    bool pin = isPinSet();
    if (pin) {
        tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
        tftInstance->drawString("PIN lock: ENABLED", UI::sx(120), UI::sy(70), UI::font(2));

        tftInstance->fillRoundRect(UI::sx(30), UI::sy(110), UI::sx(180), UI::sy(35), UI::sx(4), TFT_BLUE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLUE);
        tftInstance->drawString("Change PIN", UI::sx(120), UI::sy(127), UI::font(2));

        tftInstance->fillRoundRect(UI::sx(30), UI::sy(170), UI::sx(180), UI::sy(35), UI::sx(4), TFT_RED);
        tftInstance->setTextColor(TFT_WHITE, TFT_RED);
        tftInstance->drawString("Remove PIN", UI::sx(120), UI::sy(187), UI::font(2));
    } else {
        tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tftInstance->drawString("PIN lock: disabled", UI::sx(120), UI::sy(70), UI::font(2));
        tftInstance->drawString("Protect your Settings", UI::sx(120), UI::sy(92), UI::font(2));

        tftInstance->fillRoundRect(UI::sx(30), UI::sy(130), UI::sx(180), UI::sy(35), UI::sx(4), TFT_GREEN);
        tftInstance->setTextColor(TFT_BLACK, TFT_GREEN);
        tftInstance->drawString("Set PIN", UI::sx(120), UI::sy(147), UI::font(2));
    }

    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));
}

void SettingsUI::handleSecurityTouch(uint16_t x, uint16_t y) {
    extern int currentState;

    if (y >= UI::sy(285) && x > UI::sx(60) && x < UI::sx(180)) {
        currentState = 1;  // STATE_SETTINGS
        return;
    }

    if (!isPinSet()) {
        // Set PIN
        if (y >= UI::sy(130) && y <= UI::sy(165) && x >= UI::sx(30) && x <= UI::sx(210)) {
            setPinFlow();
            unlockedUntilMs = 0;  // pede o PIN novo no proximo acesso
        }
    } else {
        // Change PIN
        if (y >= UI::sy(110) && y <= UI::sy(145) && x >= UI::sx(30) && x <= UI::sx(210)) {
            std::string cur = pinPrompt("Current PIN", 8);
            if (cur.length() > 0 && verifyPin(cur)) {
                setPinFlow();
            } else if (cur.length() > 0) {
                tftInstance->fillScreen(TFT_BLACK);
                tftInstance->setTextColor(TFT_RED, TFT_BLACK);
                tftInstance->setTextDatum(MC_DATUM);
                tftInstance->drawString("Wrong PIN!", UI::sx(120), UI::sy(150), UI::font(2));
                delay(1200);
            }
        }
        // Remove PIN
        if (y >= UI::sy(170) && y <= UI::sy(205) && x >= UI::sx(30) && x <= UI::sx(210)) {
            std::string cur = pinPrompt("Current PIN", 8);
            if (cur.length() > 0 && verifyPin(cur)) {
                FileSystem::deleteFile(SETTINGS_PIN_FILE);
                tftInstance->fillScreen(TFT_BLACK);
                tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
                tftInstance->setTextDatum(MC_DATUM);
                tftInstance->drawString("PIN Removed!", UI::sx(120), UI::sy(150), UI::font(2));
                delay(1200);
            } else if (cur.length() > 0) {
                tftInstance->fillScreen(TFT_BLACK);
                tftInstance->setTextColor(TFT_RED, TFT_BLACK);
                tftInstance->setTextDatum(MC_DATUM);
                tftInstance->drawString("Wrong PIN!", UI::sx(120), UI::sy(150), UI::font(2));
                delay(1200);
            }
        }
    }

    drawSecurity();
}

// ----------------------------------------------------
// DISPLAY (BRILHO) — porte do BrightnessScreen do satisfaction-hub
// ----------------------------------------------------

// Trilho do slider no design 240x320: x 20..220, y 148, altura 16
void SettingsUI::drawBrightnessBar(int level) {
    tftInstance->fillRect(UI::sx(10), UI::sy(135), UI::sx(220), UI::sy(60), TFT_BLACK);

    tftInstance->fillRoundRect(UI::sx(20), UI::sy(148), UI::sx(200), UI::sy(16), UI::sx(8), TFT_DARKGREY);
    int fillW = (200 * level) / 100;
    if (fillW > 4) {
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(148), UI::sx(fillW), UI::sy(16), UI::sx(8), TFT_GREEN);
    }

    // Marcador do slider
    int knobX = UI::sx(20 + fillW);
    tftInstance->fillCircle(knobX, UI::sy(156), UI::sx(8), TFT_WHITE);

    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString((std::to_string(level) + "%").c_str(), UI::sx(120), UI::sy(185), UI::font(2));
}

void SettingsUI::runBrightnessSlider(int x0) {
    uint16_t tx = (uint16_t)constrain(x0, UI::sx(20), UI::sx(220));
    uint16_t ty = 0;
    while (true) {
        int level = ((int)tx - UI::sx(20)) * 100 / UI::sx(200);
        level = constrain(level, 5, 100);
        Backlight::set(level, false);  // aplica sem gravar a cada pixel
        drawBrightnessBar(level);

        // Espera o proximo sample ou o release
        unsigned long t0 = millis();
        while (millis() - t0 < 30) {
            if (tftInstance->getTouch(&tx, &ty)) {
                tx = (uint16_t)constrain(tx, UI::sx(20), UI::sx(220));
                break;
            }
            delay(5);
        }
        bool stillDown = tftInstance->getTouch(&tx, &ty);
        if (!stillDown) {
            // Release: grava o nivel final
            Backlight::set(Backlight::get());
            return;
        }
        tx = (uint16_t)constrain(tx, UI::sx(20), UI::sx(220));
    }
}

void SettingsUI::drawDisplaySettings() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Display", UI::sx(120), UI::sy(21), UI::font(2));

    if (!Backlight::isSupported()) {
        tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tftInstance->drawString("Brightness control is not", UI::sx(120), UI::sy(140), UI::font(2));
        tftInstance->drawString("available on this board.", UI::sx(120), UI::sy(160), UI::font(2));
    } else {
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("Brightness", UI::sx(120), UI::sy(70), UI::font(2));
        tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tftInstance->drawString("drag the slider", UI::sx(120), UI::sy(105), UI::font(1));
        drawBrightnessBar(Backlight::get());
    }

    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));
}

void SettingsUI::handleDisplayTouch(uint16_t x, uint16_t y) {
    extern int currentState;

    if (y >= UI::sy(285) && x > UI::sx(60) && x < UI::sx(180)) {
        currentState = 1;  // STATE_SETTINGS
        return;
    }

    if (Backlight::isSupported() && y >= UI::sy(135) && y <= UI::sy(200)) {
        runBrightnessSlider(x);
        drawDisplaySettings();
    }
}
