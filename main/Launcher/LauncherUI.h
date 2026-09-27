#ifndef LAUNCHER_UI_H
#define LAUNCHER_UI_H

#include "../Boards/Board.h"
#include <Arduino.h>
#include <string>

class LauncherUI {
public:
    static void init(KryonDisplay *tft);
    static void draw();
    static void update();              // relogio/swipe — chamar no loop
    static void handleTouch(uint16_t x, uint16_t y);
    static void requestRescan();
    static void scanLocalApps();
    static bool needsRescan;

private:
    static KryonDisplay *tftInstance;
    static void runApp(KryonDisplay *tft, const std::string& path, bool isFolder);
    static void drawHeader();
    static void drawDots();
    static void drawCell(int entryIndex, int cellX, int cellY, int w, int h);

    static std::string appPaths[50];   // Path to app folder or .js file
    static std::string appNames[50];   // Display name (from app.json or filename)
    static bool   appIsFolder[50]; // true = folder app, false = legacy .js
    static int appCount;
    static int page;              // pagina atual do grid

    // Estado da lista (usado pelo LauncherUI.cpp atual; a migracao para o
    // grid deve remove-los junto com o corpo antigo do .cpp)
    static int selectedIndex;
    static int scrollOffset;

    static int totalEntries();    // 4 apps de sistema + apps do usuario
    static int cols();
    static int rows();
    static int cellsPerPage();
    static int totalPages();

    // estado do swipe
    static bool tracking;
    static int trackStartX;
    static int trackStartY;
    static unsigned long trackStartMs;
    static std::string lastMinute;     // redesenha o relogio na virada do minuto
};

#endif // LAUNCHER_UI_H
