#ifndef APP_STORE_UI_H
#define APP_STORE_UI_H

#include <Arduino.h>
#include <string>
#include "../Boards/Board.h"

struct AppStoreItem {
    std::string id;
    std::string name;
    std::string metaUrl;
    std::string appUrl;
    std::string description;
    std::string author;
    std::string version;
};

class AppStoreUI {
public:
    static void init(KryonDisplay *tft);
    static void draw();
    static void handleTouch(uint16_t x, uint16_t y);

    // States
    // 0: Categories List
    // 1: App List
    // 2: App Info
    // 3: Installing / Downloading
    // 4: Error / Success Dialog
    static int storeState;
    static bool isUpdateMode;
    
private:
    static KryonDisplay *tftInstance;
    
    // UI State
    static int selectedIndex;
    static int scrollOffset;
    
    // Categories
    static std::string categoryNames[20];
    static std::string categoryUrls[20];
    static int categoryCount;
    
    // Apps
    static AppStoreItem currentApps[50];
    static int currentAppCount;
    static std::string currentCategoryName;
    static int selectedAppIndex;
    
    // Updates
    static AppStoreItem updateApps[50];
    static int updateAppCount;
    
    // Downloading logic
    static std::string dialogMessage;
    static bool downloadInProgress;
    
    // Methods
    static void drawCategories();
    static void drawAppList();
    static void drawAppInfo();
    static void drawDialog();
    
    static bool fetchCategories();
    static bool fetchCategoryApps(const std::string& url);
    static bool checkUpdates();
    static int compareVersions(const std::string& v1, const std::string& v2);
    static bool downloadFile(const std::string& url, const std::string& destPath, const std::string& loadingMsg);
    static bool fetchJson(const std::string& url, const std::string& loadingMsg, std::string& outBody);
    static void performInstall(int appIdx);
};

#endif // APP_STORE_UI_H
