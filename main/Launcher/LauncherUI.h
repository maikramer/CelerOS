#ifndef LAUNCHER_UI_H
#define LAUNCHER_UI_H

#include "../Boards/Board.h"
#include <Arduino.h>
#include <string>

class LauncherUI {
public:
    static void init(CelerDisplay *tft);
    static void requestRescan();
    static void scanLocalApps();
    static bool needsRescan;

    // Acesso para o LauncherScreen (Kui) e telas novas. Telas de sistema
    // convertidas em JS (Settings, App Store...) sao apps normais com
    // "system": true no app.json — ordenados a frente do grid.
    static int appEntryCount();
    static const std::string& appEntryPath(int i);
    static const std::string& appEntryName(int i);
    static const std::string& appEntryIcon(int i);   // nome em /local/icons ("" = tile)
    static bool appEntryIsSystem(int i);
    static bool appEntryIsFolder(int i);
    static void launchApp(int index);          // executa app (sincrono; W7d = task)
    static int gridCols();
    static int gridRows();
    static int gridTotalEntries();
    static int gridTotalPages();

private:
    static CelerDisplay *tftInstance;
    static void runApp(CelerDisplay *tft, const std::string& path, bool isFolder);

    static std::string appPaths[50];   // Path to app folder or .js file
    static std::string appNames[50];   // Display name (from app.json or filename)
    static std::string appPkg[50];     // packageName do app.json (dedup)
    static std::string appIcons[50];   // nome do icone em /local/icons ("" = sem)
    static bool   appIsFolder[50]; // true = folder app, false = legacy .js
    static bool   appIsSystem[50]; // true = "system": true no app.json
    static int    appOrder[50];    // "order" do app.json (sistema primeiro)
    static int appCount;

    static int totalEntries();    // = appCount (sistema agora sao apps JS)
    static int cols();
    static int rows();
    static int cellsPerPage();
    static int totalPages();
};

#endif // LAUNCHER_UI_H
