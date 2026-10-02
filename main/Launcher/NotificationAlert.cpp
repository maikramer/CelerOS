#include "NotificationAlert.h"
#include "LauncherUI.h"
#include "WatchPanels.h"
#include "../Display/Backlight.h"
#include "../Display/ScreenPower.h"
#include "../Display/Theme.h"
#include "../Kernel/Notifications.h"
#include "../Kernel/TimeManager.h"
#include "../UI/Kui.h"
#include <Arduino.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kStaleMs = 15000;  // pedido mais velho que isso morre
constexpr uint32_t kShowMs = 8000;    // sem toque: volta a dormir
constexpr int kBodyLines = 6;

volatile bool s_req = false;
volatile uint32_t s_reqAt = 0;  // escrito/lido de tasks diferentes (BLE -> UI)

// Quebra por palavra (mesmo algoritmo da central de notificacoes); a ultima
// linha leva o resto com "..".
std::vector<std::string> wrapText(kui::Canvas& c, const std::string& s, const lgfx::IFont* f,
                                  int maxW, int maxLines) {
    std::vector<std::string> out;
    std::string rest = s;
    for (int ln = 0; ln < maxLines && !rest.empty(); ln++) {
        size_t cut = rest.size();
        while (cut > 0 && c.textWidth(rest.substr(0, cut).c_str(), f) > maxW) {
            size_t sp = rest.rfind(' ', cut - 1);
            cut = (sp == std::string::npos || sp == 0) ? cut - 1 : sp;
        }
        if (cut == 0) cut = 1;
        std::string line = rest.substr(0, cut);
        rest = rest.substr(cut);
        while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        if (ln == maxLines - 1 && !rest.empty()) line = c.ellipsize(line + " " + rest, f, maxW);
        out.push_back(line);
    }
    return out;
}

class AlertScreen : public kui::Screen {
public:
    void onEnter() override {
        m_wasAsleep = ScreenPower::state() <= 2;
        Backlight::noteActivity();  // acorda: edges do tick fazem o resto
        load();
        m_ageMs = 0;
    }

    // Notificacao nova com o alerta ja na tela: troca o conteudo e renova.
    void refresh() {
        load();
        m_ageMs = 0;
        markDirty();
    }

    void draw(kui::Canvas& c) override {
        c.fill(THEME_BG);
        // primeiro meio segundo: repinta enquanto o painel acorda (o push
        // inicial pode ter ido ao vidro ainda em SLPIN)
        if (m_ageMs < 500) markDirty();

        const int m = UI::sx(10) + UI::inset / 2;
        const int top = UI::sy(22) + UI::inset / 3;
        const int bot = UI::H - UI::sy(30) - UI::inset / 3;
        const kui::Rect card = {m, top, UI::W - 2 * m, bot - top};
        c.fillRoundRect(card, UI::sx(14), THEME_CARD);
        c.fillRoundRect({card.x + UI::sx(7), card.y + UI::sy(12), UI::sx(3), card.h - UI::sy(24)},
                        1, THEME_ACCENT);

        const int x = card.x + UI::sx(18);
        const int w = card.w - UI::sx(30);
        // um degrau acima do padrao das outras telas: alerta e leitura de
        // relance no pulso (origem/corpo em body, titulo em title)
        const lgfx::IFont* fc = kui::type::body();
        const lgfx::IFont* ft = kui::type::title();

        int y = card.y + UI::sy(12);
        std::string src = m_src.empty() ? "Aviso" : m_src;
        c.text(c.ellipsize(src, fc, w - UI::sx(70)), x, y, fc, THEME_ACCENT, ML_DATUM);
        c.text(TimeManager::getFormattedTime(), card.x + card.w - UI::sx(14), y, fc,
               THEME_TEXT_DIM, MR_DATUM);
        y += UI::sy(26);

        std::string title = m_title.empty() ? m_msg : m_title;
        for (const std::string& ln : wrapText(c, title, ft, w, 2)) {
            c.text(ln, x, y, ft, THEME_TEXT, ML_DATUM);
            y += UI::sy(26);
        }
        y += UI::sy(8);
        if (!m_msg.empty() && !m_title.empty()) {
            for (const std::string& ln : wrapText(c, m_msg, fc, w, kBodyLines)) {
                c.text(ln, x, y, fc, THEME_TEXT_DIM, ML_DATUM);
                y += UI::sy(19);
            }
        }

        if (m_unread > 1) {
            char b[24];
            snprintf(b, sizeof(b), "e mais %d", m_unread - 1);
            c.text(b, card.x + card.w / 2, card.y + card.h - UI::sy(14), fc, THEME_ACCENT,
                   MC_DATUM);
        }
        c.text("toque: central", UI::cx(), UI::H - UI::sy(12) - UI::inset / 3, fc, THEME_STROKE,
               MC_DATUM);
    }

    bool onTouch(const kui::TouchEvent& ev) override {
        if (ev.type != kui::TouchEvent::Release) return true;
        if (ev.isTap()) {
            // central com volta para a casa (WatchPanels::service empilha no
            // proximo tick; o pop abaixo fecha o alerta)
            kui::Navigator::pop();
            WatchPanels::request(WatchPanels::Panel::Notifications, true);
        } else {
            kui::Navigator::pop();  // swipe dispensa o alerta
        }
        return true;
    }

    void onTick(uint32_t dtMs) override {
        ScreenPower::keepAwakeFor(2500);
        m_ageMs += dtMs;
        if (m_ageMs >= kShowMs) {
            kui::Navigator::pop();
            if (m_wasAsleep) ScreenPower::sleepNow();  // ninguem tocou: dorme
        }
    }

    bool allowsBackGesture() const override { return false; }

private:
    void load() {
        m_title.clear();
        m_msg.clear();
        m_src.clear();
        m_unread = 0;
        for (const Notifications::Note& n : Notifications::list()) {
            if (n.read) continue;
            if (m_unread == 0) {
                m_title = n.title;
                m_msg = n.msg;
                m_src = n.src.rfind("phone:", 0) == 0 && n.src.size() > 6 ? n.src.substr(6)
                                                                         : std::string(n.src);
            }
            m_unread++;
        }
    }

    std::string m_title, m_msg, m_src;
    int m_unread = 0;
    uint32_t m_ageMs = 0;
    bool m_wasAsleep = false;
};
AlertScreen s_alert;

}  // namespace

namespace NotificationAlert {

void request() {
    s_reqAt = millis();
    s_req = true;
}

void service(bool inApp) {
    if (!s_req) return;
    if (millis() - s_reqAt > kStaleMs) {
        s_req = false;
        return;
    }
    if (inApp) {
        // tela acesa e usuario dentro de um app: nao interrompe e nao fica
        // pendente (o toast do push ja avisou; sai do app = sem alerta
        // surpresa depois). Dormindo/dim/AOD: sai do app pelo caminho limpo
        // e o celerLoop empilha o alerta.
        if (ScreenPower::state() >= 3) {
            s_req = false;
            return;
        }
        LauncherUI::requestAppExit();
        return;
    }
    s_req = false;
    if (kui::Navigator::top() == &s_alert) {
        s_alert.refresh();
        return;
    }
    kui::Navigator::push(&s_alert);
}

}  // namespace NotificationAlert
