#ifndef INSTALLER_UI_H
#define INSTALLER_UI_H

#include "../Display/Display.h"
#include <Arduino.h>
#include "../FileSystem/FileSystem.h"
#include <string>

struct AppMetadata {
    std::string name;
    std::string packageName;
    std::string version;
    int api;
    std::string author;
    std::string type;
    std::string category;
    std::string description;
    std::string changelog;
    std::string folderPath; // Full path to the app folder (e.g. /sd/Downloads/Calculator/)
    bool valid;
};

class InstallerUI {
private:
    static KryonDisplay *tftInstance;
    static FileEntry files[200];
    static int fileCount;
    static std::string currentPath;
    
    // Scrolling states
    static int scrollOffset;
    static int selectedIndex;
    
    static std::string selectedFile;
    static bool showActionDialog;
    
    // App metadata cache for display names on root items
    static std::string displayNames[200];
    static bool isAppPackage[200];

    static void scanSD();
    static void drawFileList();
    static void drawActionDialog();
    static void drawHelp();
    static void drawInstallProgress(int current, int total);
    static AppMetadata parseAppJson(const std::string& folderPath);
    static void performInstall(const std::string& srcFolder, const std::string& appName, bool overwrite);

public:
    static std::string autoInstallPath;
    static void init(KryonDisplay *tft);
    static void draw();
    static void handleTouch(uint16_t x, uint16_t y);
};

#endif // INSTALLER_UI_H
