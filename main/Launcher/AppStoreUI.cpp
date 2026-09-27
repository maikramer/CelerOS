#include "AppStoreUI.h"
#include "../Display/Layout.h"
#include "../FileSystem/FileSystem.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD.h>
#include "InstallerUI.h"
#include "../Utils/StrUtils.h"

extern int currentState;

KryonDisplay *AppStoreUI::tftInstance = nullptr;

int AppStoreUI::storeState = 0;
bool AppStoreUI::isUpdateMode = false;
int AppStoreUI::selectedIndex = 0;
int AppStoreUI::scrollOffset = 0;

std::string AppStoreUI::categoryNames[20];
std::string AppStoreUI::categoryUrls[20];
int AppStoreUI::categoryCount = 0;

AppStoreItem AppStoreUI::currentApps[50];
int AppStoreUI::currentAppCount = 0;
std::string AppStoreUI::currentCategoryName = "";
int AppStoreUI::selectedAppIndex = -1;

AppStoreItem AppStoreUI::updateApps[50];
int AppStoreUI::updateAppCount = 0;

std::string AppStoreUI::dialogMessage = "";
bool AppStoreUI::downloadInProgress = false;

const char* INDEX_URL = "https://raw.githubusercontent.com/Haris16-code/KryonOS-AppStore/refs/heads/main/index.json";

void AppStoreUI::init(KryonDisplay *tft) {
    tftInstance = tft;
}

// ============================================================
// Core Draw Router
// ============================================================
void AppStoreUI::draw() {
    if (!tftInstance) return;
    
    if (storeState == 0) {
        if (categoryCount == 0) {
            bool success = fetchCategories();
            if (!success) {
                storeState = 4;
                drawDialog();
                return;
            }
        }
        drawCategories();
    } else if (storeState == 1) {
        drawAppList();
    } else if (storeState == 2) {
        drawAppInfo();
    } else if (storeState == 3 || storeState == 4) {
        drawDialog();
    }
}

// ============================================================
// Network Fetching
// ============================================================
bool AppStoreUI::downloadFile(const std::string& url, const std::string& destPath, const std::string& loadingMsg) {
    if (WiFi.status() != WL_CONNECTED) {
        dialogMessage = "Please turn on WiFi first\nto access the app store.";
        return false;
    }
    
    HTTPClient http;
    http.begin(url.c_str());
    
    // Draw initial progress UI
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString(loadingMsg.c_str(), UI::sx(120), UI::sy(140), UI::font(2));
    tftInstance->drawRect(UI::sx(30), UI::sy(160), UI::sx(180), UI::sy(20), TFT_WHITE);
    
    int httpCode = http.GET();
    if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
        int totalLen = http.getSize();
        int downloaded = 0;
        
        WiFiClient *stream = http.getStreamPtr();
        fs::FS* targetFS = &LittleFS;
        std::string relPath = destPath;
        if (kstr::startsWith(destPath, "/sd/")) {
            targetFS = &SD;
            relPath = destPath.substr(3);
        } else if (kstr::startsWith(destPath, "/local/")) {
            targetFS = &LittleFS;
            relPath = destPath.substr(6);
        }
        
        File file = targetFS->open(relPath.c_str(), "w");
        if (!file) {
            dialogMessage = "Error: FS Write Failed!";
            http.end();
            return false;
        }
        
        uint8_t buff[512] = { 0 };
        int len;
        
        while (http.connected() && (totalLen == -1 || downloaded < totalLen)) {
            size_t size = stream->available();
            if (size) {
                int readLen = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                if (readLen > 0) {
                    file.write(buff, readLen);
                    downloaded += readLen;
                    
                    // Update Progress Bar
                    if (totalLen > 0) {
                        int progressWidth = map(downloaded, 0, totalLen, 0, 176);
                        tftInstance->fillRect(UI::sx(32), UI::sy(162), UI::sx(progressWidth), UI::sy(16), TFT_GREEN);
                    }
                }
            } else {
                delay(1);
            }
        }
        file.close();
        http.end();
        return true;
    } else {
        dialogMessage = "Error HTTP " + std::to_string(httpCode);
        http.end();
        return false;
    }
}

bool AppStoreUI::fetchCategories() {
    std::string tmpPath = "/tmp_index.json";
    if (!downloadFile(INDEX_URL, tmpPath, "Fetching App Store...")) {
        return false;
    }
    
    File file = LittleFS.open(tmpPath.c_str(), "r");
    if (!file) {
        dialogMessage = "Failed to open index";
        return false;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    LittleFS.remove(tmpPath.c_str());
    
    if (error) {
        dialogMessage = "JSON Parse Failed";
        return false;
    }
    
    JsonObject categories = doc["categories"];
    categoryCount = 0;
    
    // Add Check for Updates category
    categoryNames[categoryCount] = "[ Check For Apps Update ]";
    categoryUrls[categoryCount] = "UPDATE_ACTION";
    categoryCount++;
    
    for (JsonPair kv : categories) {
        if (categoryCount >= 20) break;
        categoryNames[categoryCount] = kv.key().c_str();
        categoryUrls[categoryCount] = kv.value().as<std::string>();
        categoryCount++;
    }
    
    return true;
}

bool AppStoreUI::fetchCategoryApps(const std::string& url) {
    if (url == "UPDATE_ACTION") return checkUpdates();
    
    std::string tmpPath = "/tmp_category.json";
    if (!downloadFile(url, tmpPath, "Loading " + currentCategoryName + "...")) {
        return false;
    }
    
    File file = LittleFS.open(tmpPath.c_str(), "r");
    if (!file) {
        dialogMessage = "Failed to open category";
        return false;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    LittleFS.remove(tmpPath.c_str());
    
    if (error) {
        dialogMessage = "Category Parse Failed";
        return false;
    }
    
    JsonObject apps = doc["apps"];
    currentAppCount = 0;
    
    for (JsonPair kv : apps) {
        if (currentAppCount >= 50) break;
        std::string id = kv.key().c_str();
        JsonObject appData = kv.value().as<JsonObject>();
        
        // Filter out apps that require a newer OS
        int requiredApi = appData["api"] | 1;
        if (requiredApi > KRYONOS_API_LEVEL) continue;
        
        currentApps[currentAppCount].id = id;
        currentApps[currentAppCount].metaUrl = appData["meta"].as<std::string>();
        currentApps[currentAppCount].appUrl = appData["app"].as<std::string>();
        
        // Default placeholders before fetching meta
        std::string displayName = id;
        if (displayName.length() > 0) {
            displayName[0] = toupper(displayName[0]);
        }
        currentApps[currentAppCount].name = displayName; 
        currentApps[currentAppCount].description = "Select to fetch details";
        currentApps[currentAppCount].author = "Unknown";
        currentApps[currentAppCount].version = "1.0.0";
        
        currentAppCount++;
    }
    
    // Sort apps alphabetically by name (case-insensitive)
    for (int i = 0; i < currentAppCount - 1; i++) {
        for (int j = 0; j < currentAppCount - i - 1; j++) {
            std::string name1 = currentApps[j].name; for (char &c : name1) c = tolower(c);
            std::string name2 = currentApps[j + 1].name; for (char &c : name2) c = tolower(c);
            if (name1.compare(name2) > 0) {
                AppStoreItem temp = currentApps[j];
                currentApps[j] = currentApps[j + 1];
                currentApps[j + 1] = temp;
            }
        }
    }
    
    return true;
}

int AppStoreUI::compareVersions(const std::string& v1, const std::string& v2) {
    int p1 = 0, p2 = 0;
    while(p1 < (int)v1.length() || p2 < (int)v2.length()) {
        int n1 = 0, n2 = 0;
        while(p1 < (int)v1.length() && v1[p1] != '.') n1 = n1 * 10 + (v1[p1++] - '0');
        while(p2 < (int)v2.length() && v2[p2] != '.') n2 = n2 * 10 + (v2[p2++] - '0');
        if (n1 > n2) return 1;
        if (n1 < n2) return -1;
        p1++; p2++;
    }
    return 0;
}

bool AppStoreUI::checkUpdates() {
    updateAppCount = 0;
    isUpdateMode = true;
    
    if (WiFi.status() != WL_CONNECTED) {
        dialogMessage = "Please turn on WiFi first\nto check for updates.";
        return false;
    }
    
    for (int i=0; i<2; i++) {
        fs::FS* targetFS = (i == 0) ? (fs::FS*)&SD : (fs::FS*)&LittleFS;
        if (!targetFS->exists("/apps")) continue;
        
        File root = targetFS->open("/apps");
        if (!root || !root.isDirectory()) continue;
        
        File appDir = root.openNextFile();
        while (appDir) {
            if (appDir.isDirectory()) {
                std::string appJsonPath = "/apps/";
                std::string dName = appDir.name();
                if (kstr::lastIndexOf(dName, '/') >= 0) dName = dName.substr(kstr::lastIndexOf(dName, '/') + 1);
                appJsonPath += dName + "/app.json";
                if (targetFS->exists(appJsonPath.c_str())) {
                    File jsonFile = targetFS->open(appJsonPath.c_str(), "r");
                    if (jsonFile) {
                        JsonDocument doc;
                        if (!deserializeJson(doc, jsonFile)) {
                            std::string metaUrl = doc["metaUrl"].as<std::string>();
                            std::string localVer = doc["version"].as<std::string>();
                            std::string pkgName = doc["packageName"].as<std::string>();
                            std::string name = doc["name"].as<std::string>();
                            
                            if (metaUrl.length() > 0 && updateAppCount < 50) {
                                std::string tmpPath = "/tmp_update.json";
                                if (downloadFile(metaUrl, tmpPath, "Checking " + name + "...")) {
                                    File remoteJson = LittleFS.open(tmpPath.c_str(), "r");
                                    if (remoteJson) {
                                        JsonDocument rdoc;
                                        if (!deserializeJson(rdoc, remoteJson)) {
                                            std::string remoteVer = rdoc["version"].as<std::string>();
                                            int remoteApi = rdoc["api"] | 1;
                                            
                                            if (compareVersions(remoteVer, localVer) > 0) {
                                                std::string baseUrl = metaUrl;
                                                int lastSlash = kstr::lastIndexOf(baseUrl, '/');
                                                if (lastSlash > 0) baseUrl = baseUrl.substr(0, lastSlash + 1);
                                                
                                                updateApps[updateAppCount].id = pkgName;
                                                updateApps[updateAppCount].name = rdoc["name"] | name;
                                                updateApps[updateAppCount].version = remoteVer;
                                                updateApps[updateAppCount].author = rdoc["author"] | "Unknown";
                                                std::string changelog = rdoc["changelog"] | "";
                                                if (changelog.length() > 0) {
                                                    updateApps[updateAppCount].description = changelog;
                                                } else {
                                                    updateApps[updateAppCount].description = "Update available!";
                                                }
                                                updateApps[updateAppCount].metaUrl = metaUrl;
                                                updateApps[updateAppCount].appUrl = baseUrl + "main.js";
                                                updateAppCount++;
                                            }
                                        }
                                        remoteJson.close();
                                    }
                                    LittleFS.remove(tmpPath.c_str());
                                }
                            }
                        }
                        jsonFile.close();
                    }
                }
            }
            appDir = root.openNextFile();
        }
    }
    
    // Copy to currentApps so the UI uses it
    currentAppCount = updateAppCount;
    for (int i=0; i<updateAppCount; i++) {
        currentApps[i] = updateApps[i];
    }
    
    if (currentAppCount == 0) {
        currentCategoryName = "All Apps are up to date";
        return true;
    } else {
        currentCategoryName = "Update Available";
    }
    
    // Sort apps alphabetically by name (case-insensitive)
    for (int i = 0; i < currentAppCount - 1; i++) {
        for (int j = 0; j < currentAppCount - i - 1; j++) {
            std::string name1 = currentApps[j].name; for (char &c : name1) c = tolower(c);
            std::string name2 = currentApps[j + 1].name; for (char &c : name2) c = tolower(c);
            if (name1.compare(name2) > 0) {
                AppStoreItem temp = currentApps[j];
                currentApps[j] = currentApps[j + 1];
                currentApps[j + 1] = temp;
            }
        }
    }
    
    return true;
}

void AppStoreUI::performInstall(int appIdx) {
    AppStoreItem& app = currentApps[appIdx];
    
    std::string destFolder = "/local/tmp_download/";
    if (FileSystem::exists(destFolder.c_str())) {
        FileSystem::deleteFile((destFolder + "app.json").c_str());
        FileSystem::deleteFile((destFolder + "main.js").c_str());
        FileSystem::rmdir(destFolder.c_str());
    }
    FileSystem::mkdir(destFolder.c_str());
    
    bool metaOk = downloadFile(app.metaUrl, destFolder + "app.json", "Downloading Meta...");
    if (!metaOk) return;
    
    bool appOk = downloadFile(app.appUrl, destFolder + "main.js", "Downloading App...");
    if (!appOk) {
        // Cleanup if failed
        FileSystem::deleteFile((destFolder + "app.json").c_str());
        FileSystem::deleteFile((destFolder + "main.js").c_str());
        FileSystem::rmdir(destFolder.c_str());
        return;
    }
    
    InstallerUI::autoInstallPath = destFolder;
    currentState = 3; // STATE_INSTALLER
    storeState = 0;   // Reset AppStoreUI state
}

// ============================================================
// UI Draw Methods
// ============================================================
void AppStoreUI::drawCategories() {
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("App Store", UI::sx(120), UI::sy(21), UI::font(2));
    
    tftInstance->fillRect(UI::sx(10), UI::sy(45), UI::sx(220), UI::sy(230), TFT_BLACK);

    int yPos = 45;
    int itemsPerPage = UI::ITEMS_PER_PAGE;
    int totalItems = categoryCount;
    
    if (totalItems == 0) {
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("No categories found.", UI::sx(120), UI::sy(100), UI::font(2));
    } else {
        for (int i = 0; i < itemsPerPage; i++) {
            int listIndex = scrollOffset + i;
            if (listIndex >= totalItems) break;
            
            std::string name = categoryNames[listIndex];
            
            if (listIndex == selectedIndex) {
                tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(25), TFT_WHITE);
                tftInstance->setTextColor(TFT_BLACK, TFT_WHITE);
                tftInstance->setTextDatum(ML_DATUM);
                tftInstance->drawString(("> " + name).c_str(), UI::sx(15), UI::sy(yPos + 12), UI::font(2));
            } else {
                tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
                tftInstance->setTextDatum(ML_DATUM);
                tftInstance->drawString(("  " + name).c_str(), UI::sx(15), UI::sy(yPos + 12), UI::font(2));
            }
            yPos += 30;
        }
    }
    
    // Footer
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(35), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(70), UI::sy(300), UI::font(2));
    tftInstance->drawString("UP", UI::sx(100), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(130), UI::sy(300), UI::font(2));
    tftInstance->drawString("SEL", UI::sx(165), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(200), UI::sy(300), UI::font(2));
    tftInstance->drawString("DN", UI::sx(220), UI::sy(300), UI::font(2));
}

void AppStoreUI::drawAppList() {
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString(currentCategoryName.c_str(), UI::sx(120), UI::sy(21), UI::font(2));
    
    tftInstance->fillRect(UI::sx(10), UI::sy(45), UI::sx(220), UI::sy(230), TFT_BLACK);

    int yPos = 45;
    int itemsPerPage = UI::ITEMS_PER_PAGE;
    int totalItems = currentAppCount;
    
    if (totalItems == 0) {
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
        tftInstance->drawString("No apps found.", UI::sx(120), UI::sy(100), UI::font(2));
    } else {
        for (int i = 0; i < itemsPerPage; i++) {
            int listIndex = scrollOffset + i;
            if (listIndex >= totalItems) break;
            
            std::string name = currentApps[listIndex].name;
            
            if (listIndex == selectedIndex) {
                tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(25), TFT_WHITE);
                tftInstance->setTextColor(TFT_BLACK, TFT_WHITE);
                tftInstance->setTextDatum(ML_DATUM);
                tftInstance->drawString(("> " + name).c_str(), UI::sx(15), UI::sy(yPos + 12), UI::font(2));
            } else {
                tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
                tftInstance->setTextDatum(ML_DATUM);
                tftInstance->drawString(("  " + name).c_str(), UI::sx(15), UI::sy(yPos + 12), UI::font(2));
            }
            yPos += 30;
        }
    }
    
    // Footer
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(35), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(70), UI::sy(300), UI::font(2));
    tftInstance->drawString("UP", UI::sx(100), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(130), UI::sy(300), UI::font(2));
    tftInstance->drawString("SEL", UI::sx(165), UI::sy(300), UI::font(2));
    tftInstance->drawString("|", UI::sx(200), UI::sy(300), UI::font(2));
    tftInstance->drawString("DN", UI::sx(220), UI::sy(300), UI::font(2));
}

void AppStoreUI::drawAppInfo() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    AppStoreItem& app = currentApps[selectedAppIndex];
    
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("App Details", UI::sx(120), UI::sy(21), UI::font(2));
    
    int y = 45;
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(TL_DATUM);
    
    tftInstance->drawString("Name:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->drawString(app.name.c_str(), UI::sx(10), UI::sy(y), UI::font(2)); y += 22;
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Author:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->drawString(app.author.c_str(), UI::sx(10), UI::sy(y), UI::font(2)); y += 22;
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Version:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->drawString(app.version.c_str(), UI::sx(10), UI::sy(y), UI::font(2)); y += 22;
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    if (isUpdateMode) {
        tftInstance->drawString("What's New:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    } else {
        tftInstance->drawString("Description:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    }
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    
    std::string desc = app.description;
    while(desc.length() > 0) {
        int splitIdx = 25;
        if(desc.length() <= 25) splitIdx = desc.length();
        else {
            int spaceIdx = kstr::lastIndexOf(desc.substr(0, 26), ' ');
            if(spaceIdx > 0) splitIdx = spaceIdx;
        }
        tftInstance->drawString(desc.substr(0, splitIdx).c_str(), UI::sx(10), UI::sy(y), UI::font(2));
        desc = desc.substr(splitIdx);
        desc = kstr::trim(desc);
        y += 15;
    }
    
    // Action Buttons
    tftInstance->fillRoundRect(UI::sx(25), UI::sy(230), UI::sx(80), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_BLACK, TFT_GREEN);
    tftInstance->setTextDatum(MC_DATUM);
    if (isUpdateMode) {
        tftInstance->drawString("UPDATE", UI::sx(65), UI::sy(245), UI::font(2));
    } else {
        tftInstance->drawString("DOWNLOAD", UI::sx(65), UI::sy(245), UI::font(2));
    }
    
    tftInstance->drawRoundRect(UI::sx(135), UI::sy(230), UI::sx(80), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("CANCEL", UI::sx(175), UI::sy(245), UI::font(2));
}

void AppStoreUI::drawDialog() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Message", UI::sx(120), UI::sy(21), UI::font(2));
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    int nlIdx = kstr::indexOf(dialogMessage, '\n');
    if (nlIdx > 0) {
        tftInstance->drawString(dialogMessage.substr(0, nlIdx).c_str(), UI::sx(120), UI::sy(130), UI::font(2));
        tftInstance->drawString(dialogMessage.substr(nlIdx + 1).c_str(), UI::sx(120), UI::sy(150), UI::font(2));
    } else {
        tftInstance->drawString(dialogMessage.c_str(), UI::sx(120), UI::sy(140), UI::font(2));
    }
    
    tftInstance->drawRoundRect(UI::sx(85), UI::sy(220), UI::sx(70), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("OK", UI::sx(120), UI::sy(235), UI::font(2));
}

// ============================================================
// Touch Handler
// ============================================================
void AppStoreUI::handleTouch(uint16_t x, uint16_t y) {
    if (storeState == 0) { // Categories
        if (y >= UI::sy(45) && y <= UI::sy(270)) {
            int clickedRelative = (y - UI::LIST_Y) / UI::ITEM_H;
            int clickedAbs = scrollOffset + clickedRelative;
            if (clickedAbs < categoryCount) {
                selectedIndex = clickedAbs;
                currentCategoryName = categoryNames[clickedAbs];
                isUpdateMode = (categoryUrls[clickedAbs] == "UPDATE_ACTION");
                
                bool ok = fetchCategoryApps(categoryUrls[clickedAbs]);
                if (!ok) {
                    storeState = 4;
                    drawDialog();
                } else {
                    storeState = 1;
                    selectedIndex = 0;
                    scrollOffset = 0;
                    draw();
                }
            }
            return;
        }
        
        if (y >= UI::sy(285) && y <= UI::sy(315)) {
            if (x < UI::sx(70)) { // BACK
                currentState = 0;
                categoryCount = 0; // force refetch next time
            } else if (x >= UI::sx(70) && x < UI::sx(130)) { // UP
                if (selectedIndex > 0) {
                    selectedIndex--;
                    if (selectedIndex < scrollOffset) scrollOffset--;
                    draw();
                }
            } else if (x >= UI::sx(130) && x < UI::sx(200)) { // SEL
                currentCategoryName = categoryNames[selectedIndex];
                isUpdateMode = (categoryUrls[selectedIndex] == "UPDATE_ACTION");
                
                bool ok = fetchCategoryApps(categoryUrls[selectedIndex]);
                if (!ok) {
                    storeState = 4;
                    drawDialog();
                } else {
                    storeState = 1;
                    selectedIndex = 0;
                    scrollOffset = 0;
                    draw();
                }
            } else if (x >= UI::sx(200)) { // DN
                if (selectedIndex < categoryCount - 1) {
                    selectedIndex++;
                    if (selectedIndex >= scrollOffset + UI::ITEMS_PER_PAGE) scrollOffset++;
                    draw();
                }
            }
        }
    } else if (storeState == 1) { // App List
        if (y >= UI::sy(45) && y <= UI::sy(270)) {
            int clickedRelative = (y - UI::LIST_Y) / UI::ITEM_H;
            int clickedAbs = scrollOffset + clickedRelative;
            if (clickedAbs < currentAppCount) {
                selectedIndex = clickedAbs;
                selectedAppIndex = clickedAbs;
                
                // Fetch Meta for details
                std::string tmpPath = "/tmp_meta.json";
                if (downloadFile(currentApps[selectedAppIndex].metaUrl, tmpPath, "Loading details...")) {
                    File file = LittleFS.open(tmpPath.c_str(), "r");
                    if (file) {
                        JsonDocument doc;
                        if (!deserializeJson(doc, file)) {
                            int appApi = doc["api"] | 1;
                            if (appApi > KRYONOS_API_LEVEL) {
                                file.close();
                                LittleFS.remove(tmpPath.c_str());
                                if (isUpdateMode) {
                                    dialogMessage = "API " + std::to_string(appApi) + " needed to update.\nPlease update OS first!";
                                } else {
                                    dialogMessage = "This App Requires KryonOS API " + std::to_string(appApi) + "\nPlease update OS!";
                                }
                                storeState = 4;
                                drawDialog();
                                return;
                            }
                            currentApps[selectedAppIndex].name = doc["name"] | currentApps[selectedAppIndex].id;
                            currentApps[selectedAppIndex].description = doc["description"] | "No description.";
                            currentApps[selectedAppIndex].author = doc["author"] | "Unknown";
                            currentApps[selectedAppIndex].version = doc["version"] | "1.0.0";
                            
                            // Check if installed and if this is an update
                            isUpdateMode = false;
                            std::string pkgName = doc["packageName"] | currentApps[selectedAppIndex].id;
                            for (int fsIdx=0; fsIdx<2; fsIdx++) {
                                std::string localPath = (fsIdx == 0 ? "/sd/apps/" : "/local/apps/") + pkgName + "/app.json";
                                if (FileSystem::exists(localPath.c_str())) {
                                    std::string localJson = FileSystem::readTextFile(localPath.c_str());
                                    if (localJson.length() > 0) {
                                        std::string localVer = FileSystem::parseJsonValue(localJson, "version");
                                        if (compareVersions(currentApps[selectedAppIndex].version, localVer) > 0) {
                                            isUpdateMode = true;
                                        }
                                    }
                                }
                            }
                            
                            if (isUpdateMode) {
                                std::string changelog = doc["changelog"] | "";
                                if (changelog.length() > 0) {
                                    currentApps[selectedAppIndex].description = changelog;
                                } else {
                                    currentApps[selectedAppIndex].description = "Update available!";
                                }
                            }
                        }
                        file.close();
                        LittleFS.remove(tmpPath.c_str());
                    }
                }
                
                storeState = 2;
                draw();
            }
            return;
        }
        
        if (y >= UI::sy(285) && y <= UI::sy(315)) {
            if (x < UI::sx(70)) { // BACK
                storeState = 0;
                selectedIndex = 0;
                scrollOffset = 0;
                draw();
            } else if (x >= UI::sx(70) && x < UI::sx(130)) { // UP
                if (selectedIndex > 0) {
                    selectedIndex--;
                    if (selectedIndex < scrollOffset) scrollOffset--;
                    draw();
                }
            } else if (x >= UI::sx(130) && x < UI::sx(200)) { // SEL
                selectedAppIndex = selectedIndex;
                
                // Fetch Meta for details
                std::string tmpPath = "/tmp_meta.json";
                if (downloadFile(currentApps[selectedAppIndex].metaUrl, tmpPath, "Loading details...")) {
                    File file = LittleFS.open(tmpPath.c_str(), "r");
                    if (file) {
                        JsonDocument doc;
                        if (!deserializeJson(doc, file)) {
                            int appApi = doc["api"] | 1;
                            if (appApi > KRYONOS_API_LEVEL) {
                                file.close();
                                LittleFS.remove(tmpPath.c_str());
                                if (isUpdateMode) {
                                    dialogMessage = "API " + std::to_string(appApi) + " needed to update.\nPlease update OS first!";
                                } else {
                                    dialogMessage = "This App Requires KryonOS API " + std::to_string(appApi) + "\nPlease update OS!";
                                }
                                storeState = 4;
                                drawDialog();
                                return;
                            }
                            currentApps[selectedAppIndex].name = doc["name"] | currentApps[selectedAppIndex].id;
                            currentApps[selectedAppIndex].description = doc["description"] | "No description.";
                            currentApps[selectedAppIndex].author = doc["author"] | "Unknown";
                            currentApps[selectedAppIndex].version = doc["version"] | "1.0.0";
                            
                            // Check if installed and if this is an update
                            isUpdateMode = false;
                            std::string pkgName = doc["packageName"] | currentApps[selectedAppIndex].id;
                            for (int fsIdx=0; fsIdx<2; fsIdx++) {
                                std::string localPath = (fsIdx == 0 ? "/sd/apps/" : "/local/apps/") + pkgName + "/app.json";
                                if (FileSystem::exists(localPath.c_str())) {
                                    std::string localJson = FileSystem::readTextFile(localPath.c_str());
                                    if (localJson.length() > 0) {
                                        std::string localVer = FileSystem::parseJsonValue(localJson, "version");
                                        if (compareVersions(currentApps[selectedAppIndex].version, localVer) > 0) {
                                            isUpdateMode = true;
                                        }
                                    }
                                }
                            }
                            
                            if (isUpdateMode) {
                                std::string changelog = doc["changelog"] | "";
                                if (changelog.length() > 0) {
                                    currentApps[selectedAppIndex].description = changelog;
                                } else {
                                    currentApps[selectedAppIndex].description = "Update available!";
                                }
                            }
                        }
                        file.close();
                        LittleFS.remove(tmpPath.c_str());
                    }
                }
                
                storeState = 2;
                draw();
            } else if (x >= UI::sx(200)) { // DN
                if (selectedIndex < currentAppCount - 1) {
                    selectedIndex++;
                    if (selectedIndex >= scrollOffset + UI::ITEMS_PER_PAGE) scrollOffset++;
                    draw();
                }
            }
        }
    } else if (storeState == 2) { // App Info
        if (y >= UI::sy(230) && y <= UI::sy(260)) {
            if (x >= UI::sx(25) && x <= UI::sx(105)) { // INSTALL
                performInstall(selectedAppIndex);
                draw();
            } else if (x >= UI::sx(135) && x <= UI::sx(215)) { // CANCEL
                storeState = 1;
                draw();
            }
        }
    } else if (storeState == 3 || storeState == 4) { // Dialog
        if (x >= UI::sx(85) && x <= UI::sx(155) && y >= UI::sy(220) && y <= UI::sy(250)) {
            if (categoryCount == 0) {
                extern int currentState;
                currentState = 0; // Back to Launcher
            } else {
                storeState = 0; // return to categories on dialog close
                draw();
            }
        }
    }
}
