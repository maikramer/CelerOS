#ifndef LAUNCHER_UI_H
#define LAUNCHER_UI_H

#include "../Boards/Board.h"
#include <Arduino.h>
#include <string>
#include <cstdint>

class LauncherUI {
public:
    static void init(CelerDisplay *tft);
    static void requestRescan();
    // Pedido de abrir app vindo de outra task (shell "run"): consumido pelo
    // LauncherScreen no loop da UI. Aceita caminho da pasta/arquivo ou nome.
    static void requestLaunch(const std::string& pathOrName);
    static bool takeLaunchRequest(std::string& out);
    // Pedido de encerrar o app em execucao (shell "exit" / dev loop do SDK):
    // consumido pelos pontos de espera do runtime JS (delay/getTouch/keypadPoll)
    // via throwAppExit — mesma saida limpa do X da topbar.
    static void requestAppExit();
    static bool consumeAppExitRequest();
    static int findEntry(const std::string& pathOrName);  // -1 se nao achar
    static void scanLocalApps();
    // Abre sozinho apos o boot o app cujo pacote/caminho esta em
    // /local/autostart.txt (ex.: a cara do cao robotico). Sem o arquivo,
    // nada acontece. O launcher continua acessivel pelo voltar do topbar.
    static void applyAutostart();
    static bool needsRescan;

    // Acesso para o LauncherScreen (Kui) e telas novas. Telas de sistema
    // convertidas em JS (Settings, App Store...) sao apps normais com
    // "system": true no app.json — ordenados a frente do grid.
    static int appEntryCount();
    static const std::string& appEntryPath(int i);
    static const std::string& appEntryName(int i);
    static const std::string& appEntryIcon(int i);   // nome em /local/icons ("" = tile)
    static bool appEntryIsSystem(int i);
    static bool appEntryTopbar(int i);
    static bool appEntryIsFolder(int i);
    static uint32_t appEntryPerms(int i);
    static const std::string& appEntryPkg(int i);
    static void launchApp(int index);          // executa app (sincrono)
    // F3 (CELEROS_APP_TASK): inicia na task propria; false = use launchApp
    static bool launchAppAsync(int index);
    static int gridCols();
    static int gridRows();
    static int gridTotalEntries();
    static int gridTotalPages();

private:
    static CelerDisplay *tftInstance;
    static void runApp(CelerDisplay *tft, const std::string& path, bool isFolder, bool topbarFixed,
                       const std::string& appPkg, uint32_t perms);
    static void resolveApp(const std::string& path, bool isFolder,
                           std::string& filePath, std::string& title);

    static std::string appPaths[50];   // Path to app folder or .js file
    static std::string appNames[50];   // Display name (from app.json or filename)
    static std::string appPkg[50];     // packageName do app.json (dedup)
    static uint32_t appPerms[50];      // capabilities declaradas (F4)
    static std::string appIcons[50];   // nome do icone em /local/icons ("" = sem)
    static bool   appIsFolder[50]; // true = folder app, false = legacy .js
    static bool   appIsSystem[50]; // true = "system": true no app.json
    static bool   appTopbar[50];   // true = "topbar": true (faixa fixa; ausente = retratil)
    static int    appOrder[50];    // "order" do app.json (sistema primeiro)
    static int appCount;

    static int totalEntries();    // = appCount (sistema agora sao apps JS)
    static int cols();
    static int rows();
    static int cellsPerPage();
    static int totalPages();
};

#endif // LAUNCHER_UI_H
