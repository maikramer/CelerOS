#include "LauncherUI.h"
#include "../Utils/AppPerms.h"  // parsePermissions/PERM_* (testavel no host)
#include "../Utils/AppGrants.h"
#include "../USBDevice/LogSink.h"
#include "../Runtime/JSBindings.h"
#include "../Kernel/AppRunner.h"
#include "../Kernel/Core/CelerKernel.h"
#include "../FileSystem/FileSystem.h"
#include "../Display/Layout.h"
#include "../Display/Theme.h"
#include "../UI/Kui.h"
#include "../Display/Icon.h"
#include "../Boards/Board.h"
#include "../Utils/StrUtils.h"
#include "../Utils/CelerSettings.h"
#include "../Display/Backlight.h"
#include "../Kernel/Alarms.h"

CelerDisplay *LauncherUI::tftInstance = nullptr;
std::string LauncherUI::appPaths[50];
std::string LauncherUI::appNames[50];
std::string LauncherUI::appPkg[50];
uint32_t LauncherUI::appPerms[50];
std::string LauncherUI::appIcons[50];
bool   LauncherUI::appIsFolder[50];
bool   LauncherUI::appIsSystem[50];
bool   LauncherUI::appTopbar[50];
int    LauncherUI::appOrder[50];
int LauncherUI::appCount = 0;
bool LauncherUI::needsRescan = true;


void LauncherUI::requestRescan() {
    needsRescan = true;
}

namespace {
portMUX_TYPE s_launchMux = portMUX_INITIALIZER_UNLOCKED;
std::string s_launchReq;   // protegido por s_launchMux
volatile bool s_launchPending = false;
}  // namespace

void LauncherUI::requestLaunch(const std::string& pathOrName) {
    std::string copy = pathOrName;  // aloca fora da secao critica
    portENTER_CRITICAL(&s_launchMux);
    s_launchReq.swap(copy);
    s_launchPending = true;
    portEXIT_CRITICAL(&s_launchMux);
}

bool LauncherUI::takeLaunchRequest(std::string& out) {
    if (!s_launchPending) return false;
    std::string got;
    portENTER_CRITICAL(&s_launchMux);
    got.swap(s_launchReq);
    s_launchPending = false;
    portEXIT_CRITICAL(&s_launchMux);
    out.swap(got);
    return true;
}

namespace {
portMUX_TYPE s_appExitMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_appExitPending = false;   // protegido por s_appExitMux
volatile uint32_t s_appExitAt = 0;        // tick do pedido (expira sozinho)
}  // namespace

void LauncherUI::requestAppExit() {
    portENTER_CRITICAL(&s_appExitMux);
    s_appExitPending = true;
    s_appExitAt = xTaskGetTickCount();
    portEXIT_CRITICAL(&s_appExitMux);
}

bool LauncherUI::consumeAppExitRequest() {
    bool out = false;
    portENTER_CRITICAL(&s_appExitMux);
    if (s_appExitPending) {
        s_appExitPending = false;
        // Pedido sem app em execucao nao pode sobreviver e matar o PROXIMO
        // app na primeira chamada de delay/getTouch: expira em 2s.
        out = (xTaskGetTickCount() - s_appExitAt) < pdMS_TO_TICKS(2000);
    }
    portEXIT_CRITICAL(&s_appExitMux);
    return out;
}

int LauncherUI::findEntry(const std::string& pathOrName) {
    std::string want = pathOrName;
    while (want.size() > 1 && want.back() == '/') want.pop_back();
    for (int i = 0; i < appCount; i++) {
        std::string p = appPaths[i];
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        if (p == want || appNames[i] == want || appPkg[i] == want) return i;
    }
    return -1;
}

void LauncherUI::init(CelerDisplay *tft) {
    tftInstance = tft;
}

// ---------------------------------------------------------------------------
// Geometria do grid
// ---------------------------------------------------------------------------
int LauncherUI::totalEntries() {
    return appCount;  // telas de sistema viraram apps JS ("system": true)
}

int LauncherUI::cols() {
    // 4 colunas a partir de 300px (SmartDisplay 480 e CYD landscape 320,
    // celula de 80px com icone de 48); retrato estreito fica com 3
    return (UI::W >= 300) ? 4 : 3;
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

std::string LauncherUI::homeTarget() {
    // Precedencia: /local/autostart.txt (qualquer placa) > profile.homeApp
    // (a casa nativa da placa — ex.: a cara do cao robotico, o watchface).
    std::string who;
    if (FileSystem::exists("/local/autostart.txt")) {
        who = FileSystem::readTextFile("/local/autostart.txt");
        while (!who.empty() && (who.back() == '\n' || who.back() == '\r' || who.back() == ' '))
            who.pop_back();
    } else if (Board::profile().homeApp) {
        who = Board::profile().homeApp;
    }
    return who;
}

bool LauncherUI::launchHome() {
    const std::string who = homeTarget();
    if (who.empty()) return false;
    if (findEntry(who) < 0) return false;  // app sumiu: fica no launcher
    requestLaunch(who);
    return true;
}

void LauncherUI::applyAutostart() {
    launchHome();
}

void LauncherUI::idleHomeTick() {
    // So placas com casa nativa (watch, cao): o launcher ocioso volta para
    // ela — no watch e o que mantem o AOD (so existe com o watchface aberto).
    if (Board::profile().homeApp == nullptr) return;
    static uint32_t s_checkAt = 0;
    static int s_idleS = -1;
    const uint32_t now = millis();
    if (now - s_checkAt < 1000) return;
    s_checkAt = now;
    if (s_idleS < 0 || (now / 1000) % 30 == 0) {  // releitura barata do ajuste
        s_idleS = atoi(CelerSettings::get("home_idle_s", "30").c_str());
    }
    if (s_idleS <= 0) return;  // 0 = desligado
    if (Alarms::ringing()) return;  // a AlarmScreen fica por cima ate parar
    // telas que travam o voltar (alarme, codigo de pareamento, chamada)
    kui::Screen* top = kui::Navigator::top();
    if (top != nullptr && !top->allowsBackGesture()) return;
    if (now - Backlight::lastActivity() < (uint32_t)s_idleS * 1000UL) return;
    if (!launchHome()) return;
    kui::Navigator::home();  // o app volta para a raiz do launcher ao sair
}

void LauncherUI::scanLocalApps() {
    appCount = 0;
    Icon::invalidateFileIcons();  // app reinstalado pode ter trocado a arte

    const char* appDirs[] = { "/local/apps/", "/sd/apps/" };

    for (int d = 0; d < 2; d++) {
        if (!FileSystem::exists(appDirs[d])) continue;

        FileEntry entries[50];
        int count = FileSystem::listDirectory(appDirs[d], entries, 50);
        if (count >= 50) {
            // listDirectory para em 50: o resto da pasta nao aparece
            celer_log_printf("[launcher] %s tem 50+ entradas: as excedentes nao aparecem\n", appDirs[d]);
        }

        for (int i = 0; i < count && appCount < 50; i++) {
            if (tftInstance) {
                tftInstance->fillRect(UI::sx(20), UI::sy(200), (i * UI::sx(200)) / count, UI::sy(10), THEME_ACCENT);
            }

            std::string name, pkg, icon;
            bool isFolder = false, system = false, topbar = false;
            int order = 100;
            uint32_t appEntryPerms = 0xFFFFFFFFu;  // .js avulso: sem app.json = tudo

            if (entries[i].isDir) {
                std::string appJsonPath = entries[i].path;
                if (!kstr::endsWith(appJsonPath, "/")) appJsonPath += "/";
                appJsonPath += "app.json";

                if (!FileSystem::exists(appJsonPath.c_str())) continue;

                std::string jsonContent = FileSystem::readTextFile(appJsonPath.c_str());
                appEntryPerms = celer::parsePermissions(jsonContent);
                name = FileSystem::parseJsonValue(jsonContent, "name");
                if (name.length() == 0) continue;

                pkg    = FileSystem::parseJsonValue(jsonContent, "packageName");
                // packageName vira caminho (/local/data/<pkg>) e identidade
                // (Storage, concessoes): so [A-Za-z0-9._-], sem "..", ate 64.
                // Invalido = ignorado (dedup/identidade pelo nome).
                {
                    bool okPkg = pkg.size() <= 64 && pkg.find("..") == std::string::npos;
                    for (char ch : pkg) {
                        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                              ch == '.' || ch == '_' || ch == '-')) {
                            okPkg = false;
                        }
                    }
                    if (!okPkg) {
                        celer_log_printf("[launcher] packageName invalido ignorado: %s\n", pkg.c_str());
                        pkg.clear();
                    }
                }
                icon   = FileSystem::parseJsonValue(jsonContent, "icon");
                system = (FileSystem::parseJsonValue(jsonContent, "system") == "true");
                topbar = (FileSystem::parseJsonValue(jsonContent, "topbar") == "true");
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

            // tela pequena: icone reduzido fica pronto no cache em flash
            // (o scan do boot roda com o heap cheio, antes do WiFi)
            Icon::prewarm(icon.c_str());

            appPaths[appCount]    = entries[i].path;
            appNames[appCount]    = name;
            appPkg[appCount]      = pkg;
            appIcons[appCount]    = icon;
            appIsFolder[appCount] = isFolder;
            appIsSystem[appCount] = system;
            appTopbar[appCount]   = topbar;
            appOrder[appCount]    = order;
            appPerms[appCount]   = appEntryPerms;
            appCount++;
        }
    }

    if (appCount >= 50) {
        celer_log_println("[launcher] limite de 50 apps atingido: apps excedentes ocultos");
        kui::Navigator::toast("Limite de 50 apps: alguns ficaram ocultos", THEME_WARN);
    }

    // Consentimento chegou com este firmware: o que ja estava instalado no
    // primeiro scan (apps de fabrica + os que o usuario ja usava) leva o que
    // declara — so app NOVO passa pelo dialogo. SD ausente agora = os apps
    // de la pedem consentimento quando aparecerem (lado seguro).
    if (!AppGrants::migrated()) {
        for (int i = 0; i < appCount; i++) {
            const std::string& id = appPkg[i].length() > 0 ? appPkg[i] : appPaths[i];
            AppGrants::grant(id, appPaths[i], appPerms[i]);
        }
        AppGrants::setMigrated();
        celer_log_printf("[perm] migracao: %d apps instalados concedidos\n", appCount);
    }

    // "system": true no app.json ordena o app a frente e o protege da
    // remocao pelo launcher — so vale com a capability "system" CONCEDIDA
    // (antes qualquer app da loja se declarava de sistema e ficava
    // impossivel de remover pelo grid)
    for (int i = 0; i < appCount; i++) {
        if (!appIsSystem[i]) continue;
        uint32_t granted = 0;
        const std::string& id = appPkg[i].length() > 0 ? appPkg[i] : appPaths[i];
        if (!AppGrants::lookup(id, appPaths[i], &granted) || !(granted & celer::PERM_SYSTEM)) {
            appIsSystem[i] = false;
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
        bool sTopbar = appTopbar[best];
        int sOrder = appOrder[best];
        uint32_t sPerms = appPerms[best];
        for (int k = best; k > i; k--) {
            appPaths[k]    = appPaths[k - 1];
            appNames[k]    = appNames[k - 1];
            appPkg[k]      = appPkg[k - 1];
            appIcons[k]    = appIcons[k - 1];
            appIsFolder[k] = appIsFolder[k - 1];
            appIsSystem[k] = appIsSystem[k - 1];
            appTopbar[k]   = appTopbar[k - 1];
            appOrder[k]    = appOrder[k - 1];
            appPerms[k]    = appPerms[k - 1];
        }
        appPaths[i]    = sPath;
        appNames[i]    = sName;
        appPkg[i]      = sPkg;
        appIcons[i]    = sIcon;
        appIsFolder[i] = sFolder;
        appIsSystem[i] = sSystem;
        appTopbar[i]   = sTopbar;
        appOrder[i]    = sOrder;
        appPerms[i]    = sPerms;
    }
}

// ---- Acesso para o LauncherScreen (Kui) ------------------------------------
int LauncherUI::appEntryCount() { return appCount; }
const std::string& LauncherUI::appEntryPath(int i) { return appPaths[i]; }
const std::string& LauncherUI::appEntryName(int i) { return appNames[i]; }
const std::string& LauncherUI::appEntryIcon(int i) { return appIcons[i]; }
bool LauncherUI::appEntryIsSystem(int i) { return appIsSystem[i]; }
bool LauncherUI::appEntryTopbar(int i) { return appTopbar[i]; }
bool LauncherUI::appEntryIsFolder(int i) { return appIsFolder[i]; }
uint32_t LauncherUI::appEntryPerms(int i) { return appPerms[i]; }
const std::string& LauncherUI::appEntryPkg(int i) { return appPkg[i]; }
// Bits que o consentimento conhece (PERM_ALL do app.json sem o campo vira
// estes cinco)
static constexpr uint32_t kKnownPerms =
    celer::PERM_FS | celer::PERM_NET | celer::PERM_GPIO | celer::PERM_SYSTEM | celer::PERM_MIC;

static const std::string& grantId(const std::string& pkg, const std::string& path) {
    return pkg.length() > 0 ? pkg : path;
}

// Efetivas = declaradas & concedidas: um app que pulou o dialogo (caminho
// novo de lancamento, concessao revogada) roda SEM o que falta
static uint32_t effectivePerms(const std::string& pkg, const std::string& path, uint32_t declared) {
    uint32_t granted = 0;
    if (!AppGrants::lookup(grantId(pkg, path), path, &granted)) granted = 0;
    return declared & granted & kKnownPerms;
}

uint32_t LauncherUI::appEntryMissingPerms(int i) {
    if (i < 0 || i >= appCount) return 0;
    uint32_t granted = 0;
    if (!AppGrants::lookup(grantId(appPkg[i], appPaths[i]), appPaths[i], &granted)) granted = 0;
    return appPerms[i] & ~granted & kKnownPerms;
}

void LauncherUI::grantEntry(int i) {
    if (i < 0 || i >= appCount) return;
    AppGrants::grant(grantId(appPkg[i], appPaths[i]), appPaths[i], appPerms[i] & kKnownPerms);
}

bool LauncherUI::uninstallEntry(int i) {
    if (i < 0 || i >= appCount) return false;
    const std::string path = appPaths[i];
    const std::string pkg = appPkg[i];
    bool ok = appIsFolder[i] ? FileSystem::removeTree(path.c_str()) : FileSystem::deleteFile(path.c_str());
    if (ok) {
        // "Apaga o app e os dados": antes so a pasta saia — o Storage (NVS)
        // e a pasta privada /local/data/<pkg> ficavam para tras
        if (appIsFolder[i] && pkg.length() > 0) {
            JSBindings::storageClearPackage(pkg);
            std::string dataDir = "/local/data/" + pkg;
            if (FileSystem::isDirectory(dataDir.c_str())) FileSystem::removeTree(dataDir.c_str());
        }
        AppGrants::revoke(grantId(pkg, path));
    }
    return ok;
}

void LauncherUI::launchApp(int index) {
    if (index < 0 || index >= appCount) return;
    runApp(tftInstance, appPaths[index], appIsFolder[index], appTopbar[index],
           appPkg[index], effectivePerms(appPkg[index], appPaths[index], appPerms[index]));
}

bool LauncherUI::launchAppAsync(int index) {
    // Caminho F3 (CELEROS_APP_TASK): resolve o app igual ao launchApp e
    // inicia na task propria. Retorna false se o build e sincrono ou a task
    // nao foi criada — o chamador cai no caminho classico.
#ifdef CELEROS_APP_TASK
    if (index < 0 || index >= appCount) return false;
    std::string filePath;
    std::string title;
    resolveApp(appPaths[index], appIsFolder[index], filePath, title);
    return AppRunner::start(filePath, title, appTopbar[index],
                            appPkg[index], effectivePerms(appPkg[index], appPaths[index], appPerms[index]));
#else
    (void)index;
    return false;
#endif
}
int LauncherUI::gridCols() { return cols(); }
int LauncherUI::gridRows() { return rows(); }
int LauncherUI::gridTotalEntries() { return totalEntries(); }
int LauncherUI::gridTotalPages() { return totalPages(); }

// ---------------------------------------------------------------------------
// Execucao de app JS
// ---------------------------------------------------------------------------
void LauncherUI::resolveApp(const std::string& path, bool isFolder,
                            std::string& filePath, std::string& title) {
    if (isFolder) {
        filePath = path;
        if (!kstr::endsWith(filePath, "/")) filePath += "/";
        filePath += "main.js";
    } else {
        filePath = path;
    }

    // Titulo para a topbar do sistema: nome da pasta do app (ou do .js avulso)
    title = path;
    while (title.size() > 1 && title.back() == '/') title.pop_back();
    size_t slash = title.find_last_of('/');
    title = (slash == std::string::npos) ? title : title.substr(slash + 1);
    if (!isFolder && kstr::endsWith(title, ".js")) title.resize(title.size() - 3);
}

void LauncherUI::runApp(CelerDisplay* tft, const std::string& path, bool isFolder, bool topbarFixed,
                        const std::string& appPkg, uint32_t perms) {
    tft->fillScreen(TFT_BLACK);
    tft->setTextDatum(TL_DATUM);

    std::string filePath, title;
    resolveApp(path, isFolder, filePath, title);

    // "topbar": true no app.json fixa a faixa (canvas abaixo dela); ausente
    // deixa a faixa retratil com o app em tela cheia
    CelerKernel::runFile(filePath.c_str(), title.c_str(), topbarFixed,
                         appPkg.c_str(), perms);
    // (o "X" que era desenhado aqui aparecia DEPOIS do app sair e era
    // coberto na hora pelo launcher — removido)
}
