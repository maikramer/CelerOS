#include "SettingsScreens.h"

#include "../UI/Kui.h"
#include "../Launcher/Screens.h"
#include "../Launcher/LauncherUI.h"
#include "../FileSystem/FileSystem.h"
#include "../Kernel/TimeManager.h"
#include "../WebManager/WebManager.h"
#include "../OTA/OtaManager.h"
#include "../Display/Backlight.h"
#include "SystemInfo.h"
#include "../Utils/StrUtils.h"
#include "TouchCalibrator.h"

#include "esp_rom_md5.h"
#include <Arduino.h>
#include <ctime>
#include <cstdio>
#include <vector>

// ============================================================================
// Port W7c-2 do SettingsUI para o Kui. As telas antigas de settings saem do
// mapa legacy (LegacyScreen::forState) e viram Screens de verdade:
//   - PIN: PinPadScreen nao-bloqueante (o loop da UI segue vivo)
//   - hit-test = geometria do draw (List/widgets; fim dos ifs de coordenada)
//   - texto sempre transparente; fundos limpos por retangulo
// ============================================================================

namespace {

// ---------------------------------------------------------------- helpers --

std::string fmtBytes(uint64_t bytes) {
    char buf[24];
    if (bytes < 1024) snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
    else if (bytes < 1024ULL * 1024) snprintf(buf, sizeof(buf), "%u KB", (unsigned)(bytes / 1024));
    else snprintf(buf, sizeof(buf), "%u MB", (unsigned)(bytes / (1024ULL * 1024)));
    return buf;
}

// Quebra s em linhas de no maximo maxW px (por palavra); maxLines no total.
std::vector<std::string> wrapText(kui::Canvas& c, const std::string& s, const lgfx::IFont* f, int maxW,
                                  int maxLines) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < s.length() && (int)lines.size() < maxLines) {
        size_t sp = s.find(' ', i);
        size_t nl = s.find('\n', i);
        if (nl != std::string::npos && (sp == std::string::npos || nl < sp)) sp = nl;
        std::string word = (sp == std::string::npos) ? s.substr(i) : s.substr(i, sp - i);
        if (nl == sp) {  // quebra de linha explicita
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
                if ((int)lines.size() >= maxLines) break;
            }
            if (!word.empty()) lines.push_back(word);
            i = (sp == std::string::npos) ? s.length() : sp + 1;
            continue;
        }
        i = (sp == std::string::npos) ? s.length() : sp + 1;
        std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && c.textWidth(cand.c_str(), f) > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = cand;
        }
    }
    if (!line.empty() && (int)lines.size() < maxLines) lines.push_back(line);
    return lines;
}

// ------------------------------------------------------------------- PIN ----

const char* PIN_FILE = "/local/settings_pin.txt";
uint32_t s_unlockedUntilMs = 0;

std::string pinMd5(const std::string& input) {
    md5_context_t ctx;
    esp_rom_md5_init(&ctx);
    esp_rom_md5_update(&ctx, input.c_str(), (uint32_t)input.length());
    uint8_t hash[16];
    esp_rom_md5_final(hash, &ctx);
    char out[33];
    for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", hash[i]);
    out[32] = '\0';
    return std::string(out);
}

bool pinIsSet() { return FileSystem::exists(PIN_FILE); }

bool pinVerify(const std::string& pin) {
    std::string stored = kstr::trim(FileSystem::readTextFile(PIN_FILE));
    return stored.length() == 32 && stored == pinMd5(pin);
}

// ------------------------------------------------------------- MenuBase ----
// Cabecalho padrao do Kui + area de conteudo; volta pelo gesto de borda
// (Navigator) — mesma convencao das demais telas Kui.

class MenuBase : public kui::Screen {
public:
    explicit MenuBase(const char* title) : m_title(title) {}

    void draw(kui::Canvas& c) final {
        c.fill(THEME_BG);
        kui::drawHeader(c, m_title);
        drawContent(c);
    }

protected:
    virtual void drawContent(kui::Canvas& c) = 0;
    virtual bool touchContent(const kui::TouchEvent& ev) { (void)ev; return false; }

    kui::Rect contentRect() const {
        return {UI::sx(8), kui::headerHeight() + UI::sy(8), UI::W - UI::sx(16),
                UI::H - kui::headerHeight() - UI::sy(16)};
    }

    bool onTouch(const kui::TouchEvent& ev) override { return touchContent(ev); }

private:
    const char* m_title;
};

// Lista que preenche a area de conteudo (padrao das telas de menu).
class ListMenuBase : public MenuBase {
public:
    explicit ListMenuBase(const char* title) : MenuBase(title) {}

protected:
    kui::List m_list;

    void drawContent(kui::Canvas& c) override {
        m_list.rect = contentRect();
        m_list.draw(c);
    }

    bool touchContent(const kui::TouchEvent& ev) override {
        return m_list.onTouch(ev, contentRect());
    }

    void onTick(uint32_t dtMs) override {
        if (m_list.tick(dtMs)) markDirty();
    }
};

}  // namespace

// ============================================================================
// PinPadScreen — numpad 3x4 modal (nao-bloqueante). Contrato igual ao do
// KeyboardScreen: o dono do fluxo chama Navigator::pop() dentro do onResult.
// ============================================================================

class PinPadScreen : public kui::Screen {
public:
    using ResultFn = std::function<void(const std::string& pin, bool ok)>;

    PinPadScreen(const char* title, int maxLen) : m_title(title), m_maxLen(maxLen) {}
    ResultFn onResult;

    void draw(kui::Canvas& c) override {
        c.fill(THEME_BG);
        kui::drawHeader(c, m_title.c_str());

        // pontos do PIN digitado
        std::string dots;
        for (size_t i = 0; i < m_pin.length(); i++) dots += "* ";
        c.text(dots, UI::cx(), UI::sy(76), kui::type::display(), THEME_TEXT, MC_DATUM);

        for (int r = 0; r < 4; r++) {
            for (int col = 0; col < 3; col++) {
                kui::Rect k = keyRect(r, col);
                bool isOk = (r == 3 && col == 2);
                bool isClr = (r == 3 && col == 0);
                uint32_t bg = isOk ? THEME_OK : (isClr ? THEME_ERR : THEME_CARD);
                if (kui::isPressed(k)) bg = THEME_RAISED;
                c.fillRoundRect(k, UI::sx(6), bg);
                c.text(keys[r][col], k.x + k.w / 2, k.y + k.h / 2, kui::type::body(), THEME_TEXT, MC_DATUM);
            }
        }

        kui::Rect cancel = cancelRect();
        c.fillRoundRect(cancel, UI::sx(6), kui::isPressed(cancel) ? THEME_RAISED : THEME_CARD);
        c.text("Cancelar", cancel.x + cancel.w / 2, cancel.y + cancel.h / 2, kui::type::body(), THEME_TEXT_DIM,
               MC_DATUM);
    }

    bool onTouch(const kui::TouchEvent& ev) override {
        if (!(ev.type == kui::TouchEvent::Release && ev.isTap())) return true;
        if (cancelRect().contains(ev.x, ev.y)) {
            if (onResult) onResult("", false);
            return true;
        }
        for (int r = 0; r < 4; r++) {
            for (int col = 0; col < 3; col++) {
                if (!keyRect(r, col).contains(ev.x, ev.y)) continue;
                const char* k = keys[r][col];
                if (k[0] >= '0' && k[0] <= '9' && (int)m_pin.length() < m_maxLen) {
                    m_pin += k[0];
                } else if (strcmp(k, "CLR") == 0) {
                    m_pin.clear();
                } else if (strcmp(k, "OK") == 0 && m_pin.length() >= 4) {
                    if (onResult) onResult(m_pin, true);
                    return true;
                }
                markDirty();
                return true;
            }
        }
        return true;
    }

private:
    static const char* keys[4][3];

    kui::Rect keyRect(int r, int col) const {
        int x = 15 + col * 75, y = 100 + r * 46;
        return {UI::sx(x), UI::sy(y), UI::sx(60), UI::sy(38)};
    }
    kui::Rect cancelRect() const { return {UI::sx(60), UI::sy(288), UI::sx(120), UI::sy(26)}; }

    std::string m_title;
    int m_maxLen;
    std::string m_pin;
};

const char* PinPadScreen::keys[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"CLR", "0", "OK"}};

namespace {

void pushPinPad(const char* title, PinPadScreen::ResultFn onResult) {
    auto* pad = new PinPadScreen(title, 8);
    pad->onResult = std::move(onResult);
    kui::Navigator::push(pad);
}

// Fluxo novo PIN -> confirmacao -> grava (usado por definir/alterar).
void pushNewPinFlow(std::function<void()> onDone) {
    pushPinPad("Novo PIN (4-8)", [onDone](const std::string& p1, bool ok1) {
        kui::Navigator::pop();
        if (!ok1) return;
        pushPinPad("Confirmar PIN", [p1, onDone](const std::string& p2, bool ok2) {
            kui::Navigator::pop();
            if (!ok2) return;
            if (p1 != p2 || p1.length() < 4) {
                kui::Navigator::toast("PINs nao conferem", THEME_ERR);
                return;
            }
            FileSystem::writeTextFile(PIN_FILE, pinMd5(p1).c_str());
            s_unlockedUntilMs = 0;  // pede o novo PIN no proximo acesso
            kui::Navigator::toast("PIN salvo", THEME_OK);
            if (onDone) onDone();
        });
    });
}

// Fluxo protegido por PIN atual (alterar/remover).
void pushCurrentPinThen(std::function<void()> onVerified) {
    pushPinPad("PIN atual", [onVerified](const std::string& cur, bool ok) {
        kui::Navigator::pop();
        if (!ok) return;
        if (!pinVerify(cur)) {
            kui::Navigator::toast("PIN incorreto", THEME_ERR);
            return;
        }
        onVerified();
    });
}

}  // namespace

// ============================================================================
// Sobre
// ============================================================================

class AboutScreen : public ListMenuBase {
public:
    static AboutScreen* instance() {
        static AboutScreen s;
        return &s;
    }

protected:
    AboutScreen() : ListMenuBase("Sobre") {
        s_reset.buttons.resize(2);
        s_reset.buttons[0].label = "Formatar";
        s_reset.buttons[0].style = kui::Button::Danger;
        s_reset.buttons[0].onTap = [] {
            kui::Navigator::closeDialog();
            KryonDisplay& tft = Board::display();
            tft.fillScreen(THEME_BG);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(THEME_TEXT, THEME_BG);
            tft.drawString("Formatando...", UI::cx(), UI::sy(150), lgfx::fontdata[UI::font(2)]);
            FileSystem::formatLittleFS();
            delay(800);
            ESP.restart();
        };
        s_reset.buttons[1].label = "Cancelar";
        s_reset.buttons[1].style = kui::Button::Ghost;
        s_reset.buttons[1].onTap = [] { kui::Navigator::closeDialog(); };
    }

    void onEnter() override { rebuild(); }

    void rebuild() {
        SystemInfo& sys = SystemInfo::instance();
        uint64_t lfsTotal = FileSystem::getTotalSpace("/local");
        uint64_t lfsUsed = FileSystem::getUsedSpace("/local");
        uint64_t sdTotal = FileSystem::getTotalSpace("/sd");
        uint64_t sdUsed = FileSystem::getUsedSpace("/sd");

        m_list.items.clear();
        m_list.items.push_back({"Versao", KRYONOS_VERSION, -1, false});
        m_list.items.push_back({"Placa", Board::profile().name, -1, false});
        m_list.items.push_back({"Canal OTA", Board::profile().otaChannel, -1, false});
        m_list.items.push_back({"Heap livre", fmtBytes(sys.getFreeHeap()), -1, false});
        m_list.items.push_back({"Interno (LFS)", fmtBytes(lfsUsed) + " / " + fmtBytes(lfsTotal), -1, false});
        m_list.items.push_back({"Cartao SD", sdTotal > 0 ? fmtBytes(sdUsed) + " / " + fmtBytes(sdTotal) : "ausente",
                                -1, false});
        m_list.items.push_back({"Uptime", sys.getFormattedUptime(), -1, false});
        m_list.items.push_back({"Reset", sys.getResetReasonString(), -1, false});
        m_list.items.push_back({"ZERAR DADOS", "", -1});
        m_list.onSelect = [this](int idx) {
            if (idx == 8) kui::Navigator::showDialog(&s_reset);
        };
        markDirty();
    }

private:
    static kui::Dialog s_reset;
};

kui::Dialog AboutScreen::s_reset = [] {
    kui::Dialog d;
    d.title = "Zerar dados";
    d.body = "Formatar o LittleFS e apagar todos os apps?";
    return d;
}();

// ============================================================================
// Aplicativos
// ============================================================================

class AppsSettingsScreen : public ListMenuBase {
public:
    static AppsSettingsScreen* instance() {
        static AppsSettingsScreen s;
        return &s;
    }

protected:
    AppsSettingsScreen() : ListMenuBase("Aplicativos") {
        s_appDialog.buttons.resize(3);
        s_appDialog.buttons[0].label = "Excluir";
        s_appDialog.buttons[0].style = kui::Button::Danger;
        s_appDialog.buttons[1].label = "Mover";
        s_appDialog.buttons[1].style = kui::Button::Ghost;
        s_appDialog.buttons[2].label = "Fechar";
        s_appDialog.buttons[2].style = kui::Button::Ghost;
        s_appDialog.buttons[2].onTap = [] { kui::Navigator::closeDialog(); };
    }

    void onEnter() override { reload(); }

    void reload() {
        m_paths.clear();
        FileEntry entries[50];
        int n = FileSystem::listDirectory("/local/apps/", entries, 25);
        n += FileSystem::listDirectory("/sd/apps/", entries + n, 25 - n);
        for (int i = 0; i < n; i++) m_paths.push_back(entries[i]);

        m_list.items.clear();
        m_list.items.push_back({"Instalar em", installToSd() ? "SD" : "Interno", -1});
        for (const FileEntry& e : m_paths) {
            m_list.items.push_back({displayName(e), kstr::startsWith(e.path, std::string("/sd")) ? "[SD]" : "", -1});
        }
        if (m_paths.empty()) m_list.items.push_back({"Nenhum app instalado", "", -1, false});
        m_list.onSelect = [this](int idx) { select(idx); };
        markDirty();
    }

    void select(int idx) {
        if (idx == 0) {  // preferencia de instalacao
            if (installToSd()) {
                FileSystem::deleteFile("/local/config_install_sd.txt");
            } else {
                FileSystem::writeTextFile("/local/config_install_sd.txt", "1");
            }
            reload();
            return;
        }
        int app = idx - 1;
        if (app < 0 || app >= (int)m_paths.size()) return;
        FileEntry sel = m_paths[(size_t)app];

        s_appDialog.title = displayName(sel);
        s_appDialog.body = kstr::startsWith(sel.path, std::string("/sd")) ? "Mover para o interno?" : "";
        s_appDialog.buttons[0].onTap = [this, sel]() { uninstall(sel); };
        s_appDialog.buttons[1].onTap = [this, sel]() { move(sel); };
        kui::Navigator::showDialog(&s_appDialog);
    }

private:
    static kui::Dialog s_appDialog;
    std::vector<FileEntry> m_paths;

    static bool installToSd() { return FileSystem::exists("/local/config_install_sd.txt"); }

    static std::string displayName(const FileEntry& e) {
        if (!e.isDir) return e.name;
        std::string jsonPath = e.path;
        if (!kstr::endsWith(jsonPath, std::string("/"))) jsonPath += "/";
        jsonPath += "app.json";
        if (FileSystem::exists(jsonPath.c_str())) {
            std::string parsed = FileSystem::parseJsonValue(FileSystem::readTextFile(jsonPath.c_str()), "name");
            if (!parsed.empty()) return parsed;
        }
        return e.name;
    }

    static void deleteTree(const FileEntry& e) {
        if (e.isDir) {
            FileEntry files[50];
            int n = FileSystem::listDirectory(e.path.c_str(), files, 50);
            for (int i = 0; i < n; i++) {
                if (!files[i].isDir) FileSystem::deleteFile(files[i].path.c_str());
            }
            FileSystem::rmdir(e.path.c_str());
        } else {
            FileSystem::deleteFile(e.path.c_str());
        }
    }

    void uninstall(const FileEntry& sel) {
        kui::Navigator::closeDialog();
        deleteTree(sel);
        LauncherUI::requestRescan();
        kui::Navigator::toast("App removido", THEME_OK);
        reload();
    }

    void move(const FileEntry& sel) {
        kui::Navigator::closeDialog();
        bool fromSd = kstr::startsWith(sel.path, std::string("/sd"));
        std::string destDir = fromSd ? "/local/apps/" : "/sd/apps/";
        FileSystem::mkdir(destDir.c_str());
        std::string destPath = destDir + sel.name;
        kui::Navigator::toast("Movendo app...", THEME_ACCENT);

        bool ok = sel.isDir ? FileSystem::copyDirectory(sel.path.c_str(), destPath.c_str())
                            : FileSystem::copyFile(sel.path.c_str(), destPath.c_str());
        if (ok) {
            deleteTree(sel);
            LauncherUI::requestRescan();
            kui::Navigator::toast("App movido", THEME_OK);
        } else {
            kui::Navigator::toast("Falha ao mover", THEME_ERR);
        }
        reload();
    }
};

kui::Dialog AppsSettingsScreen::s_appDialog;

// ============================================================================
// Wi-Fi
// ============================================================================

class WifiSettingsScreen : public ListMenuBase {
public:
    static WifiSettingsScreen* instance() {
        static WifiSettingsScreen s;
        return &s;
    }

protected:
    WifiSettingsScreen() : ListMenuBase("Wi-Fi") {
        s_forget.buttons.resize(2);
        s_forget.buttons[0].label = "Esquecer";
        s_forget.buttons[0].style = kui::Button::Danger;
        s_forget.buttons[0].onTap = [this]() {
            WebManager::forgetAllNetworks();
            kui::Navigator::closeDialog();
            kui::Navigator::toast("Redes esquecidas", THEME_OK);
            rebuild();
        };
        s_forget.buttons[1].label = "Cancelar";
        s_forget.buttons[1].style = kui::Button::Ghost;
        s_forget.buttons[1].onTap = [] { kui::Navigator::closeDialog(); };
    }

    void onEnter() override { rebuild(); }

    void rebuild() {
        bool off = FileSystem::exists("/local/nowifi.txt");
        bool active = WebManager::isActive();
        m_list.items.clear();
        m_list.items.push_back({active ? "Conectado" : (off ? "Desligado" : "Desconectado"),
                                active ? WebManager::getIPAddress() : "--", -1, false});
        m_list.items.push_back({"Wi-Fi", off ? "OFF" : "ON", -1});
        m_list.items.push_back({"Configurar rede", "", -1});
        if (WebManager::hasSavedNetworks()) m_list.items.push_back({"Esquecer rede", "", -1});
        m_list.items.push_back({"Servidor web", "", -1});
        m_list.onSelect = [this](int idx) { select(idx); };
        markDirty();
    }

    void select(int idx) {
        if (idx <= 0) return;
        std::string label = m_list.items[(size_t)idx].label;
        if (label == "Wi-Fi") {
            if (FileSystem::exists("/local/nowifi.txt")) {
                FileSystem::deleteFile("/local/nowifi.txt");
                if (!WebManager::hasSavedNetworks()) {
                    kui::Navigator::push(WifiSetupScreen::instance());
                    return;
                }
                WebManager::enable();
                kui::Navigator::toast("Wi-Fi ativado", THEME_OK);
            } else {
                FileSystem::writeTextFile("/local/nowifi.txt", "1");
                WebManager::disable();
                kui::Navigator::toast("Wi-Fi desativado", THEME_ACCENT);
            }
            rebuild();
        } else if (label == "Configurar rede") {
            kui::Navigator::push(WifiSetupScreen::instance());
        } else if (label == "Esquecer rede") {
            kui::Navigator::showDialog(&s_forget);
        } else if (label == "Servidor web") {
            LegacyScreen::openLegacy(5);  // STATE_WEB_APP (ainda legacy)
        }
    }

private:
    static kui::Dialog s_forget;
};

kui::Dialog WifiSettingsScreen::s_forget;

// ============================================================================
// Tela (brilho) — trilho arrastavel; nivel aplicado ao vivo, gravado no release
// ============================================================================

class DisplaySettingsScreen : public MenuBase {
public:
    static DisplaySettingsScreen* instance() {
        static DisplaySettingsScreen s;
        return &s;
    }

protected:
    DisplaySettingsScreen() : MenuBase("Tela") {}

    void drawContent(kui::Canvas& c) override {
        if (!Backlight::isSupported()) {
            c.text("Controle de brilho indisponivel nesta placa.", UI::cx(), UI::sy(140), kui::type::body(),
                   THEME_TEXT_DIM, MC_DATUM);
            return;
        }
        c.text("Brilho", UI::cx(), UI::sy(104), kui::type::title(), THEME_TEXT, MC_DATUM);
        c.text("arraste o trilho", UI::cx(), UI::sy(122), kui::type::caption(), THEME_TEXT_DIM, MC_DATUM);

        kui::Rect track = trackRect();
        c.fillRoundRect(track, track.h / 2, THEME_CARD);
        int fillW = track.w * Backlight::get() / 100;
        if (fillW > 4) c.fillRoundRect({track.x, track.y, fillW, track.h}, track.h / 2, THEME_ACCENT);
        c.fillCircle(track.x + fillW, track.y + track.h / 2, UI::sx(7), THEME_TEXT);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", Backlight::get());
        c.text(buf, UI::cx(), track.y + UI::sy(30), kui::type::display(), THEME_TEXT, MC_DATUM);
    }

    bool touchContent(const kui::TouchEvent& ev) override {
        if (!Backlight::isSupported()) return true;
        kui::Rect track = trackRect();
        kui::Rect zone = {track.x - UI::sx(12), track.y - UI::sy(20), track.w + UI::sx(24), track.h + UI::sy(40)};
        if (ev.type == kui::TouchEvent::Press && zone.contains(ev.x, ev.y)) m_dragging = true;
        if (ev.type == kui::TouchEvent::Drag && m_dragging) {
            int level = (ev.x - track.x) * 100 / track.w;
            if (level < 5) level = 5;
            if (level > 100) level = 100;
            Backlight::set(level, false);
            markDirty();
        }
        if (ev.type == kui::TouchEvent::Release && m_dragging) {
            m_dragging = false;
            Backlight::set(Backlight::get(), true);  // persiste
        }
        return true;
    }

private:
    kui::Rect trackRect() const { return {UI::sx(24), UI::sy(160), UI::sx(192), UI::sy(12)}; }
    bool m_dragging = false;
};

// ============================================================================
// Atualizacao (OTA)
// ============================================================================

class UpdaterScreen : public MenuBase {
public:
    static UpdaterScreen* instance() {
        static UpdaterScreen s;
        return &s;
    }

protected:
    UpdaterScreen() : MenuBase("Atualizacao") {}

    void onEnter() override {
        m_checkPending = true;  // verifica no primeiro tick (tela ja desenhada)
        markDirty();
    }

    void onTick(uint32_t) override {
        if (!m_checkPending) return;
        m_checkPending = false;
        if (!WebManager::isWifiConnected()) {
            m_state = NoWifi;
        } else {
            OtaManager::checkForUpdates();
            if (OtaManager::info.fetchFailed) m_state = Failed;
            else if (!OtaManager::info.available) m_state = UpToDate;
            else m_state = Available;
        }
        markDirty();
    }

    void drawContent(kui::Canvas& c) override {
        int midY = UI::sy(140);
        switch (m_state) {
            case NoWifi:
                c.text("Sem conexao Wi-Fi.", UI::cx(), midY, kui::type::body(), THEME_ERR, MC_DATUM);
                c.text("Conecte em Wi-Fi primeiro.", UI::cx(), midY + UI::sy(20), kui::type::body(),
                       THEME_TEXT_DIM, MC_DATUM);
                break;
            case Checking:
                c.text("Verificando atualizacoes...", UI::cx(), midY, kui::type::body(), THEME_TEXT_DIM, MC_DATUM);
                break;
            case Failed:
                c.text("Falha ao verificar", UI::cx(), midY, kui::type::title(), THEME_ERR, MC_DATUM);
                c.text("Confira a conexao e tente de novo.", UI::cx(), midY + UI::sy(20), kui::type::body(),
                       THEME_TEXT_DIM, MC_DATUM);
                break;
            case UpToDate:
                c.text("Sistema atualizado!", UI::cx(), midY, kui::type::title(), THEME_OK, MC_DATUM);
                c.text(std::string("KryonOS ") + KRYONOS_VERSION, UI::cx(), midY + UI::sy(20), kui::type::body(),
                       THEME_TEXT_DIM, MC_DATUM);
                break;
            case Available: {
                c.text(OtaManager::info.type, UI::cx(), UI::sy(74), kui::type::caption(), THEME_ACCENT, MC_DATUM);
                c.text(std::string(KRYONOS_VERSION) + "  ->  " + OtaManager::info.version, UI::cx(), UI::sy(94),
                       kui::type::title(), THEME_TEXT, MC_DATUM);

                int maxW = UI::W - UI::sx(32);
                int y = UI::sy(116);
                auto drawBlock = [&](const char* header, const std::string& body, uint32_t hcol, int yMax) {
                    if (body.empty() || y >= yMax) return;
                    c.text(header, UI::sx(16), y, kui::type::caption(), hcol, TL_DATUM);
                    y += UI::sy(15);
                    for (const std::string& ln : wrapText(c, body, kui::type::body(), maxW, 6)) {
                        if (y >= yMax) break;
                        c.text(ln, UI::sx(16), y, kui::type::body(), THEME_TEXT_DIM, TL_DATUM);
                        y += UI::sy(14);
                    }
                    y += UI::sy(4);
                };
                drawBlock("Novidades:", OtaManager::info.changelog, THEME_TEXT, UI::sy(224));
                drawBlock("Como instalar:", OtaManager::info.guide, THEME_ACCENT, UI::sy(246));

                if (OtaManager::info.hasFirmware) {
                    kui::Rect b = installRect();
                    c.fillRoundRect(b, UI::sx(8), kui::isPressed(b) ? THEME_RAISED : THEME_OK);
                    c.text("Instalar", b.x + b.w / 2, b.y + b.h / 2, kui::type::body(), THEME_TEXT, MC_DATUM);
                    c.text("nao desligue durante a atualizacao", UI::cx(), b.y - UI::sy(14), kui::type::caption(),
                           THEME_TEXT_DIM, MC_DATUM);
                } else {
                    c.text("Canal sem firmware: siga o guia acima.", UI::cx(), installRect().y, kui::type::body(),
                           THEME_TEXT_DIM, MC_DATUM);
                }
                break;
            }
            case Installing: {
                c.text("Atualizando sistema", UI::cx(), UI::sy(100), kui::type::title(), THEME_TEXT, MC_DATUM);
                c.text("Nao desligue a alimentacao!", UI::cx(), UI::sy(122), kui::type::body(), THEME_ERR, MC_DATUM);
                kui::Rect bar = progressRect();
                c.drawRoundRect(bar, UI::sx(4), THEME_STROKE);
                break;
            }
        }

        if (m_state != Installing) {
            kui::Rect rb = recheckRect();
            c.fillRoundRect(rb, UI::sx(8), kui::isPressed(rb) ? THEME_RAISED : THEME_CARD);
            c.drawRoundRect(rb, UI::sx(8), THEME_STROKE);
            c.text("Verificar novamente", rb.x + rb.w / 2, rb.y + rb.h / 2, kui::type::body(), THEME_TEXT, MC_DATUM);
        }
    }

    bool touchContent(const kui::TouchEvent& ev) override {
        if (!(ev.type == kui::TouchEvent::Release && ev.isTap())) return true;
        if (m_state != Installing && recheckRect().contains(ev.x, ev.y)) {
            m_state = Checking;
            m_checkPending = true;
            markDirty();
            return true;
        }
        if (m_state == Available && OtaManager::info.hasFirmware && installRect().contains(ev.x, ev.y)) {
            startInstall();
        }
        return true;
    }

private:
    enum State { Checking, NoWifi, Failed, UpToDate, Available, Installing };
    static int s_lastPercent;

    kui::Rect recheckRect() const { return {UI::sx(40), UI::sy(288), UI::sx(160), UI::sy(26)}; }
    kui::Rect installRect() const { return {UI::sx(60), UI::sy(258), UI::sx(120), UI::sy(30)}; }
    static kui::Rect progressRect() { return {UI::sx(30), UI::sy(160), UI::sx(180), UI::sy(18)}; }

    // Progresso pintado direto no display (dentro de performUpdate, sem
    // passar pelo Navigator) — mesma geometria de progressRect().
    static void progressCb(int percent) {
        if (percent == s_lastPercent) return;
        s_lastPercent = percent;
        KryonDisplay& tft = Board::display();
        kui::Rect bar = progressRect();
        int fillW = bar.w * percent / 100;
        if (fillW > UI::sx(4)) {
            tft.fillRect(bar.x + UI::sx(2), bar.y + UI::sy(2), fillW - UI::sx(4), bar.h - UI::sy(4), THEME_ACCENT);
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", percent);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(THEME_TEXT, THEME_BG);
        tft.drawString(buf, UI::cx(), bar.y - UI::sy(14), lgfx::fontdata[UI::font(2)]);
    }

    void startInstall() {
        m_state = Installing;
        s_lastPercent = -1;
        markDirty();
        kui::Navigator::repaint();  // desenha a tela de instalacao ANTES de bloquear

        bool ok = OtaManager::performUpdate(OtaManager::info.firmwareUrl, progressCb);
        if (ok) {
            kui::Navigator::toast("Atualizado! Reiniciando...", THEME_OK);
            delay(1500);
            ESP.restart();
            return;
        }
        m_state = Failed;
        markDirty();
        kui::Navigator::toast("Falhou: " + OtaManager::lastError, THEME_ERR, 5000);
    }

    State m_state = Checking;
    bool m_checkPending = false;
};

int UpdaterScreen::s_lastPercent = -1;

// ============================================================================
// Hora e fuso
// ============================================================================

struct TZEntry {
    const char* label;
    const char* value;
};
const TZEntry tzList[] = {
    {"UTC-12 Baker Is", "UTC12"},  {"UTC-11 Midway", "UTC11"},   {"UTC-10 Hawaii", "UTC10"},
    {"UTC-9 Alaska", "UTC9"},      {"UTC-8 PST", "UTC8"},        {"UTC-7 MST", "UTC7"},
    {"UTC-6 CST", "UTC6"},         {"UTC-5 EST", "UTC5"},        {"UTC-4 AST", "UTC4"},
    {"UTC-3 BRT", "UTC3"},         {"UTC-2", "UTC2"},            {"UTC-1 AZOT", "UTC1"},
    {"UTC+0 GMT", "UTC0"},         {"UTC+1 CET", "UTC-1"},       {"UTC+2 EET", "UTC-2"},
    {"UTC+3 MSK", "UTC-3"},        {"UTC+4 GST", "UTC-4"},       {"UTC+5 PKT", "UTC-5"},
    {"UTC+5:30 IST", "UTC-5:30"},  {"UTC+6 BST", "UTC-6"},       {"UTC+7 ICT", "UTC-7"},
    {"UTC+8 CST/AWST", "UTC-8"},   {"UTC+9 JST", "UTC-9"},       {"UTC+10 AEST", "UTC-10"},
    {"UTC+11 AEDT", "UTC-11"},     {"UTC+12 NZST", "UTC-12"},
};
const int tzCount = (int)(sizeof(tzList) / sizeof(TZEntry));

class TzPickerScreen : public ListMenuBase {
public:
    static TzPickerScreen* instance() {
        static TzPickerScreen s;
        return &s;
    }

protected:
    TzPickerScreen() : ListMenuBase("Fuso horario") {}

    void onEnter() override {
        m_list.items.clear();
        m_list.selected = -1;
        for (int i = 0; i < tzCount; i++) {
            m_list.items.push_back({tzList[i].label, "", -1});
            if (tzList[i].value == TimeManager::currentTimezone) m_list.selected = i;
        }
        m_list.onSelect = [](int idx) {
            TimeManager::setTimezone(tzList[idx].value);
            kui::Navigator::toast("Fuso aplicado", THEME_OK);
            kui::Navigator::pop();
        };
        markDirty();
    }
};

// Ajuste manual — steppers com triangulos; draw e hit-test usam a mesma
// geometria (stepperRect/subRect).
class ManualTimeScreen : public MenuBase {
public:
    static ManualTimeScreen* instance() {
        static ManualTimeScreen s;
        return &s;
    }

protected:
    ManualTimeScreen() : MenuBase("Ajuste manual") {}

    void onEnter() override {
        m_val[0] = TimeManager::getDay();
        m_val[1] = TimeManager::getMonth();
        m_val[2] = TimeManager::getYear();
        time_t now;
        time(&now);
        struct tm tinfo;
        localtime_r(&now, &tinfo);
        m_val[3] = tinfo.tm_hour;
        m_val[4] = tinfo.tm_min;
        markDirty();
    }

    void drawContent(kui::Canvas& c) override {
        static const char* names[5] = {"dia", "mes", "ano", "hora", "min"};
        for (int i = 0; i < 5; i++) {
            kui::Rect r = stepperRect(i);
            c.fillRoundRect(r, UI::sx(8), THEME_CARD);

            int cx = r.x + r.w / 2;
            c.fillTriangle(cx, r.y + UI::sy(8), cx + UI::sx(7), r.y + UI::sy(18), cx - UI::sx(7), r.y + UI::sy(18),
                           kui::isPressed(subRect(r, Up)) ? THEME_ACCENT : THEME_TEXT_DIM);
            char buf[8];
            snprintf(buf, sizeof(buf), i == 4 ? "%02d" : "%d", m_val[i]);
            c.text(buf, cx, r.y + r.h / 2, kui::type::display(), THEME_TEXT, MC_DATUM);
            c.fillTriangle(cx - UI::sx(7), r.y + r.h - UI::sy(8), cx + UI::sx(7), r.y + r.h - UI::sy(8), cx,
                           r.y + r.h - UI::sy(18),
                           kui::isPressed(subRect(r, Down)) ? THEME_ACCENT : THEME_TEXT_DIM);
            c.text(names[i], cx, r.y - UI::sy(8), kui::type::caption(), THEME_TEXT_DIM, BC_DATUM);
        }

        kui::Rect save = saveRect();
        c.fillRoundRect(save, UI::sx(8), kui::isPressed(save) ? THEME_ACCENT : THEME_OK);
        c.text("Salvar", save.x + save.w / 2, save.y + save.h / 2, kui::type::body(), THEME_TEXT, MC_DATUM);
    }

    bool touchContent(const kui::TouchEvent& ev) override {
        if (!(ev.type == kui::TouchEvent::Release && ev.isTap())) return true;
        static const int vmin[5] = {1, 1, 2000, 0, 0};
        static const int vmax[5] = {31, 12, 2100, 23, 59};
        for (int i = 0; i < 5; i++) {
            kui::Rect r = stepperRect(i);
            if (subRect(r, Up).contains(ev.x, ev.y)) {
                m_val[i] = (m_val[i] >= vmax[i]) ? vmin[i] : m_val[i] + 1;
                markDirty();
                return true;
            }
            if (subRect(r, Down).contains(ev.x, ev.y)) {
                m_val[i] = (m_val[i] <= vmin[i]) ? vmax[i] : m_val[i] - 1;
                markDirty();
                return true;
            }
        }
        if (saveRect().contains(ev.x, ev.y)) {
            TimeManager::setManualTime(m_val[2], m_val[1], m_val[0], m_val[3], m_val[4]);
            kui::Navigator::toast("Hora ajustada", THEME_OK);
            kui::Navigator::pop();
        }
        return true;
    }

private:
    enum Sub { Up, Down };

    // 0..2 = linha da data (dia/mes/ano), 3..4 = linha da hora
    kui::Rect stepperRect(int i) const {
        if (i < 3) return {UI::sx(14 + i * 76), UI::sy(78), UI::sx(62), UI::sy(84)};
        return {UI::sx(48 + (i - 3) * 84), UI::sy(198), UI::sx(64), UI::sy(84)};
    }
    static kui::Rect subRect(const kui::Rect& r, Sub s) {
        return {r.x, s == Up ? r.y : r.y + r.h / 2, r.w, r.h / 2};
    }
    kui::Rect saveRect() const { return {UI::sx(60), UI::sy(292), UI::sx(120), UI::sy(26)}; }

    int m_val[5] = {1, 1, 2026, 12, 0};
};

class TimeSettingsScreen : public ListMenuBase {
public:
    static TimeSettingsScreen* instance() {
        static TimeSettingsScreen s;
        return &s;
    }

protected:
    TimeSettingsScreen() : ListMenuBase("Hora e fuso") {}

    void onEnter() override { rebuild(); }

    void onTick(uint32_t dtMs) override {
        ListMenuBase::onTick(dtMs);
        if (currentMinute() != m_lastMinute) rebuild();
    }

    void rebuild() {
        m_lastMinute = currentMinute();
        m_list.items.clear();
        m_list.items.push_back({"Agora", TimeManager::getFormattedTime(), -1, false});
        m_list.items.push_back({"NTP (internet)", TimeManager::ntpEnabled ? "ON" : "OFF", -1});
        m_list.items.push_back({"Fuso", TimeManager::currentTimezone, -1});
        m_list.items.push_back({"Formato", TimeManager::use24hFormat ? "24h" : "12h", -1});
        m_list.items.push_back({"Ajuste manual", "", -1, !TimeManager::ntpEnabled});
        m_list.onSelect = [this](int idx) { select(idx); };
        markDirty();
    }

    void select(int idx) {
        switch (idx) {
            case 1:
                TimeManager::setNTPEnabled(!TimeManager::ntpEnabled);
                rebuild();
                break;
            case 2: kui::Navigator::push(TzPickerScreen::instance()); break;
            case 3:
                TimeManager::setTimeFormat(!TimeManager::use24hFormat);
                rebuild();
                break;
            case 4: kui::Navigator::push(ManualTimeScreen::instance()); break;
        }
    }

private:
    static int currentMinute() {
        time_t now;
        time(&now);
        struct tm tinfo;
        localtime_r(&now, &tinfo);
        return tinfo.tm_hour * 60 + tinfo.tm_min;
    }
    int m_lastMinute = -1;
};

// ============================================================================
// Seguranca (PIN)
// ============================================================================

class SecuritySettingsScreen : public ListMenuBase {
public:
    static SecuritySettingsScreen* instance() {
        static SecuritySettingsScreen s;
        return &s;
    }

protected:
    SecuritySettingsScreen() : ListMenuBase("Seguranca") {}

    void onEnter() override { rebuild(); }

    void rebuild() {
        m_list.items.clear();
        if (pinIsSet()) {
            m_list.items.push_back({"PIN do Settings", "ativo", -1, false});
            m_list.items.push_back({"Alterar PIN", "", -1});
            m_list.items.push_back({"Remover PIN", "", -1});
        } else {
            m_list.items.push_back({"PIN do Settings", "desativado", -1, false});
            m_list.items.push_back({"Definir PIN", "", -1});
        }
        m_list.onSelect = [this](int idx) { select(idx); };
        markDirty();
    }

    void select(int idx) {
        if (!pinIsSet()) {
            if (idx == 1) pushNewPinFlow([this]() { rebuild(); });
            return;
        }
        if (idx == 1) {  // alterar
            pushCurrentPinThen([] { pushNewPinFlow(nullptr); });
        } else if (idx == 2) {  // remover
            pushCurrentPinThen([this]() {
                FileSystem::deleteFile(PIN_FILE);
                kui::Navigator::toast("PIN removido", THEME_OK);
                rebuild();
            });
        }
    }
};

// ============================================================================
// Home do Settings
// ============================================================================

class SettingsHomeScreen : public ListMenuBase {
public:
    static SettingsHomeScreen* instance() {
        static SettingsHomeScreen s;
        return &s;
    }

protected:
    SettingsHomeScreen() : ListMenuBase("Settings") {}

    void onEnter() override { rebuild(); }

    void rebuild() {
        bool wifiOff = FileSystem::exists("/local/nowifi.txt");
        m_list.items.clear();
        m_list.items.push_back({"Wi-Fi", wifiOff ? "OFF" : (WebManager::isActive() ? "ON" : "..."), -1});
        m_list.items.push_back({"Aplicativos", "", -1});
        m_list.items.push_back({"Hora e fuso", "", -1});
        m_list.items.push_back({"Seguranca", pinIsSet() ? "PIN" : "--", -1});
        m_list.items.push_back({"Tela", "", -1});
        m_list.items.push_back({"Atualizacao", "", -1});
        m_list.items.push_back({"Sobre", "", -1});
        if (!Board::profile().capacitiveTouch) m_list.items.push_back({"Calibrar touch", "", -1});
        m_list.onSelect = [this](int idx) { select(idx); };
        markDirty();
    }

    void select(int idx) {
        switch (idx) {
            case 0: kui::Navigator::push(WifiSettingsScreen::instance()); break;
            case 1: kui::Navigator::push(AppsSettingsScreen::instance()); break;
            case 2: kui::Navigator::push(TimeSettingsScreen::instance()); break;
            case 3: kui::Navigator::push(SecuritySettingsScreen::instance()); break;
            case 4: kui::Navigator::push(DisplaySettingsScreen::instance()); break;
            case 5: kui::Navigator::push(UpdaterScreen::instance()); break;
            case 6: kui::Navigator::push(AboutScreen::instance()); break;
            case 7:
                TouchCalibrator::runCalibration();
                kui::Navigator::repaint();
                break;
        }
    }
};

// ============================================================================
// Entrada com gate de PIN
// ============================================================================

void SettingsScreens::open() {
    if (!pinIsSet() || millis() < s_unlockedUntilMs) {
        kui::Navigator::push(SettingsHomeScreen::instance());
        return;
    }
    pushPinPad("PIN do Settings", [](const std::string& pin, bool ok) {
        kui::Navigator::pop();  // tira o teclado numerico
        if (!ok) return;
        if (pinVerify(pin)) {
            s_unlockedUntilMs = millis() + 60000UL;  // sessao de 60 s
            kui::Navigator::push(SettingsHomeScreen::instance());
        } else {
            kui::Navigator::toast("PIN incorreto", THEME_ERR);
        }
    });
}
