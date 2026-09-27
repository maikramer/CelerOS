#include "InstallerUI.h"
#include "../Display/Layout.h"
#include "../FileSystem/FileSystem.h"
#include "../Kernel/Core/HarixKernel.h"
#include "LauncherUI.h"
#include "../Utils/StrUtils.h"

// External state variable
extern int currentState;

KryonDisplay *InstallerUI::tftInstance = nullptr;
FileEntry InstallerUI::files[200];
int InstallerUI::fileCount = 0;
std::string InstallerUI::currentPath = "/";
std::string InstallerUI::autoInstallPath = "";
int InstallerUI::scrollOffset = 0;
int InstallerUI::selectedIndex = 0;
std::string InstallerUI::selectedFile = "";
bool InstallerUI::showActionDialog = false;
std::string InstallerUI::displayNames[200];
bool InstallerUI::isAppPackage[200];

static int installState = 0; // 0=None, 1=OverwritePrompt, 2=Result, 3=AppInfo, 4=Installing
static bool installResultOk = false;
static bool installSyntaxError = false;
static bool installApiError = false;
static bool installNoMetadata = false;
std::string syntaxErrorMessage = "";
static AppMetadata currentAppMeta;
static bool isUpdatingApp = false;

// Static pointer for progress callback
static KryonDisplay* progressTft = nullptr;

// ASCII-only lowercase, equivalent of Arduino String::toLowerCase()
static std::string toLowerAscii(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    return s;
}

static bool isVersionGreater(const std::string& newVer, const std::string& oldVer) {
    int newParts[3] = {0, 0, 0};
    int oldParts[3] = {0, 0, 0};
    
    auto parseVer = [](const std::string& v, int* parts) {
        int partIdx = 0;
        int startIdx = 0;
        while (partIdx < 3 && startIdx < (int)v.length()) {
            int dotIdx = kstr::indexOf(v, '.', startIdx);
            if (dotIdx == -1) {
                parts[partIdx] = (int)kstr::toInt(v.substr(startIdx));
                break;
            }
            parts[partIdx] = (int)kstr::toInt(v.substr(startIdx, dotIdx - startIdx));
            startIdx = dotIdx + 1;
            partIdx++;
        }
    };
    
    parseVer(newVer, newParts);
    parseVer(oldVer, oldParts);
    
    if (newParts[0] > oldParts[0]) return true;
    if (newParts[0] < oldParts[0]) return false;
    
    if (newParts[1] > oldParts[1]) return true;
    if (newParts[1] < oldParts[1]) return false;
    
    if (newParts[2] > oldParts[2]) return true;
    return false;
}

void InstallerUI::init(KryonDisplay *tft) {
    tftInstance = tft;
    progressTft = tft;
}

// ============================================================
// App Metadata Parsing
// ============================================================

AppMetadata InstallerUI::parseAppJson(const std::string& folderPath) {
    AppMetadata meta;
    meta.valid = false;
    meta.api = 0;
    meta.folderPath = folderPath;
    
    std::string jsonPath = folderPath;
    if (!kstr::endsWith(jsonPath, "/")) jsonPath += "/";
    jsonPath += "app.json";
    
    if (!FileSystem::exists(jsonPath.c_str())) {
        return meta;
    }
    
    std::string content = FileSystem::readTextFile(jsonPath.c_str());
    if (content.length() == 0) {
        return meta;
    }
    
    meta.name = FileSystem::parseJsonValue(content, "name");
    meta.packageName = FileSystem::parseJsonValue(content, "packageName");
    meta.version = FileSystem::parseJsonValue(content, "version");
    meta.author = FileSystem::parseJsonValue(content, "author");
    meta.type = FileSystem::parseJsonValue(content, "type");
    meta.category = FileSystem::parseJsonValue(content, "category");
    meta.description = FileSystem::parseJsonValue(content, "description");
    meta.changelog = FileSystem::parseJsonValue(content, "changelog");
    
    std::string apiStr = FileSystem::parseJsonValue(content, "api");
    meta.api = (int)kstr::toInt(apiStr);
    
    // Valid if we at least got a name
    if (meta.name.length() > 0) {
        meta.valid = true;
    }
    
    return meta;
}

// ============================================================
// Scanning
// ============================================================

static bool needsRescan = true;
static std::string lastScannedPath = "";

void InstallerUI::scanSD() {
    if (!needsRescan && currentPath == lastScannedPath) return;
    
    needsRescan = false;
    lastScannedPath = currentPath;
    if (currentPath == "/") {
        fileCount = 3;
        files[0].name = "SD Card";
        files[0].path = "/sd/";
        files[0].isDir = true;
        files[1].name = "Internal Storage";
        files[1].path = "/local/";
        files[1].isDir = true;
        files[2].name = "Help / Guide";
        files[2].path = "/help/";
        files[2].isDir = true;
        
        for (int i = 0; i < 3; i++) {
            displayNames[i] = files[i].name;
            isAppPackage[i] = false;
        }
    } else {
        fileCount = FileSystem::listDirectory(currentPath.c_str(), files, 200);
        
        if (fileCount > 0) {
            tftInstance->fillScreen(TFT_BLACK);
            tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
            tftInstance->setTextDatum(MC_DATUM);
            tftInstance->drawString("Loading App Details...", UI::sx(120), UI::sy(140), UI::font(2));
            tftInstance->drawRect(UI::sx(30), UI::sy(160), UI::sx(180), UI::sy(20), TFT_WHITE);
        }
        
        // Check each directory for app.json
        for (int i = 0; i < fileCount; i++) {
            if (fileCount > 0) {
                int progressWidth = map(i, 0, fileCount, 0, 176);
                tftInstance->fillRect(UI::sx(32), UI::sy(162), UI::sx(progressWidth), UI::sy(16), TFT_GREEN);
            }
            
            isAppPackage[i] = false;
            displayNames[i] = files[i].name;
            
            if (files[i].isDir) {
                std::string appJsonPath = files[i].path;
                if (!kstr::endsWith(appJsonPath, "/")) appJsonPath += "/";
                appJsonPath += "app.json";
                
                if (FileSystem::exists(appJsonPath.c_str())) {
                    // It's an app package! Read the name and type from app.json
                    std::string jsonContent = FileSystem::readTextFile(appJsonPath.c_str());
                    std::string appName = FileSystem::parseJsonValue(jsonContent, "name");
                    std::string appType = FileSystem::parseJsonValue(jsonContent, "type");
                    if (appType.length() == 0) appType = "App";
                    if (appName.length() > 0) {
                        displayNames[i] = "[" + appType + "] " + appName;
                        isAppPackage[i] = true;
                    }
                }
            }
        }
        
        // Sort everything by Type (App -> Dir -> File) then alphabetically
        for (int i = 0; i < fileCount - 1; i++) {
            for (int j = i + 1; j < fileCount; j++) {
                int scoreI = isAppPackage[i] ? 0 : (files[i].isDir ? 1 : 2);
                int scoreJ = isAppPackage[j] ? 0 : (files[j].isDir ? 1 : 2);
                
                bool doSwap = false;
                if (scoreI > scoreJ) {
                    doSwap = true;
                } else if (scoreI == scoreJ) {
                    std::string nameI = toLowerAscii(files[i].name);
                    std::string nameJ = toLowerAscii(files[j].name);
                    if (nameI.compare(nameJ) > 0) {
                        doSwap = true;
                    }
                }
                
                if (doSwap) {
                    // Swap files
                    FileEntry tempFile = files[i];
                    files[i] = files[j];
                    files[j] = tempFile;
                    
                    // Swap displayNames
                    std::string tempName = displayNames[i];
                    displayNames[i] = displayNames[j];
                    displayNames[j] = tempName;
                    
                    // Swap isAppPackage
                    bool tempApp = isAppPackage[i];
                    isAppPackage[i] = isAppPackage[j];
                    isAppPackage[j] = tempApp;
                }
            }
        }
    }
    
    // Reset selection state if we hit bounds
    bool hasUp = (currentPath != "/");
    if (selectedIndex >= fileCount + (hasUp ? 1 : 0)) {
        selectedIndex = 0;
        scrollOffset = 0;
    }
}

// ============================================================
// Drawing
// ============================================================

void InstallerUI::draw() {
    if (!tftInstance) return;
    
    if (autoInstallPath.length() > 0) {
        selectedFile = autoInstallPath;
        if (!kstr::endsWith(selectedFile, "/")) selectedFile += "/";
        currentAppMeta = parseAppJson(selectedFile);
        
        bool defaultSD = FileSystem::exists("/local/config_install_sd.txt");
        if (defaultSD && !FileSystem::exists("/sd/")) defaultSD = false;
        std::string destBase = defaultSD ? "/sd/apps/" : "/local/apps/";
        std::string destFolder = destBase + currentAppMeta.packageName + "/";
        
        isUpdatingApp = false;
        installSyntaxError = false;
        
        if (FileSystem::exists(destFolder.c_str())) {
            std::string installedJsonPath = destFolder + "app.json";
            if (FileSystem::exists(installedJsonPath.c_str())) {
                std::string installedJsonContent = FileSystem::readTextFile(installedJsonPath.c_str());
                std::string installedAuthor = FileSystem::parseJsonValue(installedJsonContent, "author");
                std::string installedVersion = FileSystem::parseJsonValue(installedJsonContent, "version");
                
                if (installedAuthor != currentAppMeta.author) {
                    installSyntaxError = true;
                    syntaxErrorMessage = "Author conflict!\nInstalled: " + installedAuthor + "\nNew: " + currentAppMeta.author;
                    installResultOk = false;
                    installState = 2;
                } else if (isVersionGreater(currentAppMeta.version, installedVersion)) {
                    isUpdatingApp = true;
                }
            }
        }
        
        if (!installSyntaxError) {
            installState = 3;
        }
        
        showActionDialog = true;
        autoInstallPath = "";
    }
    
    if (showActionDialog) {
        drawActionDialog();
        return;
    }

    scanSD();
    
    if (currentPath == "/help/") {
        drawHelp();
    } else {
        drawFileList();
    }
}

void InstallerUI::drawFileList() {
    // Draw the main border
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    // Header Bar
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    std::string headerText = "App Installer";
    if (kstr::startsWith(currentPath, "/sd")) {
        headerText = "App Installer   /sdcard";
    } else if (kstr::startsWith(currentPath, "/local")) {
        headerText = "App Installer   /internal-storage";
    }
    
    tftInstance->drawString(headerText.c_str(), UI::sx(120), UI::sy(21), UI::font(2));
    
    // Clear only the list area
    tftInstance->fillRect(UI::sx(10), UI::sy(45), UI::sx(220), UI::sy(230), TFT_BLACK);

    int yPos = 45;
    int itemsPerPage = UI::ITEMS_PER_PAGE;
    bool hasUp = (currentPath != "/");
    int totalItems = fileCount + (hasUp ? 1 : 0);

    if (totalItems == 0) {
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->setTextDatum(TC_DATUM);
        tftInstance->drawString("Folder is empty", UI::sx(120), UI::sy(100), UI::font(2));
    } else {
        for (int i = 0; i < itemsPerPage; i++) {
            int listIndex = scrollOffset + i;
            if (listIndex >= totalItems) break;
            
            std::string displayName = "";
            bool isDirectory = false;
            
            if (hasUp && listIndex == 0) {
                displayName = "[..] UP";
                isDirectory = true;
            } else {
                int fileIdx = listIndex - (hasUp ? 1 : 0);
                isDirectory = files[fileIdx].isDir;
                
                if (isAppPackage[fileIdx]) {
                    // Show as app package with app name
                    displayName = displayNames[fileIdx];
                } else if (isDirectory && currentPath != "/") {
                    displayName = "[D] " + displayNames[fileIdx];
                } else {
                    displayName = displayNames[fileIdx];
                }
            }
            
            if (listIndex == selectedIndex) {
                // Highlighted Item
                tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(25), TFT_WHITE);
                tftInstance->setTextColor(TFT_BLACK, TFT_WHITE);
                tftInstance->setTextDatum(ML_DATUM);
                tftInstance->drawString(("> " + displayName).c_str(), UI::sx(15), UI::sy(yPos + 12), UI::font(2));
            } else {
                // Normal Item
                uint16_t textColor = TFT_WHITE;
                if (hasUp && listIndex != 0) {
                    int fileIdx = listIndex - (hasUp ? 1 : 0);
                    if (isAppPackage[fileIdx]) textColor = TFT_GREEN;
                }
                tftInstance->setTextColor(textColor, TFT_BLACK);
                tftInstance->setTextDatum(ML_DATUM);
                tftInstance->drawString(("  " + displayName).c_str(), UI::sx(15), UI::sy(yPos + 12), UI::font(2));
            }
            yPos += 30;
        }
    }

    // Touch Footer
    // (We intentionally do NOT clear the footer to prevent blinking on scroll)
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    
    tftInstance->drawString("ESC", UI::sx(30), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(60), UI::sy(300), UI::font(2));
    tftInstance->drawString("UP", UI::sx(90), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(120), UI::sy(300), UI::font(2));
    tftInstance->drawString("SEL", UI::sx(150), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(180), UI::sy(300), UI::font(2));
    tftInstance->drawString("DN", UI::sx(210), UI::sy(300), UI::font(2));
}

// ============================================================
// Action Dialog Drawing
// ============================================================

void InstallerUI::drawActionDialog() {
    tftInstance->fillScreen(TFT_BLACK);
    
    if (installState == 1) { // Overwrite Prompt
        tftInstance->fillRoundRect(UI::sx(10), UI::sy(80), UI::sx(220), UI::sy(160), UI::sx(8), TFT_DARKGREY);
        tftInstance->setTextColor(TFT_YELLOW, TFT_DARKGREY);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("App Exists!", UI::sx(120), UI::sy(110), UI::font(4));
        tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
        tftInstance->drawString("Overwrite?", UI::sx(120), UI::sy(140), UI::font(2));
        
        tftInstance->fillRoundRect(UI::sx(30), UI::sy(180), UI::sx(70), UI::sy(30), UI::sx(4), TFT_GREEN);
        tftInstance->setTextColor(TFT_BLACK, TFT_GREEN);
        tftInstance->drawString("Yes", UI::sx(65), UI::sy(195), UI::font(2));
        
        tftInstance->fillRoundRect(UI::sx(140), UI::sy(180), UI::sx(70), UI::sy(30), UI::sx(4), TFT_RED);
        tftInstance->setTextColor(TFT_WHITE, TFT_RED);
        tftInstance->drawString("No", UI::sx(175), UI::sy(195), UI::font(2));
        return;
    } else if (installState == 2) { // Result
        tftInstance->fillRoundRect(UI::sx(10), UI::sy(60), UI::sx(220), UI::sy(200), UI::sx(8), TFT_DARKGREY);
        tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
        tftInstance->setTextDatum(MC_DATUM);
        if (installResultOk) {
            tftInstance->setTextColor(TFT_GREEN, TFT_DARKGREY);
            tftInstance->drawString("Installed!", UI::sx(120), UI::sy(100), UI::font(4));
            tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
            tftInstance->drawString(currentAppMeta.name.c_str(), UI::sx(120), UI::sy(130), UI::font(2));
            tftInstance->drawString(("v" + currentAppMeta.version).c_str(), UI::sx(120), UI::sy(150), UI::font(2));
        } else {
            tftInstance->setTextColor(TFT_RED, TFT_DARKGREY);
            if (installNoMetadata) {
                tftInstance->drawString("No Metadata!", UI::sx(120), UI::sy(90), UI::font(4));
                tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
                tftInstance->drawString("Folder missing app.json", UI::sx(120), UI::sy(125), UI::font(2));
                tftInstance->drawString("Cannot install.", UI::sx(120), UI::sy(145), UI::font(2));
            } else if (installApiError) {
                tftInstance->drawString("API Error!", UI::sx(120), UI::sy(90), UI::font(4));
                tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
                tftInstance->drawString(("App requires API: " + std::to_string(currentAppMeta.api)).c_str(), UI::sx(120), UI::sy(125), UI::font(2));
                tftInstance->drawString(("OS has API: " + std::to_string(KRYONOS_API_LEVEL)).c_str(), UI::sx(120), UI::sy(145), UI::font(2));
                tftInstance->drawString("Update KryonOS!", UI::sx(120), UI::sy(170), UI::font(2));
            } else if (installSyntaxError) {
                tftInstance->drawString("Syntax Error!", UI::sx(120), UI::sy(90), UI::font(4));
                
                tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
                tftInstance->setTextDatum(TC_DATUM);
                int startIdx = 0;
                int yPos = 115;
                int lineCount = 0;
                while (startIdx < (int)syntaxErrorMessage.length() && lineCount < 4) {
                    int nextNewline = kstr::indexOf(syntaxErrorMessage, '\n', startIdx);
                    if (nextNewline == -1) nextNewline = syntaxErrorMessage.length();
                    std::string line = syntaxErrorMessage.substr(startIdx, nextNewline - startIdx);
                    if (line.length() > 30) line = line.substr(0, 27) + "...";
                    tftInstance->drawString(line.c_str(), UI::sx(120), UI::sy(yPos), UI::font(1));
                    yPos += 10;
                    startIdx = nextNewline + 1;
                    lineCount++;
                }
                tftInstance->setTextDatum(MC_DATUM);
            } else {
                tftInstance->drawString("Failed!", UI::sx(120), UI::sy(120), UI::font(4));
            }
        }
        
        tftInstance->fillRoundRect(UI::sx(85), UI::sy(220), UI::sx(70), UI::sy(30), UI::sx(4), TFT_BLUE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLUE);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString("OK", UI::sx(120), UI::sy(235), UI::font(2));
        return;
    } else if (installState == 3) { // App Info Dialog (before install)
        tftInstance->fillRoundRect(UI::sx(10), UI::sy(30), UI::sx(220), UI::sy(240), UI::sx(8), TFT_DARKGREY);
        tftInstance->setTextColor(TFT_GREEN, TFT_DARKGREY);
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->drawString(currentAppMeta.name.c_str(), UI::sx(120), UI::sy(55), UI::font(4));
        
        tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
        tftInstance->setTextDatum(TL_DATUM);
        int y = 80;
        tftInstance->drawString(("Version: " + currentAppMeta.version).c_str(), UI::sx(25), UI::sy(y), UI::font(2)); y += 16;
        tftInstance->drawString(("Author:  " + currentAppMeta.author).c_str(), UI::sx(25), UI::sy(y), UI::font(2)); y += 16;
        tftInstance->drawString(("Type:    " + currentAppMeta.type).c_str(), UI::sx(25), UI::sy(y), UI::font(2)); y += 16;
        tftInstance->drawString(("Category: " + currentAppMeta.category).c_str(), UI::sx(25), UI::sy(y), UI::font(2)); y += 20;
        
        // Changelog or Description
        tftInstance->setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
        std::string desc = "";
        
        if (isUpdatingApp && currentAppMeta.changelog.length() > 0) {
            tftInstance->setTextColor(TFT_YELLOW, TFT_DARKGREY);
            tftInstance->drawString("What's New:", UI::sx(25), UI::sy(y), UI::font(2)); y += 16;
            tftInstance->setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
            desc = currentAppMeta.changelog;
        } else {
            desc = currentAppMeta.description;
        }
        
        if (desc.length() > 0) {
            // Simple line splitting every ~28 chars
            int startIdx = 0;
            int maxLines = 3;
            int lineCount = 0;
            while (startIdx < (int)desc.length() && lineCount < maxLines) {
                int endIdx = startIdx + 28;
                if (endIdx >= (int)desc.length()) endIdx = desc.length();
                else {
                    // Try to break at a space
                    int spaceIdx = kstr::lastIndexOf(desc.substr(0, endIdx + 1), ' ');
                    if (spaceIdx > startIdx) endIdx = spaceIdx;
                }
                tftInstance->drawString(desc.substr(startIdx, endIdx - startIdx).c_str(), UI::sx(25), UI::sy(y), UI::font(2));
                y += 16;
                startIdx = endIdx;
                if (startIdx < (int)desc.length() && desc[startIdx] == ' ') startIdx++;
                lineCount++;
            }
        }

        // Install and Cancel buttons
        tftInstance->setTextDatum(MC_DATUM);
        tftInstance->fillRoundRect(UI::sx(25), UI::sy(230), UI::sx(80), UI::sy(30), UI::sx(4), TFT_GREEN);
        tftInstance->setTextColor(TFT_BLACK, TFT_GREEN);
        tftInstance->drawString(isUpdatingApp ? "Update" : "Install", UI::sx(65), UI::sy(245), UI::font(2));
        
        tftInstance->fillRoundRect(UI::sx(135), UI::sy(230), UI::sx(80), UI::sy(30), UI::sx(4), TFT_RED);
        tftInstance->setTextColor(TFT_WHITE, TFT_RED);
        tftInstance->drawString("Cancel", UI::sx(175), UI::sy(245), UI::font(2));
        return;
    }

    // Default Action Dialog (for regular files - non-app folders)
    tftInstance->fillRoundRect(UI::sx(10), UI::sy(40), UI::sx(220), UI::sy(160), UI::sx(8), TFT_DARKGREY);
    tftInstance->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tftInstance->setTextDatum(MC_DATUM);
    
    std::string filename = selectedFile.substr(kstr::lastIndexOf(selectedFile, '/') + 1);
    tftInstance->drawString(filename.c_str(), UI::sx(120), UI::sy(60), UI::font(2));
    
    bool isJS = kstr::endsWith(filename, ".js");

    // Run Button (only show if it's a JS file)
    if (isJS) {
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(120), UI::sx(60), UI::sy(30), UI::sx(4), TFT_GREEN);
        tftInstance->setTextColor(TFT_BLACK, TFT_GREEN);
        tftInstance->drawString("Run", UI::sx(50), UI::sy(135), UI::font(2));
    }

    // Install Button
    tftInstance->fillRoundRect(UI::sx(90), UI::sy(120), UI::sx(60), UI::sy(30), UI::sx(4), TFT_BLUE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLUE);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Install", UI::sx(120), UI::sy(135), UI::font(2));
    
    // Cancel Button
    tftInstance->fillRoundRect(UI::sx(160), UI::sy(120), UI::sx(60), UI::sy(30), UI::sx(4), TFT_RED);
    tftInstance->setTextColor(TFT_WHITE, TFT_RED);
    tftInstance->drawString("Cancel", UI::sx(190), UI::sy(135), UI::font(2));
}

// ============================================================
// Progress Animation
// ============================================================

static void installProgressCallback(int current, int total) {
    if (!progressTft) return;
    
    int barWidth = 180;
    int barX = 30;
    int barY = 160;
    int barH = 20;
    
    int fillWidth = (current * barWidth) / total;
    
    // Draw progress bar outline (only first time)
    if (current == 1) {
        progressTft->fillRoundRect(UI::sx(10), UI::sy(60), UI::sx(220), UI::sy(200), UI::sx(8), TFT_DARKGREY);
        progressTft->setTextColor(TFT_GREEN, TFT_DARKGREY);
        progressTft->setTextDatum(MC_DATUM);
        progressTft->drawString("Installing...", UI::sx(120), UI::sy(100), UI::font(4));
        progressTft->drawRoundRect(UI::sx(barX - 2), UI::sy(barY - 2), UI::sx(barWidth + 4), UI::sy(barH + 4), UI::sx(3), TFT_WHITE);
    }
    
    // Fill progress bar
    progressTft->fillRect(UI::sx(barX), UI::sy(barY), UI::sx(fillWidth), UI::sy(barH), TFT_GREEN);
    
    // Draw percentage text
    int pct = (current * 100) / total;
    progressTft->fillRect(UI::sx(90), UI::sy(190), UI::sx(60), UI::sy(20), TFT_DARKGREY);
    progressTft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    progressTft->setTextDatum(MC_DATUM);
    progressTft->drawString((std::to_string(pct) + "%").c_str(), UI::sx(120), UI::sy(200), UI::font(2));
    
    // Draw file count
    progressTft->fillRect(UI::sx(60), UI::sy(210), UI::sx(120), UI::sy(20), TFT_DARKGREY);
    progressTft->drawString((std::to_string(current) + " / " + std::to_string(total) + " files").c_str(), UI::sx(120), UI::sy(220), UI::font(2));
}

void InstallerUI::drawInstallProgress(int current, int total) {
    installProgressCallback(current, total);
}

// ============================================================
// Install Logic
// ============================================================

void InstallerUI::performInstall(const std::string& srcFolder, const std::string& appName, bool overwrite) {
    bool defaultSD = FileSystem::exists("/local/config_install_sd.txt");
    if (defaultSD && !FileSystem::exists("/sd/")) defaultSD = false;
    
    std::string destBase = defaultSD ? "/sd/apps/" : "/local/apps/";
    std::string destFolder = destBase + appName + "/";
    
    if (overwrite) {
        // Delete existing app folder files first
        FileEntry existingFiles[50];
        int existingCount = FileSystem::listDirectory(destFolder.c_str(), existingFiles, 50);
        for (int i = 0; i < existingCount; i++) {
            if (!existingFiles[i].isDir) {
                FileSystem::deleteFile(existingFiles[i].path.c_str());
            }
        }
    }
    
    // Ensure apps directory exists
    FileSystem::mkdir(destBase.c_str());
    
    // Show installing screen
    tftInstance->fillScreen(TFT_BLACK);
    
    // Copy entire folder with progress callback
    installResultOk = FileSystem::copyDirectory(srcFolder.c_str(), destFolder.c_str(), installProgressCallback);
    
    // Cleanup AppStore temporary download
    if (kstr::indexOf(srcFolder, "tmp_download") != -1) {
        FileSystem::deleteFile((srcFolder + "app.json").c_str());
        FileSystem::deleteFile((srcFolder + "main.js").c_str());
        FileSystem::rmdir(srcFolder.c_str());
    }
    
    needsRescan = true; // Refresh list after install
    LauncherUI::requestRescan(); // Tell KryonOS Home to refresh its cache
    
    delay(300); // Brief pause so user sees 100%
    
    installState = 2; // Show result
    drawActionDialog();
}

// ============================================================
// Help Screen
// ============================================================

void InstallerUI::drawHelp() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    // Header Bar
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Installer Help", UI::sx(120), UI::sy(21), UI::font(2));
    
    // Help Text
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(TL_DATUM);
    int y = 45;
    
    tftInstance->drawString("How to Install Apps:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->drawString("1. Put app folder on SD.", UI::sx(10), UI::sy(y), UI::font(2)); y += 14;
    tftInstance->drawString("2. Folder needs app.json", UI::sx(10), UI::sy(y), UI::font(2)); y += 14;
    tftInstance->drawString("   and main.js inside.", UI::sx(10), UI::sy(y), UI::font(2)); y += 14;
    tftInstance->drawString("3. Tap [APP] to install.", UI::sx(10), UI::sy(y), UI::font(2)); y += 14;
    tftInstance->drawString("4. App appears in Home.", UI::sx(10), UI::sy(y), UI::font(2)); y += 20;
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("How to Update Apps:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->drawString("1. Copy updated folder.", UI::sx(10), UI::sy(y), UI::font(2)); y += 14;
    tftInstance->drawString("2. Install and overwrite.", UI::sx(10), UI::sy(y), UI::font(2));
    
    // Back Button Footer
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));
}

// ============================================================
// Touch Handling
// ============================================================

void InstallerUI::handleTouch(uint16_t x, uint16_t y) {
    if (currentPath == "/help/") {
        if (y >= UI::sy(285)) { // BACK button
            currentPath = "/";
            draw();
        }
        return;
    }

    if (showActionDialog) {
        std::string filename = selectedFile.substr(kstr::lastIndexOf(selectedFile, '/') + 1);
        bool isJS = kstr::endsWith(filename, ".js");

        if (installState == 1) { // Overwrite Prompt
            if (y >= UI::sy(180) && y <= UI::sy(210)) {
                if (x >= UI::sx(30) && x <= UI::sx(100)) { // Yes - overwrite
                    installSyntaxError = false;
                    installApiError = false;
                    installNoMetadata = false;
                    performInstall(currentAppMeta.folderPath, currentAppMeta.packageName, true);
                } else if (x >= UI::sx(140) && x <= UI::sx(210)) { // No
                    installState = 0;
                    showActionDialog = false;
                    tftInstance->fillScreen(TFT_BLACK);
                    if (kstr::indexOf(selectedFile, "tmp_download") != -1) {
                        FileSystem::deleteFile((selectedFile + "app.json").c_str());
                        FileSystem::deleteFile((selectedFile + "main.js").c_str());
                        FileSystem::rmdir(selectedFile.c_str());
                        extern int currentState;
                        currentState = 13;
                    } else {
                        drawFileList();
                    }
                }
            }
            return;
        } else if (installState == 2) { // Result
            if (x >= UI::sx(85) && x <= UI::sx(155) && y >= UI::sy(220) && y <= UI::sy(250)) { // OK
                installState = 0;
                showActionDialog = false;
                tftInstance->fillScreen(TFT_BLACK);
                if (kstr::indexOf(selectedFile, "tmp_download") != -1) {
                    extern int currentState;
                    currentState = 13;
                } else {
                    drawFileList();
                }
            }
            return;
        } else if (installState == 3) { // App Info dialog
            if (y >= UI::sy(230) && y <= UI::sy(260)) {
                if (x >= UI::sx(25) && x <= UI::sx(105)) { // Install clicked
                    // Check if app already exists
                    bool defaultSD = FileSystem::exists("/local/config_install_sd.txt");
                    if (defaultSD && !FileSystem::exists("/sd/")) defaultSD = false;
                    std::string destBase = defaultSD ? "/sd/apps/" : "/local/apps/";
                    std::string destFolder = destBase + currentAppMeta.packageName + "/";
                    
                    if (isUpdatingApp) {
                        installSyntaxError = false;
                        installApiError = false;
                        installNoMetadata = false;
                        performInstall(currentAppMeta.folderPath, currentAppMeta.packageName, true);
                    } else if (FileSystem::exists(destFolder.c_str())) {
                        installState = 1; // Ask overwrite
                        drawActionDialog();
                    } else {
                        performInstall(currentAppMeta.folderPath, currentAppMeta.packageName, false);
                    }
                } else if (x >= UI::sx(135) && x <= UI::sx(215)) { // Cancel clicked
                    installState = 0;
                    showActionDialog = false;
                    tftInstance->fillScreen(TFT_BLACK);
                    if (kstr::indexOf(selectedFile, "tmp_download") != -1) {
                        FileSystem::deleteFile((selectedFile + "app.json").c_str());
                        FileSystem::deleteFile((selectedFile + "main.js").c_str());
                        FileSystem::rmdir(selectedFile.c_str());
                        extern int currentState;
                        currentState = 13; // Return to App Store instead of staying in Installer
                    } else {
                        drawFileList();
                    }
                }
            }
            return;
        }

        // Default Action Dialog Touches (for regular files)
        // Run clicked (only if JS)
        if (isJS && x >= UI::sx(20) && x <= UI::sx(80) && y >= UI::sy(120) && y <= UI::sy(150)) {
            Serial.println(("Running from SD: " + selectedFile).c_str());
            
            extern int currentState;
            currentState = 2; // STATE_RUN_APP
            showActionDialog = false;
            
            tftInstance->fillScreen(TFT_BLACK);
            tftInstance->setTextDatum(TL_DATUM);
            
            HarixKernel::runFile(selectedFile.c_str());
            
            tftInstance->fillRoundRect(UI::sx(200), UI::sy(0), UI::sx(40), UI::sy(30), UI::sx(5), TFT_RED);
            tftInstance->setTextColor(TFT_WHITE, TFT_RED);
            tftInstance->setTextDatum(MC_DATUM);
            tftInstance->drawString("X", UI::sx(220), UI::sy(15), UI::font(2));
        }
        // Install clicked (legacy single-file install)
        else if (x >= UI::sx(90) && x <= UI::sx(150) && y >= UI::sy(120) && y <= UI::sy(150)) {
            bool defaultSD = FileSystem::exists("/local/config_install_sd.txt");
            if (defaultSD && !FileSystem::exists("/sd/")) defaultSD = false;
            std::string dest = defaultSD ? "/sd/apps/" + filename : "/local/apps/" + filename;
            std::string otherDest = defaultSD ? "/local/apps/" + filename : "/sd/apps/" + filename;
            
            if (FileSystem::exists(dest.c_str()) || FileSystem::exists(otherDest.c_str())) {
                installState = 1; // Overwrite prompt
                drawActionDialog();
            } else {
                installSyntaxError = false;
                installResultOk = true;
                if (isJS) {
                    std::string content = FileSystem::readTextFile(selectedFile.c_str());
                    extern std::string syntaxErrorMessage;
                    syntaxErrorMessage = HarixKernel::checkSyntax(content.c_str());
                    if (syntaxErrorMessage.length() > 0) {
                        installResultOk = false;
                        installSyntaxError = true;
                    }
                }
                if (installResultOk) {
                    if (defaultSD) FileSystem::mkdir("/sd/apps/");
                    installResultOk = FileSystem::copyFile(selectedFile.c_str(), dest.c_str());
                }
                installState = 2; // Result
                drawActionDialog();
            }
        }
        // Cancel clicked
        else if (x >= UI::sx(160) && x <= UI::sx(220) && y >= UI::sy(120) && y <= UI::sy(150)) {
            installState = 0;
            showActionDialog = false;
            tftInstance->fillScreen(TFT_BLACK);
            drawFileList();
        }
        return;
    }

    bool hasUp = (currentPath != "/");
    int totalItems = fileCount + (hasUp ? 1 : 0);

    // Helper lambda-like function to handle item selection
    auto selectItem = [&](int fileIdx) {
        if (files[fileIdx].isDir) {
            if (isAppPackage[fileIdx]) {
                // This is an app package - show app info dialog
                currentAppMeta = parseAppJson(files[fileIdx].path);
                
                installSyntaxError = false;
                installApiError = false;
                installNoMetadata = false;
                
                if (!currentAppMeta.valid) {
                    // No valid metadata
                    installNoMetadata = true;
                    installResultOk = false;
                    installState = 2;
                    showActionDialog = true;
                    drawActionDialog();
                    return;
                }
                
                // Check API level
                if (currentAppMeta.api > KRYONOS_API_LEVEL) {
                    installApiError = true;
                    installResultOk = false;
                    installState = 2;
                    showActionDialog = true;
                    drawActionDialog();
                    return;
                }
                
                // Check syntax of main.js
                std::string mainJsPath = currentAppMeta.folderPath;
                if (!kstr::endsWith(mainJsPath, "/")) mainJsPath += "/";
                mainJsPath += "main.js";
                
                if (FileSystem::exists(mainJsPath.c_str())) {
                    std::string content = FileSystem::readTextFile(mainJsPath.c_str());
                    syntaxErrorMessage = HarixKernel::checkSyntax(content.c_str());
                    if (syntaxErrorMessage.length() > 0) {
                        installSyntaxError = true;
                        installResultOk = false;
                        installState = 2;
                        showActionDialog = true;
                        drawActionDialog();
                        return;
                    }
                }
                
                // Validate packageName
                std::string pkg = currentAppMeta.packageName;
                bool validPkg = true;
                if (pkg.length() == 0 || kstr::indexOf(pkg, ' ') != -1 || kstr::indexOf(pkg, '.') == -1) validPkg = false;
                for (int c = 0; c < (int)pkg.length(); c++) {
                    if (isUpperCase(pkg[c])) validPkg = false;
                }
                
                if (!validPkg) {
                    installSyntaxError = true;
                    syntaxErrorMessage = "Invalid packageName!\nMust be lowercase,\nno spaces, dot-separated.";
                    installResultOk = false;
                    installState = 2;
                    showActionDialog = true;
                    drawActionDialog();
                    return;
                }

                // Check for updates and conflicts
                bool defaultSD = FileSystem::exists("/local/config_install_sd.txt");
                if (defaultSD && !FileSystem::exists("/sd/")) defaultSD = false;
                std::string destBase = defaultSD ? "/sd/apps/" : "/local/apps/";
                std::string destFolder = destBase + currentAppMeta.packageName + "/";
                
                isUpdatingApp = false;
                
                if (FileSystem::exists(destFolder.c_str())) {
                    std::string installedJsonPath = destFolder + "app.json";
                    if (FileSystem::exists(installedJsonPath.c_str())) {
                        std::string installedJsonContent = FileSystem::readTextFile(installedJsonPath.c_str());
                        std::string installedAuthor = FileSystem::parseJsonValue(installedJsonContent, "author");
                        std::string installedVersion = FileSystem::parseJsonValue(installedJsonContent, "version");
                        
                        if (installedAuthor != currentAppMeta.author) {
                            installSyntaxError = true;
                            syntaxErrorMessage = "Author conflict!\nInstalled: " + installedAuthor + "\nNew: " + currentAppMeta.author;
                            installResultOk = false;
                            installState = 2;
                            showActionDialog = true;
                            drawActionDialog();
                            return;
                        }
                        
                        if (isVersionGreater(currentAppMeta.version, installedVersion)) {
                            isUpdatingApp = true;
                        }
                    }
                }
                
                // Show app info dialog
                installState = 3;
                showActionDialog = true;
                drawActionDialog();
            } else {
                // Regular directory - navigate into it
                currentPath = files[fileIdx].path;
                if (!kstr::endsWith(currentPath, "/")) currentPath += "/";
                selectedIndex = 0;
                scrollOffset = 0;
                draw();
            }
        } else {
            // Regular file
            selectedFile = files[fileIdx].path;
            showActionDialog = true;
            installState = 0;
            drawActionDialog();
        }
    };

    // Direct Touch Selection (Single Tap)
    if (y >= UI::sy(45) && y <= UI::sy(270)) {
        int clickedItem = scrollOffset + ((y - UI::LIST_Y) / UI::ITEM_H);
        if (clickedItem < totalItems) {
            selectedIndex = clickedItem;
            
            if (hasUp && selectedIndex == 0) {
                int lastSlash = kstr::lastIndexOf(currentPath.substr(0, currentPath.length() - 1), '/');
                if (lastSlash >= 0) {
                    currentPath = currentPath.substr(0, lastSlash + 1);
                } else {
                    currentPath = "/";
                }
                selectedIndex = 0;
                scrollOffset = 0;
                draw();
            } else {
                int fileIdx = selectedIndex - (hasUp ? 1 : 0);
                selectItem(fileIdx);
            }
        }
        return;
    }

    // Footer Buttons
    if (y >= UI::sy(285) && y <= UI::sy(315)) {
        if (x < UI::sx(60)) { // ESC (BACK)
            currentState = 0;
            needsRescan = true;
            return;
        } else if (x >= UI::sx(60) && x < UI::sx(120)) { // UP
            if (selectedIndex > 0) {
                selectedIndex--;
                if (selectedIndex < scrollOffset) scrollOffset--;
                draw();
            }
        } else if (x >= UI::sx(120) && x < UI::sx(180)) { // SEL
            if (hasUp && selectedIndex == 0) {
                int lastSlash = kstr::lastIndexOf(currentPath.substr(0, currentPath.length() - 1), '/');
                if (lastSlash >= 0) {
                    currentPath = currentPath.substr(0, lastSlash + 1);
                } else {
                    currentPath = "/";
                }
                selectedIndex = 0;
                scrollOffset = 0;
                draw();
            } else {
                int fileIdx = selectedIndex - (hasUp ? 1 : 0);
                selectItem(fileIdx);
            }
        } else if (x >= UI::sx(180)) { // DN
            if (selectedIndex < totalItems - 1) {
                selectedIndex++;
                if (selectedIndex >= scrollOffset + UI::ITEMS_PER_PAGE) scrollOffset++;
                draw();
            }
        }
        return;
    }
    
    // Quick jump back to launcher if pressing header
    if (y < UI::sy(40)) {
        currentState = 0; // Back to launcher
        needsRescan = true;
        return;
    }
}
