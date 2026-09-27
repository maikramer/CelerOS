#include "Screens.h"
#include "LauncherUI.h"
#include "../Display/Icon.h"
#include "../Kernel/TimeManager.h"
#include "../FileSystem/FileSystem.h"
#include "../Settings/SettingsUI.h"
#include "../Launcher/InstallerUI.h"
#include "../Launcher/AppStoreUI.h"
#include "../Launcher/HelpCenterUI.h"
#include "../WebServerApp/WebServerAppUI.h"
#include "../WebManager/WebManager.h"
#include "../WebManager/WifiSetupPortal.h"
#include "../Boards/Board.h"
#include "../Utils/StrUtils.h"

#include <Arduino.h>

using namespace kui;

// Estados antigos (main.cpp) — so para a transicao LegacyScreen
#define LEGACY_SETTINGS 1
#define LEGACY_WEB_APP 5
#define LEGACY_SETTINGS_WIFI 6
#define LEGACY_SETTINGS_ABOUT 7
#define LEGACY_SETTINGS_APPS 8
#define LEGACY_SETTINGS_TIME 9
#define LEGACY_SETTINGS_TIME_MANUAL 10
#define LEGACY_UPDATER_BOOT 11
#define LEGACY_APP_STORE 13
#define LEGACY_HELP_CENTER 14
#define LEGACY_SETTINGS_SECURITY 15
#define LEGACY_SETTINGS_DISPLAY 16

// ============================================================ Launcher =====

namespace {
int gridHeaderH() { return UI::sy(56); }
int gridTop() { return gridHeaderH() + UI::sy(10); }
int gridAreaH() { return UI::H - gridTop() - UI::sy(28); }
int gridCellW() { return UI::W / LauncherUI::gridCols(); }
int gridCellH() { return gridAreaH() / LauncherUI::gridRows(); }
int gridLeft() { return (UI::W - LauncherUI::gridCols() * gridCellW()) / 2; }
int gridCellsPerPage() { return LauncherUI::gridCols() * LauncherUI::gridRows(); }

// tile de app (letra inicial sobre cor do hash) desenhado no Canvas
void drawAppTile(kui::Canvas& c, const std::string& name, int x, int y) {
    uint32_t bg = Icon::appTileColor(name.c_str());
    c.fillRoundRect({x, y, Icon::SIZE, Icon::SIZE}, UI::sx(8), bg);
    char letter = 'A';
    for (char ch : name) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            letter = (ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
            break;
        }
    }
    c.text(std::string(1, letter), x + Icon::SIZE / 2, y + Icon::SIZE / 2, UI::big ? 6 : 4,
           THEME_TEXT, MC_DATUM);
}

kui::Rect wifiBannerRect() {
    int h = UI::sy(24);
    return {UI::sx(8), UI::H - h - UI::sy(6), UI::W - UI::sx(16), h};
}

bool wifiBannerVisible() {
    return !WebManager::isActive() && !FileSystem::exists("/local/nowifi.txt");
}
}  // namespace

kui::Rect LauncherScreen::cellRect(int entryIndex) const {
    int cell = entryIndex - page * gridCellsPerPage();
    int col = cell % LauncherUI::gridCols();
    int row = cell / LauncherUI::gridCols();
    return {gridLeft() + col * gridCellW(), gridTop() + row * gridCellH(), gridCellW(), gridCellH()};
}

void LauncherScreen::onEnter() {
    if (LauncherUI::needsRescan) {
        LauncherUI::scanLocalApps();
        LauncherUI::needsRescan = false;
    }
    if (page >= LauncherUI::gridTotalPages()) page = LauncherUI::gridTotalPages() - 1;
    if (page < 0) page = 0;
}

void LauncherScreen::draw(kui::Canvas& c) {
    c.fill(THEME_BG);

    // header: card + barra accent + titulo + relogio + wifi
    int hdr = gridHeaderH();
    c.fillRect({0, 0, UI::W, hdr}, THEME_CARD);
    c.fillRect({0, hdr - UI::sy(3), UI::W, UI::sy(3)}, THEME_ACCENT);
    c.text("KryonOS", UI::sx(14), hdr / 2, UI::font(4), THEME_TEXT, ML_DATUM);

    bool wifi = WebManager::isActive();
    const char* wifiName = wifi ? "wifi_on" : "wifi_off";
    if (Icon::available(wifiName)) {
        c.drawIcon(wifiName, UI::W - Icon::SIZE - UI::sx(10), (hdr - Icon::SIZE) / 2);
    } else {
        c.fillCircle(UI::W - UI::sx(12), hdr / 2, UI::sx(4), wifi ? THEME_OK : THEME_TEXT_DIM);
    }
    c.text(TimeManager::getFormattedTime(), UI::W - Icon::SIZE - UI::sx(24), hdr / 2, UI::font(2),
           THEME_TEXT_DIM, MR_DATUM);

    // grid
    const char* sysIcons[4] = {"appstore", "installer", "settings", "help"};
    const char* sysNames[4] = {"App Store", "Installer", "Settings", "Help"};
    for (int entry = page * gridCellsPerPage();
         entry < LauncherUI::gridTotalEntries() && entry < (page + 1) * gridCellsPerPage(); entry++) {
        kui::Rect cell = cellRect(entry);
        int iconX = cell.x + (cell.w - Icon::SIZE) / 2;
        int iconY = cell.y + (cell.h - Icon::SIZE - (UI::big ? 26 : 14)) / 2;
        std::string label;
        if (entry < 4) {
            if (Icon::available(sysIcons[entry])) {
                c.drawIcon(sysIcons[entry], iconX, iconY);
            } else {
                c.fillRoundRect({iconX, iconY, Icon::SIZE, Icon::SIZE}, UI::sx(8), THEME_CARD);
                c.drawRoundRect({iconX, iconY, Icon::SIZE, Icon::SIZE}, UI::sx(8), THEME_STROKE);
            }
            label = sysNames[entry];
        } else {
            int appIdx = entry - 4;
            drawAppTile(c, LauncherUI::appEntryName(appIdx), iconX, iconY);
            label = LauncherUI::appEntryName(appIdx);
        }
        // label truncada
        std::string shown = label;
        while (shown.length() > 1 && c.textWidth(shown.c_str(), UI::font(2)) > cell.w - UI::sx(8)) {
            shown = shown.substr(0, shown.length() - 1);
        }
        if (shown != label && shown.length() > 1) shown += ".";
        c.text(shown, cell.x + cell.w / 2, iconY + Icon::SIZE + (UI::big ? 6 : 3), UI::font(2), THEME_TEXT, TC_DATUM);
    }

    // dots
    int tp = LauncherUI::gridTotalPages();
    if (tp > 1) {
        int dotsY = UI::H - UI::sy(14);
        int spacing = UI::sx(18);
        int x0 = UI::cx() - (tp - 1) * spacing / 2;
        for (int i = 0; i < tp; i++) {
            c.fillCircle(x0 + i * spacing, dotsY, i == page ? UI::sx(3) : UI::sx(2),
                         i == page ? THEME_ACCENT : THEME_STROKE);
        }
    }

    // banner "WiFi offline"
    if (wifiBannerVisible()) {
        kui::Rect r = wifiBannerRect();
        c.fillRoundRect(r, UI::sx(6), 0x231A0D);
        c.drawRoundRect(r, UI::sx(6), THEME_WARN);
        c.text("WiFi offline - toque para configurar", r.x + r.w / 2, r.y + r.h / 2, UI::font(1),
               THEME_WARN, MC_DATUM);
    }
}

bool LauncherScreen::onTouch(const kui::TouchEvent& ev) {
    if (ev.type != TouchEvent::Release) return false;

    // banner wifi -> tela de setup
    if (ev.isTap() && wifiBannerVisible() && wifiBannerRect().contains(ev.x, ev.y)) {
        Navigator::push(WifiSetupScreen::instance());
        return true;
    }

    // swipe troca pagina
    switch (ev.swipe()) {
        case TouchEvent::SwipeLeft:
            if (page < LauncherUI::gridTotalPages() - 1) {
                page++;
                markDirty();
            }
            return true;
        case TouchEvent::SwipeRight:
            if (page > 0) {
                page--;
                markDirty();
            }
            return true;
        default: break;
    }

    if (!ev.isTap()) return false;
    for (int entry = page * gridCellsPerPage();
         entry < LauncherUI::gridTotalPages() * gridCellsPerPage() && entry < LauncherUI::gridTotalEntries();
         entry++) {
        if (!cellRect(entry).contains(ev.x, ev.y)) continue;
        if (entry == 0) LegacyScreen::openLegacy(LEGACY_APP_STORE);
        else if (entry == 1) LegacyScreen::openLegacy(3);  // installer
        else if (entry == 2) LegacyScreen::openLegacy(LEGACY_SETTINGS);
        else if (entry == 3) LegacyScreen::openLegacy(LEGACY_HELP_CENTER);
        else Navigator::push(AppHostScreen::instance(entry - 4));
        return true;
    }
    return false;
}

void LauncherScreen::onTick(uint32_t) {
    std::string clock = TimeManager::getFormattedTime();
    bool wifi = WebManager::isActive();
    if (clock != lastClock || wifi != lastWifi) {
        lastClock = clock;
        lastWifi = wifi;
        markDirty();
    }
}

// ============================================================ App host =====

namespace {
AppHostScreen* s_appHost[50] = {};
}

AppHostScreen* AppHostScreen::instance(int appIndex) {
    if (appIndex < 0 || appIndex >= 50) return nullptr;
    if (s_appHost[appIndex] == nullptr) s_appHost[appIndex] = new AppHostScreen(appIndex);
    return s_appHost[appIndex];
}

AppHostScreen::AppHostScreen(int appIndex) : m_appIndex(appIndex) {}

void AppHostScreen::draw(kui::Canvas& c) {
    c.fill(THEME_BG);
    c.text("Carregando app...", c.width() / 2, c.height() / 2, UI::font(2), THEME_TEXT_DIM, MC_DATUM);
}

void AppHostScreen::onEnter() { m_started = false; }

void AppHostScreen::onTick(uint32_t) {
    if (m_started) return;
    m_started = true;
    // Executa o app sincronamente (sai via OS_EXIT; task propria no W7d)
    LauncherUI::launchApp(m_appIndex);
    Navigator::home();
}

// ============================================================ WiFi setup ===

namespace {
WifiSetupScreen* s_wifiSetup = nullptr;

// Ponte do evento de scan (disparado na task do WiFi) para a tela: o handler
// so copia os resultados e acorda uma flag; o onTick consome no loop da UI.
// O vector e protegido por mutex e entregue por swap — o handler nunca
// realoca o buffer que a UI itera.
std::vector<ScannedNetwork> s_scanResults;   // escrito pelo handler (com lock)
std::vector<ScannedNetwork> s_scanBuffer;    // lido pelo rebuildList (so na UI)
SemaphoreHandle_t s_scanMutex = nullptr;
volatile bool s_scanDelivered = false;
bool s_scanHandlerBound = false;

// Barras de sinal (glifos ASCII) para o texto a direita do item
const char* rssiBars(int32_t rssi) {
    if (rssi >= -50) return "####";
    if (rssi >= -62) return "###";
    if (rssi >= -74) return "##";
    return "#";
}
}  // namespace

WifiSetupScreen* WifiSetupScreen::instance() {
    if (s_wifiSetup == nullptr) {
        s_wifiSetup = new WifiSetupScreen();
        s_wifiSetup->m_btnBack.onTap = [] { Navigator::pop(); };
        s_wifiSetup->m_btnScan.onTap = [s = s_wifiSetup] { s->startScan(); };
        s_wifiSetup->m_btnWeb.onTap = [s = s_wifiSetup] { s->enterWebPortal(); };
        s_wifiSetup->m_list.onSelect = [s = s_wifiSetup](int idx) {
            if (idx < 0) return;
            if (idx >= (int)s->m_nets.size()) {  // item fixo "Outra rede"
                s->askHiddenSsid();
                return;
            }
            const NetEntry& n = s->m_nets[idx];
            if (n.secure) s->askPassword(n.ssid);
            else s->tryConnect(n.ssid, "", false);
        };
    }
    return s_wifiSetup;
}

void WifiSetupScreen::startScan() {
    if (!s_scanHandlerBound) {
        s_scanHandlerBound = true;
        if (s_scanMutex == nullptr) s_scanMutex = xSemaphoreCreateMutex();
        NetworkManager::instance().onScanCompleted.addHandler(
            [](const std::vector<ScannedNetwork>& nets) {
                if (xSemaphoreTake(s_scanMutex, portMAX_DELAY) == pdTRUE) {
                    s_scanResults.assign(nets.begin(), nets.end());
                    s_scanDelivered = true;
                    xSemaphoreGive(s_scanMutex);
                }
            });
    }
    s_scanDelivered = false;
    m_scanning = WebManager::startScanAsync();
    m_scanStartMs = millis();
    m_scanStarted = true;
    if (!m_scanning) Navigator::toast("Falha ao iniciar o scan", THEME_ERR);
    updateStatus();
    markDirty();
}

void WifiSetupScreen::rebuildList() {
    m_nets.clear();
    for (const auto& net : s_scanBuffer) {
        if (net.ssid[0] == '\0') continue;  // ocultas entram via "Outra rede"
        std::string ssid = net.ssid;
        bool dup = false;
        for (const auto& n : m_nets) {
            if (n.ssid == ssid) { dup = true; break; }
        }
        if (!dup) m_nets.push_back({ssid, net.rssi, net.authMode != WIFI_AUTH_OPEN});
    }

    m_list.items.clear();
    int maxChars = UI::big ? 28 : 16;
    for (const auto& n : m_nets) {
        std::string label = n.ssid;
        if ((int)label.length() > maxChars) label = label.substr(0, maxChars - 1) + ".";
        List::Item it;
        it.label = label;
        it.right = n.secure ? rssiBars(n.rssi) : "aberta";
        m_list.items.push_back(it);
    }
    List::Item hidden;
    hidden.label = "Outra rede (oculta)...";
    m_list.items.push_back(hidden);
    m_list.selected = -1;
    m_list.top = 0;
}

void WifiSetupScreen::updateStatus() {
    if (m_phase == WebPortal) {
        m_status.clear();
        return;
    }
    if (m_scanning) {
        m_status = "Buscando redes...";
        return;
    }
    if (WebManager::isActive()) {
        std::string ip = WebManager::getIPAddress();
        m_status = "Conectado" + (ip.empty() ? "" : " - " + ip);
        return;
    }
    if (m_nets.empty()) {
        m_status = m_scanStarted ? "Nenhuma rede encontrada" : "";
        return;
    }
    m_status = kstr::fmt("%d redes - toque para conectar", (int)m_nets.size());
}

void WifiSetupScreen::askPassword(const std::string& ssid) {
    KeyboardScreen* kb = new KeyboardScreen("Senha: " + ssid, "", 64);
    m_kbTrash = m_kb;  // teclado anterior sai fora da cadeia de chamadas dele
    m_kb = kb;
    kb->onResult = [this, ssid](const std::string& text, bool ok) {
        Navigator::pop();  // tira o teclado; esta tela volta ao topo
        std::string pass = kstr::trim(text);
        if (ok && !pass.empty()) tryConnect(ssid, pass, true);
    };
    Navigator::push(kb);
}

void WifiSetupScreen::askHiddenSsid() {
    KeyboardScreen* kb = new KeyboardScreen("Nome da rede (SSID)", "", 32);
    m_kbTrash = m_kb;
    m_kb = kb;
    kb->onResult = [this](const std::string& text, bool ok) {
        Navigator::pop();
        std::string ssid = kstr::trim(text);
        if (ok && !ssid.empty()) askPassword(ssid);
    };
    Navigator::push(kb);
}

void WifiSetupScreen::tryConnect(const std::string& ssid, const std::string& password,
                                 bool secure) {
    drawConnecting(ssid);  // feedback direto no display: o connect bloqueia ~15s
    bool ok = WebManager::connect(ssid, password, true, 15000);
    if (ok) {
        WebManager::enable();  // religa o servidor web se o usuario o tinha ligado
        Navigator::toast("WiFi conectado", THEME_OK);
        m_scanStarted = false;  // proxima entrada refaz o scan
        Navigator::pop();       // volta para quem abriu (launcher/settings)
        return;
    }
    Navigator::toast("Falha ao conectar", THEME_ERR);
    markDirty();
    if (secure) askPassword(ssid);  // senha provavelmente errada: pede de novo
}

void WifiSetupScreen::drawConnecting(const std::string& ssid) {
    Canvas c(Board::display());
    c.begin(true);  // direto no display, sem sprite
    c.fill(THEME_BG);
    int hdr = UI::sy(56);
    c.fillRect({0, 0, UI::W, hdr}, THEME_CARD);
    c.fillRect({0, hdr - UI::sy(3), UI::W, UI::sy(3)}, THEME_ACCENT);
    c.text("Configurar WiFi", UI::sx(14), hdr / 2, UI::font(4), THEME_TEXT, ML_DATUM);
    c.text("Conectando em", UI::cx(), UI::sy(140), UI::font(2), THEME_TEXT, MC_DATUM);
    c.text(ssid, UI::cx(), UI::sy(166), UI::font(2), THEME_ACCENT, MC_DATUM);
    c.text("Aguarde...", UI::cx(), UI::sy(205), UI::font(1), THEME_TEXT_DIM, MC_DATUM);
    c.end();
}

void WifiSetupScreen::enterWebPortal() {
    m_phase = WebPortal;
    m_portalState = 0;
    m_portalDetail.clear();
    m_portalStarted = WifiSetupPortal::begin();
    if (!m_portalStarted) {
        Navigator::toast("Falha ao abrir o portal", THEME_ERR);
        m_phase = LocalList;
    }
    markDirty();
}

void WifiSetupScreen::leaveWebPortal() {
    if (m_portalStarted) {
        WifiSetupPortal::end();
        m_portalStarted = false;
    }
    m_phase = LocalList;
    m_portalState = 0;
    m_portalDetail.clear();
    startScan();  // o radio trocou de modo; a lista antiga esta stale
}

void WifiSetupScreen::onEnter() {
    if (m_phase == WebPortal) {
        if (!m_portalStarted) m_portalStarted = WifiSetupPortal::begin();
    } else if (!m_scanStarted) {
        startScan();
    } else {
        updateStatus();  // estado pode ter mudado desde a ultima visita
    }
    markDirty();
}

void WifiSetupScreen::onExit() {
    // onExit roda tambem ao EMPILHAR o teclado por cima — so derruba o
    // portal quando a tela sai de vez ainda em modo web sem conexao.
    if (m_phase == WebPortal && m_portalState != 1 && m_portalStarted) {
        WifiSetupPortal::end();
        m_portalStarted = false;
        m_phase = LocalList;
    }
}

void WifiSetupScreen::draw(Canvas& c) {
    c.fill(THEME_BG);
    int hdr = UI::sy(56);
    c.fillRect({0, 0, UI::W, hdr}, THEME_CARD);
    c.fillRect({0, hdr - UI::sy(3), UI::W, UI::sy(3)}, THEME_ACCENT);
    c.text("Configurar WiFi", UI::sx(14), hdr / 2, UI::font(4), THEME_TEXT, ML_DATUM);

    if (m_phase == WebPortal) {
        c.text("1. Conecte no ponto de acesso:", UI::cx(), UI::sy(92), UI::font(2), THEME_TEXT, MC_DATUM);
        c.text(WifiSetupPortal::apSsid(), UI::cx(), UI::sy(114), UI::font(2), THEME_ACCENT, MC_DATUM);
        c.text("2. Abra no navegador:", UI::cx(), UI::sy(144), UI::font(2), THEME_TEXT, MC_DATUM);
        c.text("http://192.168.4.1", UI::cx(), UI::sy(166), UI::font(2), THEME_ACCENT, MC_DATUM);

        const char* status = "Aguardando configuracao...";
        uint32_t statusColor = THEME_TEXT_DIM;
        if (!m_portalStarted) {
            status = "Falha ao iniciar o portal";
            statusColor = THEME_ERR;
        } else if (m_portalState == 1) {
            status = "Conectado!";
            statusColor = THEME_OK;
        } else if (m_portalState == 2) {
            status = "Falhou - tente de novo pela pagina";
            statusColor = THEME_ERR;
        }
        c.text(status, UI::cx(), UI::sy(202), UI::font(1), statusColor, MC_DATUM);
        if (m_portalState == 1 && !m_portalDetail.empty()) {
            c.text(m_portalDetail, UI::cx(), UI::sy(220), UI::font(1), THEME_TEXT_DIM, MC_DATUM);
        }

        m_btnBack.label = "Voltar";
        m_btnBack.style = Button::Ghost;
        m_btnBack.rect = {UI::sx(70), UI::sy(270), UI::sx(100), UI::sy(32)};
        m_btnBack.draw(c);
        return;
    }

    if (!m_status.empty()) {
        c.text(m_status, UI::cx(), hdr + UI::sy(13), UI::font(1),
               m_scanning ? THEME_ACCENT : THEME_TEXT_DIM, MC_DATUM);
    }

    int by = UI::H - UI::sy(40);
    int gap = UI::sx(6);
    int bw = (UI::W - UI::sx(16) - 2 * gap) / 3;
    m_btnBack.label = "Voltar";
    m_btnBack.style = Button::Ghost;
    m_btnBack.rect = {UI::sx(8), by, bw, UI::sy(32)};
    m_btnScan.label = "Buscar";
    m_btnScan.style = Button::Ghost;
    m_btnScan.rect = {UI::sx(8) + bw + gap, by, bw, UI::sy(32)};
    m_btnWeb.label = "Via web";
    m_btnWeb.style = Button::Ghost;
    m_btnWeb.rect = {UI::sx(8) + 2 * (bw + gap), by, bw, UI::sy(32)};

    int listTop = hdr + UI::sy(22);
    m_list.rect = {UI::sx(8), listTop, UI::W - UI::sx(16), by - UI::sy(8) - listTop};
    if (m_list.items.empty()) {
        int midY = (listTop + by) / 2;
        if (!m_scanning) {
            c.text("Nenhuma rede encontrada", UI::cx(), midY - UI::sy(8), UI::font(2),
                   THEME_TEXT_DIM, MC_DATUM);
            if (m_scanStarted) {
                c.text("Toque em Buscar para tentar de novo", UI::cx(), midY + UI::sy(12),
                       UI::font(1), THEME_TEXT_DIM, MC_DATUM);
            }
        }
    } else {
        m_list.draw(c);
    }
    m_btnBack.draw(c);
    m_btnScan.draw(c);
    m_btnWeb.draw(c);
}

bool WifiSetupScreen::onTouch(const TouchEvent& ev) {
    if (m_phase == WebPortal) return m_btnBack.onTouch(ev, m_btnBack.rect);
    if (m_btnBack.onTouch(ev, m_btnBack.rect)) return true;
    if (m_btnScan.onTouch(ev, m_btnScan.rect)) return true;
    if (m_btnWeb.onTouch(ev, m_btnWeb.rect)) return true;
    return m_list.onTouch(ev, m_list.rect);
}

void WifiSetupScreen::onTick(uint32_t) {
    // Teclado descartado so e deletado aqui, fora da cadeia de chamadas dele
    if (m_kbTrash != nullptr) {
        delete m_kbTrash;
        m_kbTrash = nullptr;
    }

    if (m_phase == LocalList) {
        if (s_scanDelivered) {
            bool doRebuild = false;
            if (xSemaphoreTake(s_scanMutex, portMAX_DELAY) == pdTRUE) {
                s_scanDelivered = false;
                if (m_scanning) {
                    m_scanning = false;
                    s_scanBuffer.swap(s_scanResults);
                    s_scanResults.clear();
                    doRebuild = true;
                }
                xSemaphoreGive(s_scanMutex);
            }
            if (doRebuild) {
                rebuildList();
                updateStatus();
                markDirty();
            }
        } else if (m_scanning && millis() - m_scanStartMs > 20000) {
            m_scanning = false;  // o evento de scan se perdeu
            updateStatus();
            markDirty();
        }
        return;
    }

    // Modo web: poll nao-bloqueante do portal
    if (m_portalStarted && m_portalState == 0) {
        std::string detail;
        WifiSetupPortal::State st = WifiSetupPortal::poll(detail);
        if (st == WifiSetupPortal::Connected) {
            m_portalState = 1;
            m_portalDetail = detail;
            m_portalDoneAtMs = millis();
            markDirty();
        } else if (st == WifiSetupPortal::Failed) {
            m_portalState = 2;
            markDirty();
        }
    }
    if (m_portalState == 1 && millis() - m_portalDoneAtMs > 1500) {
        m_phase = LocalList;   // onExit nao deve derrubar a STA conectada
        m_portalStarted = false;
        m_scanStarted = false;
        Navigator::toast("WiFi conectado", THEME_OK);
        Navigator::pop();
    }
}

// ============================================================ Legacy =======

namespace {
// wrappers para DrawFn (sem parametros)
void drawUpdaterBoot() { SettingsUI::drawUpdater(true); }
void drawUpdaterManual() { SettingsUI::drawUpdater(false); }
}  // namespace

void LegacyScreen::onTick(uint32_t dtMs) {
    // telas antigas esperam o handler repetido enquanto pressionado
    if (m_pressed) {
        m_repeatAccum += dtMs;
        if (m_repeatAccum >= 100) {
            m_repeatAccum = 0;
            m_touchFn((uint16_t)m_lastX, (uint16_t)m_lastY);
        }
    }
}

bool LegacyScreen::onTouch(const kui::TouchEvent& ev) {
    if (ev.type == TouchEvent::Press) {
        m_pressed = true;
        m_lastX = ev.x;
        m_lastY = ev.y;
    } else if (ev.type == TouchEvent::Drag) {
        m_lastX = ev.x;
        m_lastY = ev.y;
    } else if (ev.type == TouchEvent::Release) {
        m_pressed = false;
    }
    m_touchFn((uint16_t)ev.x, (uint16_t)ev.y);
    return true;
}

namespace {
LegacyScreen* s_legacyCache[17] = {};
}  // namespace

LegacyScreen* LegacyScreen::forState(int legacyState) {
    if (legacyState <= 0 || legacyState > 16) return nullptr;
    if (s_legacyCache[legacyState] != nullptr) return s_legacyCache[legacyState];

    LegacyScreen* s = nullptr;
    switch (legacyState) {
        case LEGACY_SETTINGS: s = new LegacyScreen("settings", SettingsUI::draw, SettingsUI::handleTouch); break;
        case 3: s = new LegacyScreen("installer", InstallerUI::draw, InstallerUI::handleTouch); break;
        case LEGACY_WEB_APP: s = new LegacyScreen("webapp", WebServerAppUI::draw, WebServerAppUI::handleTouch); break;
        case LEGACY_SETTINGS_WIFI: s = new LegacyScreen("settings:wifi", SettingsUI::drawWiFi, SettingsUI::handleWiFiTouch); break;
        case LEGACY_SETTINGS_ABOUT: s = new LegacyScreen("settings:about", SettingsUI::drawAbout, SettingsUI::handleAboutTouch); break;
        case LEGACY_SETTINGS_APPS: s = new LegacyScreen("settings:apps", SettingsUI::drawApps, SettingsUI::handleAppsTouch); break;
        case LEGACY_SETTINGS_TIME: s = new LegacyScreen("settings:time", SettingsUI::drawTimeSettings, SettingsUI::handleTimeTouch); break;
        case LEGACY_SETTINGS_TIME_MANUAL: s = new LegacyScreen("settings:timemanual", SettingsUI::drawTimeManual, SettingsUI::handleTimeManualTouch); break;
        case LEGACY_UPDATER_BOOT: s = new LegacyScreen("updater", drawUpdaterBoot, SettingsUI::handleUpdaterTouch); break;
        case 12: s = new LegacyScreen("updater", drawUpdaterManual, SettingsUI::handleUpdaterTouch); break;
        case LEGACY_APP_STORE: s = new LegacyScreen("appstore", AppStoreUI::draw, AppStoreUI::handleTouch); break;
        case LEGACY_HELP_CENTER: s = new LegacyScreen("help", HelpCenterUI::draw, HelpCenterUI::handleTouch); break;
        case LEGACY_SETTINGS_SECURITY: s = new LegacyScreen("settings:security", SettingsUI::drawSecurity, SettingsUI::handleSecurityTouch); break;
        case LEGACY_SETTINGS_DISPLAY: s = new LegacyScreen("settings:display", SettingsUI::drawDisplaySettings, SettingsUI::handleDisplayTouch); break;
        default: break;
    }
    s_legacyCache[legacyState] = s;
    return s;
}

