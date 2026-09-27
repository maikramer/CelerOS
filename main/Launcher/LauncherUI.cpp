#include "LauncherUI.h"
#include "../Kernel/Core/HarixKernel.h"
#include "../FileSystem/FileSystem.h"
#include "../Display/Layout.h"
#include "../Display/Theme.h"
#include "../Display/Icon.h"
#include "../Utils/StrUtils.h"

KryonDisplay *LauncherUI::tftInstance = nullptr;
std::string LauncherUI::appPaths[50];
std::string LauncherUI::appNames[50];
std::string LauncherUI::appPkg[50];
std::string LauncherUI::appIcons[50];
bool   LauncherUI::appIsFolder[50];
bool   LauncherUI::appIsSystem[50];
int    LauncherUI::appOrder[50];
int LauncherUI::appCount = 0;
bool LauncherUI::needsRescan = true;


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
    return appCount;  // telas de sistema viraram apps JS ("system": true)
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
    Icon::invalidateFileIcons();  // app reinstalado pode ter trocado a arte

    const char* appDirs[] = { "/local/apps/", "/sd/apps/" };

    for (int d = 0; d < 2; d++) {
        if (!FileSystem::exists(appDirs[d])) continue;

        FileEntry entries[50];
        int count = FileSystem::listDirectory(appDirs[d], entries, 50);

        for (int i = 0; i < count && appCount < 50; i++) {
            if (tftInstance) {
                tftInstance->fillRect(UI::sx(20), UI::sy(200), (i * UI::sx(200)) / count, UI::sy(10), THEME_ACCENT);
            }

            std::string name, pkg, icon;
            bool isFolder = false, system = false;
            int order = 100;

            if (entries[i].isDir) {
                std::string appJsonPath = entries[i].path;
                if (!kstr::endsWith(appJsonPath, "/")) appJsonPath += "/";
                appJsonPath += "app.json";

                if (!FileSystem::exists(appJsonPath.c_str())) continue;

                std::string jsonContent = FileSystem::readTextFile(appJsonPath.c_str());
                name = FileSystem::parseJsonValue(jsonContent, "name");
                if (name.length() == 0) continue;

                pkg    = FileSystem::parseJsonValue(jsonContent, "packageName");
                icon   = FileSystem::parseJsonValue(jsonContent, "icon");
                system = (FileSystem::parseJsonValue(jsonContent, "system") == "true");
                order  = atoi(FileSystem::parseJsonValue(jsonContent, "order").c_str());
                if (order <= 0) order = 100;
                isFolder = true;

                // icone do pacote na pasta do app tem preferencia: cada app
                // carrega a propria arte onde for instalado (icon.png; o
                // .bin v1/v2 segue como legado)
                std::string pkgDir = appJsonPath.substr(0, appJsonPath.length() - strlen("app.json"));
                std::string pkgIcon = pkgDir + "icon.png";
                if (!FileSystem::exists(pkgIcon.c_str())) pkgIcon = pkgDir + "icon.bin";
                if (FileSystem::exists(pkgIcon.c_str())) icon = pkgIcon;
            } else {
                std::string fname = entries[i].name;
                if (!kstr::endsWith(fname, ".js")) continue;
                name = fname;
                pkg = fname;  // apps .js avulsos deduplicam pelo nome do arquivo
            }

            // Dedup por packageName (fallback nome). /local e varrido antes
            // de /sd, entao a copia local nao e sombreada pela do cartao.
            const std::string key = pkg.length() > 0 ? pkg : name;
            bool duplicate = false;
            for (int j = 0; j < appCount; j++) {
                const std::string& kOther = appPkg[j].length() > 0 ? appPkg[j] : appNames[j];
                if (kOther == key) { duplicate = true; break; }
            }
            if (duplicate) continue;

            appPaths[appCount]    = entries[i].path;
            appNames[appCount]    = name;
            appPkg[appCount]      = pkg;
            appIcons[appCount]    = icon;
            appIsFolder[appCount] = isFolder;
            appIsSystem[appCount] = system;
            appOrder[appCount]    = order;
            appCount++;
        }
    }

    // Ordem do grid: apps de sistema primeiro pelo campo "order" do
    // app.json; apps comuns depois, na ordem de descobertura. Selection sort
    // com rotacao — estavel em empates.
    auto sortKey = [](bool sys, int ord) { return sys ? ord : 1000; };
    for (int i = 0; i < appCount; i++) {
        int best = i;
        for (int j = i + 1; j < appCount; j++) {
            if (sortKey(appIsSystem[j], appOrder[j]) < sortKey(appIsSystem[best], appOrder[best])) best = j;
        }
        if (best == i) continue;

        std::string sPath = appPaths[best], sName = appNames[best],
                    sPkg = appPkg[best], sIcon = appIcons[best];
        bool sFolder = appIsFolder[best], sSystem = appIsSystem[best];
        int sOrder = appOrder[best];
        for (int k = best; k > i; k--) {
            appPaths[k]    = appPaths[k - 1];
            appNames[k]    = appNames[k - 1];
            appPkg[k]      = appPkg[k - 1];
            appIcons[k]    = appIcons[k - 1];
            appIsFolder[k] = appIsFolder[k - 1];
            appIsSystem[k] = appIsSystem[k - 1];
            appOrder[k]    = appOrder[k - 1];
        }
        appPaths[i]    = sPath;
        appNames[i]    = sName;
        appPkg[i]      = sPkg;
        appIcons[i]    = sIcon;
        appIsFolder[i] = sFolder;
        appIsSystem[i] = sSystem;
        appOrder[i]    = sOrder;
    }
}

// ---- Acesso para o LauncherScreen (Kui) ------------------------------------
int LauncherUI::appEntryCount() { return appCount; }
const std::string& LauncherUI::appEntryPath(int i) { return appPaths[i]; }
const std::string& LauncherUI::appEntryName(int i) { return appNames[i]; }
const std::string& LauncherUI::appEntryIcon(int i) { return appIcons[i]; }
bool LauncherUI::appEntryIsSystem(int i) { return appIsSystem[i]; }
bool LauncherUI::appEntryIsFolder(int i) { return appIsFolder[i]; }
void LauncherUI::launchApp(int index) {
    if (index < 0 || index >= appCount) return;
    runApp(tftInstance, appPaths[index], appIsFolder[index]);
}
int LauncherUI::gridCols() { return cols(); }
int LauncherUI::gridRows() { return rows(); }
int LauncherUI::gridTotalEntries() { return totalEntries(); }
int LauncherUI::gridTotalPages() { return totalPages(); }

// ---------------------------------------------------------------------------
// Execucao de app JS
// ---------------------------------------------------------------------------
void LauncherUI::runApp(KryonDisplay* tft, const std::string& path, bool isFolder) {
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
