#include "LauncherUI.h"
#include "../Kernel/Core/HarixKernel.h"
#include "../FileSystem/FileSystem.h"
#include "../Display/Layout.h"
#include "../Display/Theme.h"
#include "../Display/Icon.h"
#include "../Utils/StrUtils.h"
#include "../Kernel/TimeManager.h"
#include "../WebManager/WebManager.h"

extern int currentState;

KryonDisplay *LauncherUI::tftInstance = nullptr;
std::string LauncherUI::appPaths[50];
std::string LauncherUI::appNames[50];
bool   LauncherUI::appIsFolder[50];
int LauncherUI::appCount = 0;
int LauncherUI::page = 0;
bool LauncherUI::needsRescan = true;

bool LauncherUI::tracking = false;
int LauncherUI::trackStartX = 0;
int LauncherUI::trackStartY = 0;
unsigned long LauncherUI::trackStartMs = 0;
std::string LauncherUI::lastMinute = "";

void LauncherUI::requestRescan() {
    needsRescan = true;
}

void LauncherUI::init(KryonDisplay *tft) {
    tftInstance = tft;
}

// ---------------------------------------------------------------------------
// Geometria do grid
// ---------------------------------------------------------------------------
int LauncherUI::totalEntries() {
    return 4 + appCount;  // App Store, Installer, Settings, Help + apps
}

int LauncherUI::cols() {
    return (UI::W >= 400) ? 4 : 3;
}

int LauncherUI::rows() {
    int labelH = UI::big ? 26 : 14;
    int cellH = Icon::SIZE + labelH + (UI::big ? 18 : 10);
    int areaH = UI::H - UI::sy(56) - UI::sy(10) - UI::sy(28);
    int r = areaH / cellH;
    return (r < 1) ? 1 : r;
}

int LauncherUI::cellsPerPage() {
    return cols() * rows();
}

int LauncherUI::totalPages() {
    int tp = (totalEntries() + cellsPerPage() - 1) / cellsPerPage();
    return (tp < 1) ? 1 : tp;
}

// ---------------------------------------------------------------------------
// Scan de apps (LittleFS + SD)
// ---------------------------------------------------------------------------
void LauncherUI::scanLocalApps() {
    appCount = 0;

    const char* appDirs[] = { "/local/apps/", "/sd/apps/" };

    for (int d = 0; d < 2; d++) {
        if (!FileSystem::exists(appDirs[d])) continue;

        FileEntry entries[50];
        int count = FileSystem::listDirectory(appDirs[d], entries, 50);

        for (int i = 0; i < count && appCount < 50; i++) {
            if (tftInstance) {
                tftInstance->fillRect(UI::sx(20), UI::sy(200), (i * UI::sx(200)) / count, UI::sy(10), THEME_ACCENT);
            }

            if (entries[i].isDir) {
                std::string appJsonPath = entries[i].path;
                if (!kstr::endsWith(appJsonPath, "/")) appJsonPath += "/";
                appJsonPath += "app.json";

                if (FileSystem::exists(appJsonPath.c_str())) {
                    std::string jsonContent = FileSystem::readTextFile(appJsonPath.c_str());
                    std::string name = FileSystem::parseJsonValue(jsonContent, "name");

                    if (name.length() > 0) {
                        bool duplicate = false;
                        for (int j = 0; j < appCount; j++) {
                            if (appNames[j] == name) { duplicate = true; break; }
                        }
                        if (duplicate) continue;

                        appPaths[appCount] = entries[i].path;
                        appNames[appCount] = name;
                        appIsFolder[appCount] = true;
                        appCount++;
                    }
                }
            } else {
                std::string fname = entries[i].name;
                if (kstr::endsWith(fname, ".js")) {
                    bool duplicate = false;
                    for (int j = 0; j < appCount; j++) {
                        if (appNames[j] == fname) { duplicate = true; break; }
                    }
                    if (duplicate) continue;

                    appPaths[appCount] = entries[i].path;
                    appNames[appCount] = fname;
                    appIsFolder[appCount] = false;
                    appCount++;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------
void LauncherUI::drawHeader() {
    int hdr = UI::sy(56);
    tftInstance->fillRect(0, 0, UI::W, hdr, THEME_CARD);
    tftInstance->fillRect(0, hdr - UI::sy(3), UI::W, UI::sy(3), THEME_ACCENT);

    // Titulo
    tftInstance->setTextColor(THEME_TEXT, THEME_CARD);
    tftInstance->setTextDatum(ML_DATUM);
    tftInstance->drawString("KryonOS", UI::sx(14), hdr / 2, UI::font(4));

    // Status WiFi (icone na tela grande, ponto na classica)
    bool wifi = WebManager::isActive();
    if (UI::big) {
        Icon::draw(tftInstance, wifi ? "wifi_on" : "wifi_off", UI::W - Icon::SIZE - UI::sx(10), (hdr - Icon::SIZE) / 2);
    } else {
        tftInstance->fillCircle(UI::W - UI::sx(10), hdr / 2, UI::sx(3), wifi ? THEME_OK : THEME_TEXT_DIM);
    }

    // Relogio
    tftInstance->setTextColor(THEME_TEXT_DIM, THEME_CARD);
    tftInstance->setTextDatum(MR_DATUM);
    tftInstance->drawString(TimeManager::getFormattedTime().c_str(),
                            UI::W - Icon::SIZE - UI::sx(24), hdr / 2, UI::font(2));
    lastMinute = TimeManager::getFormattedTime();
}

void LauncherUI::drawDots() {
    int tp = totalPages();
    if (tp <= 1) return;
    int dotsY = UI::H - UI::sy(14);
    int spacing = UI::sx(18);
    int totalW = (tp - 1) * spacing;
    int x0 = UI::cx() - totalW / 2;
    for (int i = 0; i < tp; i++) {
        int x = x0 + i * spacing;
        if (i == page) {
            tftInstance->fillCircle(x, dotsY, UI::sx(3), THEME_ACCENT);
        } else {
            tftInstance->fillCircle(x, dotsY, UI::sx(2), THEME_STROKE);
        }
    }
}

void LauncherUI::drawCell(int entryIndex, int cellX, int cellY, int w, int h) {
    const char* sysIcons[4] = { "appstore", "installer", "settings", "help" };
    const char* sysNames[4] = { "App Store", "Installer", "Settings", "Help" };

    std::string label;
    int iconX = cellX + (w - Icon::SIZE) / 2;
    int iconY = cellY + (h - Icon::SIZE - (UI::big ? 26 : 14)) / 2;

    if (entryIndex < 4) {
        Icon::draw(tftInstance, sysIcons[entryIndex], iconX, iconY);
        label = sysNames[entryIndex];
    } else {
        int appIdx = entryIndex - 4;
        Icon::drawAppTile(tftInstance, appNames[appIdx].c_str(), iconX, iconY);
        label = appNames[appIdx];
    }

    // Label truncada para a largura da celula
    int maxW = w - UI::sx(8);
    std::string shown = label;
    while (shown.length() > 1 && tftInstance->textWidth(shown.c_str(), UI::font(2)) > maxW) {
        shown = shown.substr(0, shown.length() - 1);
    }
    if (shown != label && shown.length() > 1) shown += ".";

    tftInstance->setTextColor(THEME_TEXT, THEME_BG);
    tftInstance->setTextDatum(TC_DATUM);
    tftInstance->drawString(shown.c_str(), cellX + w / 2, iconY + Icon::SIZE + (UI::big ? 6 : 3), UI::font(2));
}

// ---- Acesso para o LauncherScreen (Kui) ------------------------------------
int LauncherUI::appEntryCount() { return appCount; }
const std::string& LauncherUI::appEntryPath(int i) { return appPaths[i]; }
const std::string& LauncherUI::appEntryName(int i) { return appNames[i]; }
bool LauncherUI::appEntryIsFolder(int i) { return appIsFolder[i]; }
void LauncherUI::launchApp(int index) {
    if (index < 0 || index >= appCount) return;
    runApp(tftInstance, appPaths[index], appIsFolder[index]);
}
int LauncherUI::gridCols() { return cols(); }
int LauncherUI::gridRows() { return rows(); }
int LauncherUI::gridTotalEntries() { return totalEntries(); }
int LauncherUI::gridTotalPages() { return totalPages(); }

void LauncherUI::draw() {
    if (!tftInstance) return;

    tftInstance->fillScreen(THEME_BG);

    if (needsRescan) {
        scanLocalApps();
        needsRescan = false;
        tftInstance->fillScreen(THEME_BG);
    }

    if (page >= totalPages()) page = totalPages() - 1;
    if (page < 0) page = 0;

    drawHeader();
    drawDots();

    int hdr = UI::sy(56);
    int gridTop = hdr + UI::sy(10);
    int areaH = UI::H - gridTop - UI::sy(28);
    int c = cols(), r = rows();
    int cellW = UI::W / c;
    int cellH = areaH / r;
    int gridLeft = (UI::W - c * cellW) / 2;

    for (int cell = 0; cell < c * r; cell++) {
        int entry = page * c * r + cell;
        if (entry >= totalEntries()) break;
        drawCell(entry, gridLeft + (cell % c) * cellW, gridTop + (cell / c) * cellH, cellW, cellH);
    }
}

// ---------------------------------------------------------------------------
// Interacao: o toque inicial e registrado; no release, update() decide entre
// tap (celula / dot) e swipe (troca de pagina).
// ---------------------------------------------------------------------------
void LauncherUI::handleTouch(uint16_t x, uint16_t y) {
    if (!tracking) {
        tracking = true;
        trackStartX = x;
        trackStartY = y;
        trackStartMs = millis();
    }
}

void LauncherUI::update() {
    if (!tftInstance) return;

    // Atualiza header na virada do minuto ou mudanca de estado do WiFi
    static bool lastWifi = false;
    bool wifi = WebManager::isActive();
    std::string now = TimeManager::getFormattedTime();
    if (now != lastMinute || wifi != lastWifi) {
        drawHeader();
        lastWifi = wifi;
    }

    uint16_t x, y;
    bool touched = tftInstance->getTouch(&x, &y);
    if (touched) {
        if (!tracking) {
            tracking = true;
            trackStartX = x;
            trackStartY = y;
            trackStartMs = millis();
        }
        return;  // aguarda o release para decidir
    }
    if (!tracking) return;
    tracking = false;

    long dx = (long)x - trackStartX;
    long dy = (long)y - trackStartY;
    unsigned long dur = millis() - trackStartMs;

    // Swipe horizontal: troca de pagina
    if (labs(dx) > UI::sx(60) && labs(dx) > labs(dy) && dur < 600) {
        int tp = totalPages();
        if (dx < 0) page = (page + 1) % tp;
        else page = (page - 1 + tp) % tp;
        draw();
        return;
    }

    // Tap nos dots de paginacao
    int tp = totalPages();
    if (tp > 1 && trackStartY >= UI::H - UI::sy(24)) {
        int spacing = UI::sx(18);
        int x0 = UI::cx() - (tp - 1) * spacing / 2;
        for (int i = 0; i < tp; i++) {
            if (labs((long)trackStartX - (x0 + i * spacing)) < spacing / 2) {
                if (page != i) { page = i; draw(); }
                return;
            }
        }
        return;
    }

    // Tap em celula do grid
    int hdr = UI::sy(56);
    int gridTop = hdr + UI::sy(10);
    if (trackStartY < gridTop) return;  // header nao e clicavel
    int areaH = UI::H - gridTop - UI::sy(28);
    int c = cols(), r = rows();
    int cellW = UI::W / c;
    int cellH = areaH / r;
    int col = trackStartX / cellW;
    int row = (trackStartY - gridTop) / cellH;
    if (row < 0 || row >= r) return;
    int entry = page * c * r + row * c + col;
    if (entry >= totalEntries()) return;

    if (entry == 0) currentState = 13;      // STATE_APP_STORE
    else if (entry == 1) currentState = 3;  // STATE_INSTALLER
    else if (entry == 2) currentState = 1;  // STATE_SETTINGS
    else if (entry == 3) currentState = 14; // STATE_HELP_CENTER
    else {
        int appIdx = entry - 4;
        runApp(tftInstance, appPaths[appIdx], appIsFolder[appIdx]);
    }
}

// ---------------------------------------------------------------------------
// Execucao de app JS
// ---------------------------------------------------------------------------
void LauncherUI::runApp(KryonDisplay* tft, const std::string& path, bool isFolder) {
    extern int currentState;
    currentState = 2; // STATE_RUN_APP

    tft->fillScreen(TFT_BLACK);
    tft->setTextDatum(TL_DATUM);

    std::string filePath;
    if (isFolder) {
        filePath = path;
        if (!kstr::endsWith(filePath, "/")) filePath += "/";
        filePath += "main.js";
    } else {
        filePath = path;
    }

    HarixKernel::runFile(filePath.c_str());

    // Botao de saida (canto superior direito)
    tft->fillRoundRect(UI::exitX(), UI::exitY(), UI::exitW(), UI::exitH(), UI::sx(5), THEME_ERR);
    tft->setTextColor(THEME_TEXT, THEME_ERR);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("X", UI::exitX() + UI::exitW() / 2, UI::exitY() + UI::exitH() / 2, UI::font(2));
}
