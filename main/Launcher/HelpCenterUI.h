#ifndef HELPCENTERUI_H
#define HELPCENTERUI_H

#include <Arduino.h>
#include <string>
#include "../Display/Display.h"
#include <ArduinoJson.h>

class HelpCenterUI {
public:
    static void init(KryonDisplay *tft);
    static void draw();
    static void update();
    static void handleTouch(uint16_t x, uint16_t y);

private:
    static KryonDisplay *tftInstance;
    
    // 0: Main Menu (Offline vs Online)
    // 1: Offline Categories
    // 2: Offline Topics
    // 3: Online Categories
    // 4: Online Topics
    // 5: Topic Viewer (Offline & Online)
    // 6: Dialog Message
    static int uiState;
    
    static int selectedIndex;
    static int scrollOffset;
    static int listCount;
    
    static std::string listItems[25];
    static std::string listUrls[25]; // Used for online fetching
    
    static int selectedCategoryIndex;
    
    static std::string currentViewerTitle;
    static std::string currentViewerContent;
    static int viewerScrollOffset;
    static int titleScrollPos;
    static unsigned long lastTitleScrollTime;
    
    static std::string currentCategoryName;
    static int listScrollPos;
    static unsigned long lastListScrollTime;
    
    static std::string dialogMessage;

    // Core Drawers
    static void drawMainMenu();
    static void drawList(const std::string& title);
    static void drawViewer();
    static void drawDialog();

    // Offline Data Loaders
    static void loadOfflineCategories();
    static void loadOfflineTopics(int catIdx);
    static void loadOfflineContent(int catIdx, int topicIdx);

    // Online Data Loaders
    static bool fetchOnlineCategories();
    static bool fetchOnlineTopics(const std::string& url);
    static bool fetchOnlineContent(const std::string& url, const std::string& title);
    
    // HTTP Downloader with Loading Bar
    static bool downloadFile(const std::string& url, const std::string& destPath, const std::string& loadingMsg);
};

#endif
