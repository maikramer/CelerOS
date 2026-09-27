#include "SettingsUI.h"
#include "../Display/Layout.h"
#include "../Display/Theme.h"
#include "../Display/Icon.h"
#include <SD.h>
#include <LittleFS.h>
#include "../File System/FileSystem.h"
#include "../Kernel/TimeManager.h"
#include "../Keyboard/MyKeyboard.h"
#include <WiFi.h>
#include "../WebManager/WebManager.h"
#include "../Launcher/LauncherUI.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "../OTA/OtaManager.h"
#include "../WebManager/CaptivePortal.h"

KryonDisplay *SettingsUI::tftInstance = nullptr;
bool showResetDialog = false;

void SettingsUI::init(KryonDisplay *tft) {
    tftInstance = tft;
}

String formatBytes(uint64_t bytes) {
    if (bytes < 1024) return String((uint32_t)bytes) + " B";
    else if (bytes < (1024 * 1024)) return String((uint32_t)(bytes / 1024)) + " KB";
    else if (bytes < (1024 * 1024 * 1024)) return String((uint32_t)(bytes / (1024 * 1024))) + " MB";
    else return String((uint32_t)(bytes / (1024 * 1024 * 1024))) + " GB";
}

// ----------------------------------------------------
// MAIN SETTINGS MENU
// ----------------------------------------------------

void SettingsUI::draw() {
    if (!tftInstance) return;

    tftInstance->fillScreen(THEME_BG);

    // Header no estilo do launcher
    int hdr = UI::sy(56);
    tftInstance->fillRect(0, 0, UI::W, hdr, THEME_CARD);
    tftInstance->fillRect(0, hdr - UI::sy(3), UI::W, UI::sy(3), THEME_ACCENT);
    tftInstance->setTextColor(THEME_TEXT, THEME_CARD);
    tftInstance->setTextDatum(ML_DATUM);
    tftInstance->drawString("Settings", UI::sx(14), hdr / 2, UI::font(4));

    // Itens do menu (calibrador so existe em placas resistivas)
    const char* icons[6] = { "wifi_on", "settings", "app", "time", "about", "update" };
    const char* labels[6] = { "WiFi", "Calibrator", "Apps", "Time & Region", "About", "Updates" };
    int count = 6;
#ifdef KRYONOS_TOUCH_CAPACITIVE
    icons[1] = "settings";  // placeholder oculto abaixo
    count = 5;
    const int mapIdx[5] = { 0, 2, 3, 4, 5 };  // sem o calibrador
#else
    const int mapIdx[6] = { 0, 1, 2, 3, 4, 5 };
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
    int n = 6;
#ifdef KRYONOS_TOUCH_CAPACITIVE
    n = 5;
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
                int dests[5] = { 6, 8, 9, 7, 12 };      // WiFi, Apps, Time, About, Updates
#else
                int dests[6] = { 6, 4, 8, 9, 7, 12 };   // WiFi, Calibrator, Apps, Time, About, Updates
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
        
        bool hasWifiCredentials = FileSystem::exists("/sd/wifi.txt") || FileSystem::exists("/local/wifi.txt");
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
            bool hasWifiCredentials = FileSystem::exists("/sd/wifi.txt") || FileSystem::exists("/local/wifi.txt");
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
            bool hasWifiCredentials = FileSystem::exists("/sd/wifi.txt") || FileSystem::exists("/local/wifi.txt");

            // Captive portal: metade direita (com credenciais) ou linha inteira
            bool hitPortal = hasWifiCredentials
                ? (x >= UI::sx(122) && x <= UI::sx(230))
                : (x >= UI::sx(40) && x <= UI::sx(200));
            if (hitPortal) {
                WebManager::stopWebServer();  // libera a porta 80 para o portal
                CaptivePortal::runBlocking(tftInstance);
                WebManager::enable();         // restaura STA + servidor conforme o resultado
                drawWiFi();
                return;
            }

            if (hasWifiCredentials && x >= UI::sx(10) && x <= UI::sx(118)) {
                if (FileSystem::exists("/sd/wifi.txt")) FileSystem::deleteFile("/sd/wifi.txt");
                if (FileSystem::exists("/local/wifi.txt")) FileSystem::deleteFile("/local/wifi.txt");
                WebManager::disable();

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
    uint64_t fsTotal = LittleFS.totalBytes();
    uint64_t fsUsed = LittleFS.usedBytes();
    uint64_t fsFree = fsTotal - fsUsed;

    uint64_t sdTotal = SD.totalBytes();
    uint64_t sdUsed = SD.usedBytes();
    uint64_t sdFree = sdTotal - sdUsed;

    tftInstance->setTextColor(TFT_CYAN, TFT_BLACK);
    tftInstance->setTextDatum(TL_DATUM);
    tftInstance->drawString("Internal Memory (LittleFS)", UI::sx(15), UI::sy(40), UI::font(2));
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Total: " + formatBytes(fsTotal), UI::sx(25), UI::sy(60), UI::font(2));
    tftInstance->drawString("Used:  " + formatBytes(fsUsed), UI::sx(25), UI::sy(80), UI::font(2));
    tftInstance->drawString("Free:  " + formatBytes(fsFree), UI::sx(25), UI::sy(100), UI::font(2));

    tftInstance->setTextColor(TFT_ORANGE, TFT_BLACK);
    tftInstance->drawString("External Memory (SD Card)", UI::sx(15), UI::sy(125), UI::font(2));
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    if (sdTotal > 0) {
        tftInstance->drawString("Total: " + formatBytes(sdTotal), UI::sx(25), UI::sy(145), UI::font(2));
        tftInstance->drawString("Used:  " + formatBytes(sdUsed), UI::sx(25), UI::sy(165), UI::font(2));
        tftInstance->drawString("Free:  " + formatBytes(sdFree), UI::sx(25), UI::sy(185), UI::font(2));
    } else {
        tftInstance->setTextColor(TFT_RED, TFT_BLACK);
        tftInstance->drawString("SD Card not mounted!", UI::sx(25), UI::sy(145), UI::font(2));
    }
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB", 15, 205, 2);
    
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->drawString(String("KryonOS ") + KRYONOS_VERSION, UI::sx(15), UI::sy(222), UI::font(2));

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
        
        uint16_t color = TFT_WHITE;
        if (listIndex == appSelected) {
            tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(30), TFT_BLUE);
        } else {
            tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(30), TFT_BLACK);
        }
        
        tftInstance->setTextColor(color);
        // Show Name
        String displayName = entry.name;
        if (entry.isDir) {
            String appJsonPath = entry.path;
            if (!appJsonPath.endsWith("/")) appJsonPath += "/";
            appJsonPath += "app.json";
            if (FileSystem::exists(appJsonPath.c_str())) {
                String jsonContent = FileSystem::readTextFile(appJsonPath.c_str());
                String parsedName = FileSystem::parseJsonValue(jsonContent, "name");
                if (parsedName.length() > 0) displayName = parsedName;
            }
        }
        tftInstance->drawString(displayName, UI::sx(15), UI::sy(yPos + 8), UI::font(2));
        
        // Show Drive Marker
        String drive = entry.path.startsWith("/sd") ? "[SD]" : "[LFS]";
        tftInstance->setTextColor(TFT_YELLOW);
        tftInstance->drawString(drive, UI::sx(190), UI::sy(yPos + 8), UI::font(2));
        
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
        bool isSD = sel.path.startsWith("/sd");
        
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
            bool isSD = sel.path.startsWith("/sd");
            
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
                String destDir = isSD ? "/local/apps/" : "/sd/apps/";
                FileSystem::mkdir(destDir.c_str()); // Ensure dir exists
                String destPath = destDir + sel.name;
                
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
            
            if (String(tzList[listIndex].value) == TimeManager::currentTimezone) {
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
        tftInstance->drawString(TimeManager::getFormattedTime(), UI::sx(120), UI::sy(75), UI::font(4));
        
        int y = 110;
        
        // NTP Toggle
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(y), UI::sx(200), UI::sy(35), UI::sx(5), TFT_DARKGREY);
        tftInstance->setTextColor(TimeManager::ntpEnabled ? TFT_GREEN : TFT_RED, TFT_DARKGREY);
        tftInstance->drawString(TimeManager::ntpEnabled ? "NTP Sync: ON" : "NTP Sync: OFF", UI::sx(120), UI::sy(y + 17), UI::font(2));
        y += 45;
        
        // Region Button
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(y), UI::sx(200), UI::sy(35), UI::sx(5), TFT_BLUE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLUE);
        String r = "Region: " + TimeManager::currentTimezone;
        tftInstance->drawString(r, UI::sx(120), UI::sy(y + 17), UI::font(2));
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
    auto drawSection = [](int x, int y, int w, String val) {
        tftInstance->fillTriangle(UI::sx(x + w/2), UI::sy(y), UI::sx(x + w - 5), UI::sy(y + 15), UI::sx(x + 5), UI::sy(y + 15), TFT_GREEN);
        tftInstance->fillRoundRect(UI::sx(x), UI::sy(y + 20), UI::sx(w), UI::sy(30), UI::sx(4), TFT_DARKGREY);
        tftInstance->drawString(val, UI::sx(x + w/2), UI::sy(y + 35), UI::font(2));
        tftInstance->fillTriangle(UI::sx(x + 5), UI::sy(y + 55), UI::sx(x + w - 5), UI::sy(y + 55), UI::sx(x + w/2), UI::sy(y + 70), TFT_RED);
    };

    // Date Line
    drawSection(10, 60, 50, String(mDay));
    tftInstance->drawString("/", UI::sx(70), UI::sy(95), UI::font(2));
    drawSection(80, 60, 50, String(mMonth));
    tftInstance->drawString("/", UI::sx(140), UI::sy(95), UI::font(2));
    drawSection(150, 60, 70, String(mYear));
    
    // Time Line
    drawSection(40, 160, 60, String(mHour));
    tftInstance->drawString(":", UI::sx(120), UI::sy(195), UI::font(4));
    char mBuf[8]; snprintf(mBuf, sizeof(mBuf), "%02d", mMinute);
    drawSection(140, 160, 60, String(mBuf));
    
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

    // Initialize WiFi in Station Mode and scan
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();

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
            
            String ssid = WiFi.SSID(i);
            if (ssid.length() > 18) ssid = ssid.substring(0, 15) + "..."; // Truncate long SSIDs
            
            tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
            tftInstance->drawString(ssid, UI::sx(20), UI::sy(yPos + 10), UI::font(2));

            // Draw lock icon or open text
            if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
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
            String selectedSSID = WiFi.SSID(tappedIndex);
            selectedSSID.trim();
            String password = "";
            bool connected = false;

            while (!connected) {
                if (WiFi.encryptionType(tappedIndex) != WIFI_AUTH_OPEN) {
                    // Ask for password
                    String promptMsg = "Password for " + selectedSSID;
                    password = MyKeyboard::getString("", promptMsg, 64);
                    password.trim();
                    if (password.length() == 0) {
                        // Canceled typing password
                        break; 
                    }
                }

                tftInstance->fillScreen(TFT_BLACK);
                tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
                tftInstance->setTextDatum(MC_DATUM);
                tftInstance->drawString("Testing Connection...", UI::sx(120), UI::sy(160), UI::font(2));

                WiFi.disconnect(); // Reset state
                delay(100);
                WiFi.mode(WIFI_STA);
                
                if (password.length() > 0) {
                    WiFi.begin(selectedSSID.c_str(), password.c_str());
                } else {
                    WiFi.begin(selectedSSID.c_str());
                }
                
                int attempts = 0;
                while (WiFi.status() != WL_CONNECTED && attempts < 30) { // Wait up to 15 seconds
                    delay(500);
                    attempts++;
                }

                if (WiFi.status() == WL_CONNECTED) {
                    connected = true;
                } else {
                    if (WiFi.encryptionType(tappedIndex) == WIFI_AUTH_OPEN) {
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

            // Save and Reboot
            if (FileSystem::exists("/sd/")) {
                FileSystem::writeTextFile("/sd/wifi.txt", (selectedSSID + "\n" + password).c_str());
            } else {
                FileSystem::writeTextFile("/local/wifi.txt", (selectedSSID + "\n" + password).c_str());
            }

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
    tftInstance->drawString(String(percent) + "%", UI::sx(120), UI::sy(150), UI::font(1));
}

void SettingsUI::drawUpdater(bool isBootCheck) {
    updaterIsFromBoot = isBootCheck;
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);

    if (WiFi.status() != WL_CONNECTED) {
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
        tftInstance->drawString(String(KRYONOS_VERSION) + " -> " + OtaManager::info.version, UI::sx(120), UI::sy(35), UI::font(2));

        int y = 60;
        tftInstance->setTextColor(TFT_YELLOW, TFT_BLACK);
        tftInstance->setTextDatum(TL_DATUM);
        tftInstance->drawString("What's New:", UI::sx(15), UI::sy(y), UI::font(2)); y += 16;

        tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        int start = 0;
        while (start < (int)OtaManager::info.changelog.length() && y < 180) {
            int nl = OtaManager::info.changelog.indexOf('\n', start);
            String line;
            if (nl == -1) { line = OtaManager::info.changelog.substring(start); start = OtaManager::info.changelog.length(); }
            else { line = OtaManager::info.changelog.substring(start, nl); start = nl + 1; }

            int lStart = 0;
            while(lStart < (int)line.length() && y < 180) {
                int lEnd = lStart + 30;
                if(lEnd >= (int)line.length()) lEnd = line.length();
                else { int space = line.lastIndexOf(' ', lEnd); if(space > lStart) lEnd = space; }
                tftInstance->drawString(line.substring(lStart, lEnd).c_str(), UI::sx(15), UI::sy(y), UI::font(2));
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
            int nl = OtaManager::info.guide.indexOf('\n', start);
            String line;
            if (nl == -1) { line = OtaManager::info.guide.substring(start); start = OtaManager::info.guide.length(); }
            else { line = OtaManager::info.guide.substring(start, nl); start = nl + 1; }

            int lStart = 0;
            while(lStart < (int)line.length() && y < 275) {
                int lEnd = lStart + 30;
                if(lEnd >= (int)line.length()) lEnd = line.length();
                else { int space = line.lastIndexOf(' ', lEnd); if(space > lStart) lEnd = space; }
                tftInstance->drawString(line.substring(lStart, lEnd).c_str(), UI::sx(15), UI::sy(y), UI::font(2));
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
    tftInstance->drawString(String(KRYONOS_VERSION) + " -> " + OtaManager::info.version, UI::sx(120), UI::sy(85), UI::font(2));

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
    String err = OtaManager::lastError;
    int y = 130;
    int lStart = 0;
    while (lStart < (int)err.length() && y < 270) {
        int lEnd = lStart + 28;
        if (lEnd >= (int)err.length()) lEnd = err.length();
        else { int space = err.lastIndexOf(' ', lEnd); if (space > lStart) lEnd = space; }
        tftInstance->drawString(err.substring(lStart, lEnd).c_str(), UI::sx(15), UI::sy(y), UI::font(2));
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
