#include "AppStoreUI.h"
#include "../Display/Layout.h"
#include "../File System/FileSystem.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD.h>
#include "InstallerUI.h"

extern int currentState;

KryonDisplay *AppStoreUI::tftInstance = nullptr;

int AppStoreUI::storeState = 0;
bool AppStoreUI::isUpdateMode = false;
int AppStoreUI::selectedIndex = 0;
int AppStoreUI::scrollOffset = 0;

String AppStoreUI::categoryNames[20];
String AppStoreUI::categoryUrls[20];
int AppStoreUI::categoryCount = 0;

AppStoreItem AppStoreUI::currentApps[50];
int AppStoreUI::currentAppCount = 0;
String AppStoreUI::currentCategoryName = "";
int AppStoreUI::selectedAppIndex = -1;

AppStoreItem AppStoreUI::updateApps[50];
int AppStoreUI::updateAppCount = 0;

String AppStoreUI::dialogMessage = "";
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
bool AppStoreUI::downloadFile(const String& url, const String& destPath, const String& loadingMsg) {
    if (WiFi.status() != WL_CONNECTED) {
        dialogMessage = "Please turn on WiFi first\nto access the app store.";
        return false;
    }
    
    HTTPClient http;
    http.begin(url);
    
    // Draw initial progress UI
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString(loadingMsg, UI::sx(120), UI::sy(140), UI::font(2));
    tftInstance->drawRect(UI::sx(30), UI::sy(160), UI::sx(180), UI::sy(20), TFT_WHITE);
    
    int httpCode = http.GET();
    if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
        int totalLen = http.getSize();
        int downloaded = 0;
        
        WiFiClient *stream = http.getStreamPtr();
        fs::FS* targetFS = &LittleFS;
        String relPath = destPath;
        if (destPath.startsWith("/sd/")) {
            targetFS = &SD;
            relPath = destPath.substring(3);
        } else if (destPath.startsWith("/local/")) {
            targetFS = &LittleFS;
            relPath = destPath.substring(6);
        }
        
        File file = targetFS->open(relPath, "w");
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
        dialogMessage = "Error HTTP " + String(httpCode);
        http.end();
        return false;
    }
}

bool AppStoreUI::fetchCategories() {
    String tmpPath = "/tmp_index.json";
    if (!downloadFile(INDEX_URL, tmpPath, "Fetching App Store...")) {
        return false;
    }
    
    File file = LittleFS.open(tmpPath, "r");
    if (!file) {
        dialogMessage = "Failed to open index";
        return false;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    LittleFS.remove(tmpPath);
    
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
        categoryUrls[categoryCount] = kv.value().as<String>();
        categoryCount++;
    }
    
    return true;
}

bool AppStoreUI::fetchCategoryApps(const String& url) {
    if (url == "UPDATE_ACTION") return checkUpdates();
    
    String tmpPath = "/tmp_category.json";
    if (!downloadFile(url, tmpPath, "Loading " + currentCategoryName + "...")) {
        return false;
    }
    
    File file = LittleFS.open(tmpPath, "r");
    if (!file) {
        dialogMessage = "Failed to open category";
        return false;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    LittleFS.remove(tmpPath);
    
    if (error) {
        dialogMessage = "Category Parse Failed";
        return false;
    }
    
    JsonObject apps = doc["apps"];
    currentAppCount = 0;
    
    for (JsonPair kv : apps) {
        if (currentAppCount >= 50) break;
        String id = kv.key().c_str();
        JsonObject appData = kv.value().as<JsonObject>();
        
        // Filter out apps that require a newer OS
        int requiredApi = appData["api"] | 1;
        if (requiredApi > KRYONOS_API_LEVEL) continue;
        
        currentApps[currentAppCount].id = id;
        currentApps[currentAppCount].metaUrl = appData["meta"].as<String>();
        currentApps[currentAppCount].appUrl = appData["app"].as<String>();
        
        // Default placeholders before fetching meta
        String displayName = id;
        if (displayName.length() > 0) {
            displayName.setCharAt(0, toupper(displayName[0]));
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
            String name1 = currentApps[j].name; name1.toLowerCase();
            String name2 = currentApps[j + 1].name; name2.toLowerCase();
            if (name1.compareTo(name2) > 0) {
                AppStoreItem temp = currentApps[j];
                currentApps[j] = currentApps[j + 1];
                currentApps[j + 1] = temp;
            }
        }
    }
    
    return true;
}

int AppStoreUI::compareVersions(const String& v1, const String& v2) {
    int p1 = 0, p2 = 0;
    while(p1 < v1.length() || p2 < v2.length()) {
        int n1 = 0, n2 = 0;
        while(p1 < v1.length() && v1[p1] != '.') n1 = n1 * 10 + (v1[p1++] - '0');
        while(p2 < v2.length() && v2[p2] != '.') n2 = n2 * 10 + (v2[p2++] - '0');
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
                String appJsonPath = "/apps/";
                String dName = appDir.name();
                if (dName.lastIndexOf('/') >= 0) dName = dName.substring(dName.lastIndexOf('/') + 1);
                appJsonPath += dName + "/app.json";
                if (targetFS->exists(appJsonPath)) {
                    File jsonFile = targetFS->open(appJsonPath, "r");
                    if (jsonFile) {
                        JsonDocument doc;
                        if (!deserializeJson(doc, jsonFile)) {
                            String metaUrl = doc["metaUrl"].as<String>();
                            String localVer = doc["version"].as<String>();
                            String pkgName = doc["packageName"].as<String>();
                            String name = doc["name"].as<String>();
                            
                            if (metaUrl.length() > 0 && updateAppCount < 50) {
                                String tmpPath = "/tmp_update.json";
                                if (downloadFile(metaUrl, tmpPath, "Checking " + name + "...")) {
                                    File remoteJson = LittleFS.open(tmpPath, "r");
                                    if (remoteJson) {
                                        JsonDocument rdoc;
                                        if (!deserializeJson(rdoc, remoteJson)) {
                                            String remoteVer = rdoc["version"].as<String>();
                                            int remoteApi = rdoc["api"] | 1;
                                            
                                            if (compareVersions(remoteVer, localVer) > 0) {
                                                String baseUrl = metaUrl;
                                                int lastSlash = baseUrl.lastIndexOf('/');
                                                if (lastSlash > 0) baseUrl = baseUrl.substring(0, lastSlash + 1);
                                                
                                                updateApps[updateAppCount].id = pkgName;
                                                updateApps[updateAppCount].name = rdoc["name"] | name;
                                                updateApps[updateAppCount].version = remoteVer;
                                                updateApps[updateAppCount].author = rdoc["author"] | "Unknown";
                                                String changelog = rdoc["changelog"] | "";
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
                                    LittleFS.remove(tmpPath);
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
            String name1 = currentApps[j].name; name1.toLowerCase();
            String name2 = currentApps[j + 1].name; name2.toLowerCase();
            if (name1.compareTo(name2) > 0) {
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
    
    String destFolder = "/local/tmp_download/";
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
            
            String name = categoryNames[listIndex];
            
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
    tftInstance->drawString(currentCategoryName, UI::sx(120), UI::sy(21), UI::font(2));
    
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
            
            String name = currentApps[listIndex].name;
            
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
    tftInstance->drawString(app.name, UI::sx(10), UI::sy(y), UI::font(2)); y += 22;
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Author:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->drawString(app.author, UI::sx(10), UI::sy(y), UI::font(2)); y += 22;
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("Version:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tftInstance->drawString(app.version, UI::sx(10), UI::sy(y), UI::font(2)); y += 22;
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    if (isUpdateMode) {
        tftInstance->drawString("What's New:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    } else {
        tftInstance->drawString("Description:", UI::sx(10), UI::sy(y), UI::font(2)); y += 18;
    }
    tftInstance->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    
    String desc = app.description;
    while(desc.length() > 0) {
        int splitIdx = 25;
        if(desc.length() <= 25) splitIdx = desc.length();
        else {
            int spaceIdx = desc.lastIndexOf(' ', 25);
            if(spaceIdx > 0) splitIdx = spaceIdx;
        }
        tftInstance->drawString(desc.substring(0, splitIdx), UI::sx(10), UI::sy(y), UI::font(2));
        desc = desc.substring(splitIdx);
        desc.trim();
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
    int nlIdx = dialogMessage.indexOf('\n');
    if (nlIdx > 0) {
        tftInstance->drawString(dialogMessage.substring(0, nlIdx), UI::sx(120), UI::sy(130), UI::font(2));
        tftInstance->drawString(dialogMessage.substring(nlIdx + 1), UI::sx(120), UI::sy(150), UI::font(2));
    } else {
        tftInstance->drawString(dialogMessage, UI::sx(120), UI::sy(140), UI::font(2));
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
                String tmpPath = "/tmp_meta.json";
                if (downloadFile(currentApps[selectedAppIndex].metaUrl, tmpPath, "Loading details...")) {
                    File file = LittleFS.open(tmpPath, "r");
                    if (file) {
                        JsonDocument doc;
                        if (!deserializeJson(doc, file)) {
                            int appApi = doc["api"] | 1;
                            if (appApi > KRYONOS_API_LEVEL) {
                                file.close();
                                LittleFS.remove(tmpPath);
                                if (isUpdateMode) {
                                    dialogMessage = "API " + String(appApi) + " needed to update.\nPlease update OS first!";
                                } else {
                                    dialogMessage = "This App Requires KryonOS API " + String(appApi) + "\nPlease update OS!";
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
                            String pkgName = doc["packageName"] | currentApps[selectedAppIndex].id;
                            for (int fsIdx=0; fsIdx<2; fsIdx++) {
                                String localPath = (fsIdx == 0 ? "/sd/apps/" : "/local/apps/") + pkgName + "/app.json";
                                if (FileSystem::exists(localPath.c_str())) {
                                    String localJson = FileSystem::readTextFile(localPath.c_str());
                                    if (localJson.length() > 0) {
                                        String localVer = FileSystem::parseJsonValue(localJson, "version");
                                        if (compareVersions(currentApps[selectedAppIndex].version, localVer) > 0) {
                                            isUpdateMode = true;
                                        }
                                    }
                                }
                            }
                            
                            if (isUpdateMode) {
                                String changelog = doc["changelog"] | "";
                                if (changelog.length() > 0) {
                                    currentApps[selectedAppIndex].description = changelog;
                                } else {
                                    currentApps[selectedAppIndex].description = "Update available!";
                                }
                            }
                        }
                        file.close();
                        LittleFS.remove(tmpPath);
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
                String tmpPath = "/tmp_meta.json";
                if (downloadFile(currentApps[selectedAppIndex].metaUrl, tmpPath, "Loading details...")) {
                    File file = LittleFS.open(tmpPath, "r");
                    if (file) {
                        JsonDocument doc;
                        if (!deserializeJson(doc, file)) {
                            int appApi = doc["api"] | 1;
                            if (appApi > KRYONOS_API_LEVEL) {
                                file.close();
                                LittleFS.remove(tmpPath);
                                if (isUpdateMode) {
                                    dialogMessage = "API " + String(appApi) + " needed to update.\nPlease update OS first!";
                                } else {
                                    dialogMessage = "This App Requires KryonOS API " + String(appApi) + "\nPlease update OS!";
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
                            String pkgName = doc["packageName"] | currentApps[selectedAppIndex].id;
                            for (int fsIdx=0; fsIdx<2; fsIdx++) {
                                String localPath = (fsIdx == 0 ? "/sd/apps/" : "/local/apps/") + pkgName + "/app.json";
                                if (FileSystem::exists(localPath.c_str())) {
                                    String localJson = FileSystem::readTextFile(localPath.c_str());
                                    if (localJson.length() > 0) {
                                        String localVer = FileSystem::parseJsonValue(localJson, "version");
                                        if (compareVersions(currentApps[selectedAppIndex].version, localVer) > 0) {
                                            isUpdateMode = true;
                                        }
                                    }
                                }
                            }
                            
                            if (isUpdateMode) {
                                String changelog = doc["changelog"] | "";
                                if (changelog.length() > 0) {
                                    currentApps[selectedAppIndex].description = changelog;
                                } else {
                                    currentApps[selectedAppIndex].description = "Update available!";
                                }
                            }
                        }
                        file.close();
                        LittleFS.remove(tmpPath);
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
