#include "HelpCenterUI.h"
#include "../Display/Layout.h"
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"
#include <algorithm>
#include "../WebManager/WebManager.h"
#include "HttpClient.h"

KryonDisplay* HelpCenterUI::tftInstance = nullptr;

int HelpCenterUI::uiState = 0;
int HelpCenterUI::selectedIndex = 0;
int HelpCenterUI::scrollOffset = 0;
int HelpCenterUI::listCount = 0;
std::string HelpCenterUI::listItems[25];
std::string HelpCenterUI::listUrls[25];
int HelpCenterUI::selectedCategoryIndex = 0;

std::string HelpCenterUI::currentViewerTitle = "";
std::string HelpCenterUI::currentViewerContent = "";
int HelpCenterUI::viewerScrollOffset = 0;
std::string HelpCenterUI::dialogMessage = "";
int HelpCenterUI::titleScrollPos = 0;
unsigned long HelpCenterUI::lastTitleScrollTime = 0;

std::string HelpCenterUI::currentCategoryName = "";
int HelpCenterUI::listScrollPos = 0;
unsigned long HelpCenterUI::lastListScrollTime = 0;

// --- Offline Data ---
const char* offCats[] = {"Getting Started", "Basic Navigation", "Connectivity", "Troubleshooting"};
const int offCatCount = 4;

const char* offTopics0[] = {"What is KryonOS", "First setup guide", "System vs User apps"};
const char* offContent0[] = {
    "KryonOS is a fast, lightweight operating system built specifically for ESP32. It features an onboard app store, JavaScript app execution from SD, and a smooth UI interface.",
    "To get started, go to Settings -> WiFi to connect your device. Make sure a FAT32 formatted SD card is inserted if you plan to install new user apps.",
    "System apps (like Settings, Launcher) run deeply integrated in C++ for maximum speed. User apps run in KryonOS JavaScript Runtime from the SD card or local memory."
};

const char* offTopics1[] = {"Home screen overview", "Opening & closing apps"};
const char* offContent1[] = {
    "The Home screen lists system settings at the top and your installed user apps below. Use physical hardware buttons or touch controls to navigate up and down.",
    "Tap an app name to open it. To close any running user app, simply tap the red X button in the top right corner of the screen to return to the launcher."
};

const char* offTopics2[] = {"How to connect WiFi"};
const char* offContent2[] = {
    "Go to Settings -> WiFi. The device will scan networks. Tap an available network, enter the password using the on-screen keyboard, and press Connect. The device will reboot to apply."
};

const char* offTopics3[] = {"Black screen & Crash"};
const char* offContent3[] = {
    "If you experience a black screen or ESP crash when trying to run JS apps, please turn off WiFi. This will free up the RAM and allow your app to work fine."
};


void HelpCenterUI::init(KryonDisplay *tft) {
    tftInstance = tft;
}

void HelpCenterUI::draw() {
    if (!tftInstance) return;
    
    if (uiState == 0) drawMainMenu();
    else if (uiState == 1) drawList("Offline Categories");
    else if (uiState == 2) drawList(offCats[selectedCategoryIndex]);
    else if (uiState == 3) drawList("Online Categories");
    else if (uiState == 4) drawList(currentCategoryName);
    else if (uiState == 5) drawViewer();
    else if (uiState == 6) drawDialog();
}

void HelpCenterUI::drawMainMenu() {
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Help Center", UI::sx(120), UI::sy(21), UI::font(2));
    
    tftInstance->fillRect(UI::sx(10), UI::sy(45), UI::sx(220), UI::sy(230), TFT_BLACK);
    
    // Offline Button
    if (selectedIndex == 0) {
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(80), UI::sx(200), UI::sy(40), UI::sx(5), TFT_WHITE);
        tftInstance->setTextColor(TFT_BLACK, TFT_WHITE);
    } else {
        tftInstance->drawRoundRect(UI::sx(20), UI::sy(80), UI::sx(200), UI::sy(40), UI::sx(5), TFT_WHITE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tftInstance->drawString("Offline Help Center", UI::sx(120), UI::sy(100), UI::font(2));
    
    // Online Button
    if (selectedIndex == 1) {
        tftInstance->fillRoundRect(UI::sx(20), UI::sy(140), UI::sx(200), UI::sy(40), UI::sx(5), TFT_WHITE);
        tftInstance->setTextColor(TFT_BLACK, TFT_WHITE);
    } else {
        tftInstance->drawRoundRect(UI::sx(20), UI::sy(140), UI::sx(200), UI::sy(40), UI::sx(5), TFT_WHITE);
        tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tftInstance->drawString("Online Help Center", UI::sx(120), UI::sy(160), UI::font(2));
    
    // Footer
    tftInstance->fillRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(120), UI::sy(300), UI::font(2));
}

void HelpCenterUI::drawList(const std::string& title) {
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    
    int titleWidth = tftInstance->textWidth(title, UI::font(2));
    if (titleWidth > UI::sx(200)) {
        std::string scrollText = title + "      ";
        int len = scrollText.length();
        std::string shifted = "";
        for (int i = 0; i < len; i++) {
            char c = scrollText[(titleScrollPos + i) % len];
            if (tftInstance->textWidth(shifted + c, UI::font(2)) > UI::sx(210)) break;
            shifted += c;
        }
        tftInstance->setTextDatum(ML_DATUM);
        tftInstance->drawString(shifted.c_str(), UI::sx(10), UI::sy(21), UI::font(2));
    } else {
        tftInstance->drawString(title.c_str(), UI::sx(120), UI::sy(21), UI::font(2));
    }
    
    tftInstance->fillRect(UI::sx(10), UI::sy(45), UI::sx(220), UI::sy(230), TFT_BLACK);
    
    int yPos = 45;
    int itemsPerPage = UI::ITEMS_PER_PAGE;
    tftInstance->setTextDatum(TL_DATUM);
    
    for (int i=0; i<itemsPerPage; i++) {
        int idx = scrollOffset + i;
        if (idx >= listCount) break;
        
        if (idx == selectedIndex) {
            tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(25), TFT_WHITE);
            tftInstance->setTextColor(TFT_BLACK, TFT_WHITE);
            
            int itemWidth = tftInstance->textWidth(listItems[idx], UI::font(2));
            if (itemWidth > UI::sx(190)) {
                std::string scrollText = listItems[idx] + "      ";
                int len = scrollText.length();
                std::string shifted = "";
                for (int j = 0; j < len; j++) {
                    char c = scrollText[(listScrollPos + j) % len];
                    if (tftInstance->textWidth(shifted + c, UI::font(2)) > UI::sx(190)) break;
                    shifted += c;
                }
                tftInstance->drawString(("> " + shifted).c_str(), UI::sx(15), UI::sy(yPos + 4), UI::font(2));
            } else {
                tftInstance->drawString(("> " + listItems[idx]).c_str(), UI::sx(15), UI::sy(yPos + 4), UI::font(2));
            }
        } else {
            tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
            tftInstance->drawString(("  " + listItems[idx]).c_str(), UI::sx(15), UI::sy(yPos + 4), UI::font(2));
        }
        yPos += 30;
    }
    
    // Scrollbar
    if (listCount > itemsPerPage) {
        int thumbH = std::max(20, (230 * itemsPerPage) / listCount);
        int thumbY = 45 + (scrollOffset * (230 - thumbH)) / (listCount - itemsPerPage);
        tftInstance->fillRect(UI::sx(232), UI::sy(45), UI::sx(3), UI::sy(230), TFT_DARKGREY);
        tftInstance->fillRect(UI::sx(232), UI::sy(thumbY), UI::sx(3), UI::sy(thumbH), TFT_WHITE);
    }
    
    tftInstance->fillRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(35), UI::sy(300), UI::font(2));
    tftInstance->drawString("UP", UI::sx(95), UI::sy(300), UI::font(2));
    tftInstance->drawString("SEL", UI::sx(155), UI::sy(300), UI::font(2));
    tftInstance->drawString("DN", UI::sx(215), UI::sy(300), UI::font(2));
}

void HelpCenterUI::drawViewer() {
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    
    int titleWidth = tftInstance->textWidth(currentViewerTitle, UI::font(2));
    if (titleWidth > UI::sx(200)) {
        std::string scrollText = currentViewerTitle + "      ";
        int len = scrollText.length();
        std::string shifted = "";
        for (int i = 0; i < len; i++) {
            char c = scrollText[(titleScrollPos + i) % len];
            if (tftInstance->textWidth(shifted + c, UI::font(2)) > UI::sx(210)) break;
            shifted += c;
        }
        tftInstance->setTextDatum(ML_DATUM);
        tftInstance->drawString(shifted.c_str(), UI::sx(10), UI::sy(21), UI::font(2));
    } else {
        tftInstance->drawString(currentViewerTitle.c_str(), UI::sx(120), UI::sy(21), UI::font(2));
    }
    
    tftInstance->fillRect(UI::sx(10), UI::sy(45), UI::sx(220), UI::sy(230), TFT_BLACK);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(TL_DATUM);
    
    int yPos = 45;
    int currentLine = 0;
    
    std::string content = currentViewerContent;
    while(content.length() > 0) {
        int splitIdx = 22; // Max chars per line
        if(content.length() <= 22) splitIdx = content.length();
        else {
            size_t spaceIdx = content.rfind(' ', 22);
            if(spaceIdx != std::string::npos && spaceIdx > 0) splitIdx = (int)spaceIdx;
        }

        int nlIdx = kstr::indexOf(content, '\n');
        if(nlIdx >= 0 && nlIdx < splitIdx) {
            splitIdx = nlIdx;
        }

        if (currentLine >= viewerScrollOffset && currentLine < viewerScrollOffset + 14) {
            tftInstance->drawString(content.substr(0, splitIdx).c_str(), UI::sx(12), UI::sy(yPos), UI::font(2));
            yPos += 16;
        }

        content = content.substr(splitIdx);
        if(kstr::startsWith(content, "\n") || kstr::startsWith(content, " ")) {
            content = content.substr(1);
        }
        currentLine++;
    }
    
    // Up/Down Indicators
    if (viewerScrollOffset > 0) {
        tftInstance->fillTriangle(UI::sx(220), UI::sy(50), UI::sx(230), UI::sy(60), UI::sx(210), UI::sy(60), TFT_WHITE);
    }
    if (currentLine > viewerScrollOffset + 14) {
        tftInstance->fillTriangle(UI::sx(220), UI::sy(265), UI::sx(210), UI::sy(255), UI::sx(230), UI::sy(255), TFT_WHITE);
    }
    
    tftInstance->fillRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(5), UI::sy(285), UI::sx(230), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("BACK", UI::sx(40), UI::sy(300), UI::font(2));
    tftInstance->drawString("UP", UI::sx(120), UI::sy(300), UI::font(2));
    tftInstance->drawString("DN", UI::sx(200), UI::sy(300), UI::font(2));
}

void HelpCenterUI::drawDialog() {
    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_RED);
    tftInstance->setTextColor(TFT_RED, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString("Error", UI::sx(120), UI::sy(21), UI::font(2));
    
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString(dialogMessage.c_str(), UI::sx(120), UI::sy(140), UI::font(2));
    
    tftInstance->drawRoundRect(UI::sx(85), UI::sy(220), UI::sx(70), UI::sy(30), UI::sx(5), TFT_WHITE);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->drawString("OK", UI::sx(120), UI::sy(235), UI::font(2));
}

// --- Loaders ---
void HelpCenterUI::loadOfflineCategories() {
    listCount = offCatCount;
    for(int i=0; i<listCount; i++) listItems[i] = offCats[i];
    selectedIndex = 0;
    scrollOffset = 0;
    uiState = 1;
    draw();
}

void HelpCenterUI::loadOfflineTopics(int catIdx) {
    if (catIdx == 0) { listCount = 3; for(int i=0; i<3; i++) listItems[i] = offTopics0[i]; }
    else if (catIdx == 1) { listCount = 2; for(int i=0; i<2; i++) listItems[i] = offTopics1[i]; }
    else if (catIdx == 2) { listCount = 1; for(int i=0; i<1; i++) listItems[i] = offTopics2[i]; }
    else if (catIdx == 3) { listCount = 1; for(int i=0; i<1; i++) listItems[i] = offTopics3[i]; }
    selectedIndex = 0;
    scrollOffset = 0;
    uiState = 2;
    draw();
}

void HelpCenterUI::loadOfflineContent(int catIdx, int topicIdx) {
    if (catIdx == 0) { currentViewerTitle = offTopics0[topicIdx]; currentViewerContent = offContent0[topicIdx]; }
    else if (catIdx == 1) { currentViewerTitle = offTopics1[topicIdx]; currentViewerContent = offContent1[topicIdx]; }
    else if (catIdx == 2) { currentViewerTitle = offTopics2[topicIdx]; currentViewerContent = offContent2[topicIdx]; }
    else if (catIdx == 3) { currentViewerTitle = offTopics3[topicIdx]; currentViewerContent = offContent3[topicIdx]; }
    viewerScrollOffset = 0;
    titleScrollPos = 0;
    lastTitleScrollTime = millis();
    uiState = 5;
    draw();
}

bool HelpCenterUI::downloadFile(const std::string& url, const std::string& destPath, const std::string& loadingMsg) {
    if (!WebManager::isWifiConnected()) {
        dialogMessage = "Please turn on WiFi first!";
        return false;
    }

    tftInstance->fillScreen(TFT_BLACK);
    tftInstance->setTextColor(TFT_WHITE, TFT_BLACK);
    tftInstance->setTextDatum(MC_DATUM);
    tftInstance->drawString(loadingMsg.c_str(), UI::sx(120), UI::sy(140), UI::font(2));
    tftInstance->drawRect(UI::sx(30), UI::sy(160), UI::sx(180), UI::sy(20), TFT_WHITE);

    HttpClient http;
    http.setTimeout(10000);
    http.setProgressCallback([](int64_t downloaded, int64_t totalLen) {
        if (totalLen > 0) {
            int progressWidth = map((long)downloaded, 0, (long)totalLen, 0, 176);
            tftInstance->fillRect(UI::sx(32), UI::sy(162), UI::sx(progressWidth), UI::sy(16), TFT_GREEN);
        }
    });

    HttpResponse resp = http.downloadToFile(url, destPath);
    if (resp.isOk()) {
        return true;
    }
    dialogMessage = "Error HTTP " + std::to_string(resp.statusCode);
    return false;
}

bool HelpCenterUI::fetchOnlineCategories() {
    std::string tmpPath = "/local/tmp_download/h_idx.json";
    if (!FileSystem::exists("/local/tmp_download/")) FileSystem::mkdir("/local/tmp_download/");

    if (!downloadFile("https://raw.githubusercontent.com/Haris16-code/KryonOS/refs/heads/main/help/index.json", tmpPath, "Fetching Index...")) {
        return false;
    }

    std::string body = FileSystem::readTextFile(tmpPath.c_str());
    if (body.empty()) { dialogMessage = "Failed to open index"; return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    FileSystem::deleteFile(tmpPath.c_str());

    if (err && err != DeserializationError::IncompleteInput) { dialogMessage = "Parse error!"; return false; }

    listCount = 0;
    JsonArray cats = doc["categories"];
    for (JsonObject cat : cats) {
        if (listCount >= 25) break;
        listItems[listCount] = cat["name"].as<std::string>();
        listUrls[listCount] = cat["url"].as<std::string>();
        listCount++;
    }
    
    selectedIndex = 0;
    scrollOffset = 0;
    titleScrollPos = 0;
    listScrollPos = 0;
    lastTitleScrollTime = millis();
    lastListScrollTime = millis();
    uiState = 3;
    draw();
    return true;
}

bool HelpCenterUI::fetchOnlineTopics(const std::string& url) {
    std::string tmpPath = "/local/tmp_download/h_cat.json";
    if (!downloadFile(url, tmpPath, "Loading Topics...")) return false;

    std::string body = FileSystem::readTextFile(tmpPath.c_str());
    if (body.empty()) { dialogMessage = "Failed to open cat"; return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    FileSystem::deleteFile(tmpPath.c_str());

    if (err && err != DeserializationError::IncompleteInput) { dialogMessage = "Parse error!"; return false; }

    listCount = 0;
    JsonArray arts = doc["articles"];
    for (JsonObject art : arts) {
        if (listCount >= 25) break;
        listItems[listCount] = art["title"].as<std::string>();
        listUrls[listCount] = art["url"].as<std::string>();
        listCount++;
    }
    
    selectedIndex = 0;
    scrollOffset = 0;
    titleScrollPos = 0;
    listScrollPos = 0;
    lastTitleScrollTime = millis();
    lastListScrollTime = millis();
    uiState = 4;
    draw();
    return true;
}

bool HelpCenterUI::fetchOnlineContent(const std::string& url, const std::string& title) {
    std::string tmpPath = "/local/tmp_download/h_art.json";
    if (!downloadFile(url, tmpPath, "Loading Article...")) return false;

    std::string body = FileSystem::readTextFile(tmpPath.c_str());
    if (body.empty()) { dialogMessage = "Failed to open art"; return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    FileSystem::deleteFile(tmpPath.c_str());

    if (err && err != DeserializationError::IncompleteInput) { dialogMessage = "Parse error!"; return false; }
    
    currentViewerTitle = title;
    currentViewerContent = doc["content"] | "No content found.";
    viewerScrollOffset = 0;
    titleScrollPos = 0;
    lastTitleScrollTime = millis();
    uiState = 5;
    draw();
    return true;
}

void HelpCenterUI::handleTouch(uint16_t x, uint16_t y) {
    extern int currentState;
    
    if (uiState == 0) { // Main Menu
        if (y >= UI::sy(80) && y <= UI::sy(120)) {
            selectedIndex = 0; draw();
            loadOfflineCategories();
        } else if (y >= UI::sy(140) && y <= UI::sy(180)) {
            selectedIndex = 1; draw();
            if(!fetchOnlineCategories()) {
                uiState = 6; draw();
            }
        } else if (y >= UI::sy(285)) {
            currentState = 0; // Launcher
        }
    }
    else if (uiState == 1 || uiState == 2 || uiState == 3 || uiState == 4) { // Lists
        if (y >= UI::sy(45) && y <= UI::sy(270)) {
            int clickedAbs = scrollOffset + ((y - UI::LIST_Y) / UI::ITEM_H);
            if (clickedAbs < listCount) {
                if (selectedIndex != clickedAbs) {
                    selectedIndex = clickedAbs;
                    listScrollPos = 0;
                    lastListScrollTime = millis();
                    draw(); // Highlight
                }
                
                if (uiState == 1) { // Off Cats -> Off Topics
                    selectedCategoryIndex = selectedIndex;
                    loadOfflineTopics(selectedCategoryIndex);
                } else if (uiState == 2) { // Off Topics -> Viewer
                    loadOfflineContent(selectedCategoryIndex, selectedIndex);
                } else if (uiState == 3) { // On Cats -> On Topics
                    currentCategoryName = listItems[selectedIndex];
                    if(!fetchOnlineTopics(listUrls[selectedIndex])) { uiState = 6; draw(); }
                } else if (uiState == 4) { // On Topics -> Viewer
                    if(!fetchOnlineContent(listUrls[selectedIndex], listItems[selectedIndex])) { uiState = 6; draw(); }
                }
            }
        }
        else if (y >= UI::sy(285)) {
            if (x < UI::sx(70)) { // BACK
                if (uiState == 1 || uiState == 3) { uiState = 0; selectedIndex = 0; draw(); }
                else if (uiState == 2) { loadOfflineCategories(); }
                else if (uiState == 4) { if(!fetchOnlineCategories()) { uiState = 6; draw(); } }
            } else if (x >= UI::sx(70) && x < UI::sx(130)) { // UP
                if (selectedIndex > 0) {
                    selectedIndex--;
                    if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
                    listScrollPos = 0;
                    lastListScrollTime = millis();
                    draw();
                }
            } else if (x >= UI::sx(130) && x < UI::sx(190)) { // SEL
                if (uiState == 1) { selectedCategoryIndex = selectedIndex; loadOfflineTopics(selectedCategoryIndex); }
                else if (uiState == 2) { loadOfflineContent(selectedCategoryIndex, selectedIndex); }
                else if (uiState == 3) { currentCategoryName = listItems[selectedIndex]; if(!fetchOnlineTopics(listUrls[selectedIndex])) { uiState = 6; draw(); } }
                else if (uiState == 4) { if(!fetchOnlineContent(listUrls[selectedIndex], listItems[selectedIndex])) { uiState = 6; draw(); } }
            } else if (x >= UI::sx(190)) { // DN
                if (selectedIndex < listCount - 1) {
                    selectedIndex++;
                    if (selectedIndex >= scrollOffset + UI::ITEMS_PER_PAGE) scrollOffset = selectedIndex - (UI::ITEMS_PER_PAGE - 1);
                    listScrollPos = 0;
                    lastListScrollTime = millis();
                    draw();
                }
            }
        }
    }
    else if (uiState == 5) { // Viewer
        if (y < UI::sy(100)) { // Scroll Up
            if (viewerScrollOffset > 0) { viewerScrollOffset--; draw(); }
        } else if (y > UI::sy(180) && y < UI::sy(270)) { // Scroll Down
            viewerScrollOffset++; draw();
        } else if (y >= UI::sy(285)) {
            if (x < UI::sx(70)) { // BACK
                if (listUrls[0].length() > 0) { uiState = 4; draw(); }
                else { loadOfflineTopics(selectedCategoryIndex); }
            } else if (x >= UI::sx(70) && x < UI::sx(160)) { // UP
                if (viewerScrollOffset > 0) { viewerScrollOffset--; draw(); }
            } else if (x >= UI::sx(160)) { // DN
                viewerScrollOffset++; draw();
            }
        }
    }
    else if (uiState == 6) { // Dialog
        if (y >= UI::sy(220) && y <= UI::sy(250) && x >= UI::sx(85) && x <= UI::sx(155)) {
            uiState = 0;
            draw();
        }
    }
}

void HelpCenterUI::update() {
    if (uiState == 5 && tftInstance) {
        int titleWidth = tftInstance->textWidth(currentViewerTitle, UI::font(2));
        if (titleWidth > UI::sx(200)) {
            unsigned long waitTime = (titleScrollPos == 0) ? 1500 : 350;
            if (millis() - lastTitleScrollTime > waitTime) {
                lastTitleScrollTime = millis();
                int scrollTextLen = currentViewerTitle.length() + 6;
                titleScrollPos = (titleScrollPos + 1) % scrollTextLen;
                
                tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
                tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
                tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
                tftInstance->setTextDatum(ML_DATUM);
                
                std::string scrollText = currentViewerTitle + "      ";
                int len = scrollText.length();
                std::string shifted = "";
                for (int i = 0; i < len; i++) {
                    char c = scrollText[(titleScrollPos + i) % len];
                    if (tftInstance->textWidth(shifted + c, UI::font(2)) > UI::sx(210)) break;
                    shifted += c;
                }
                tftInstance->drawString(shifted.c_str(), UI::sx(10), UI::sy(21), UI::font(2));
            }
        }
    } else if (uiState >= 1 && uiState <= 4 && tftInstance) {
        std::string headerTitle = "";
        if (uiState == 1) headerTitle = "Offline Categories";
        else if (uiState == 2) headerTitle = offCats[selectedCategoryIndex];
        else if (uiState == 3) headerTitle = "Online Categories";
        else if (uiState == 4) headerTitle = currentCategoryName;
        
        int titleWidth = tftInstance->textWidth(headerTitle, UI::font(2));
        if (titleWidth > UI::sx(200)) {
            unsigned long waitTime = (titleScrollPos == 0) ? 1500 : 350;
            if (millis() - lastTitleScrollTime > waitTime) {
                lastTitleScrollTime = millis();
                int scrollTextLen = headerTitle.length() + 6;
                titleScrollPos = (titleScrollPos + 1) % scrollTextLen;
                
                tftInstance->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
                tftInstance->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
                tftInstance->setTextColor(TFT_GREEN, TFT_BLACK);
                tftInstance->setTextDatum(ML_DATUM);
                
                std::string scrollText = headerTitle + "      ";
                int len = scrollText.length();
                std::string shifted = "";
                for (int i = 0; i < len; i++) {
                    char c = scrollText[(titleScrollPos + i) % len];
                    if (tftInstance->textWidth(shifted + c, UI::font(2)) > UI::sx(210)) break;
                    shifted += c;
                }
                tftInstance->drawString(shifted.c_str(), UI::sx(10), UI::sy(21), UI::font(2));
            }
        }
        
        if (listCount > 0 && selectedIndex >= 0 && selectedIndex < listCount) {
            int itemWidth = tftInstance->textWidth(listItems[selectedIndex], UI::font(2));
            if (itemWidth > UI::sx(190)) {
                unsigned long waitTime = (listScrollPos == 0) ? 1500 : 300;
                if (millis() - lastListScrollTime > waitTime) {
                    lastListScrollTime = millis();
                    int scrollTextLen = listItems[selectedIndex].length() + 6;
                    listScrollPos = (listScrollPos + 1) % scrollTextLen;
                    
                    int yPos = 45 + ((selectedIndex - scrollOffset) * 30);
                    if (yPos >= 45 && yPos < 255) {
                        tftInstance->fillRect(UI::sx(10), UI::sy(yPos), UI::sx(220), UI::sy(25), TFT_WHITE);
                        tftInstance->setTextColor(TFT_BLACK, TFT_WHITE);
                        tftInstance->setTextDatum(TL_DATUM);
                        
                        std::string scrollText = listItems[selectedIndex] + "      ";
                        int len = scrollText.length();
                        std::string shifted = "";
                        for (int j = 0; j < len; j++) {
                            char c = scrollText[(listScrollPos + j) % len];
                            if (tftInstance->textWidth(shifted + c, UI::font(2)) > UI::sx(190)) break;
                            shifted += c;
                        }
                        tftInstance->drawString(("> " + shifted).c_str(), UI::sx(15), UI::sy(yPos + 4), UI::font(2));
                    }
                }
            }
        }
    }
}
