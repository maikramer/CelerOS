#include "PhoneScreens.h"
#include "../Bluetooth/PhoneLink.h"
#include "../Display/Backlight.h"
#include "../Display/ScreenPower.h"
#include "../Display/Theme.h"
#include "../Hardware/BoardIO.h"
#include "../UI/Kui.h"
#include <Arduino.h>
#include <stdio.h>
#include <string>

namespace {

// Digitos grandes: fonte 8 (7-seg 75 px) no vidro grande; 4 nos pequenos
uint8_t bigDigitsFont() { return UI::W >= 400 ? 8 : 4; }

class PairScreen : public kui::Screen {
public:
    void onEnter() override { Backlight::noteActivity(); }
    void draw(kui::Canvas& c) override {
        c.fill(THEME_BG);
        char b[12];
        snprintf(b, sizeof(b), "%06lu", (unsigned long)PhoneLink::passkey());
        c.text("Código", UI::cx(), UI::sy(70) + UI::inset / 3, kui::type::body(), THEME_TEXT_DIM, MC_DATUM);
        c.text(b, UI::cx(), UI::cy(), bigDigitsFont(), THEME_TEXT, MC_DATUM);
        c.text("digite no celular", UI::cx(), UI::H - UI::sy(70) - UI::inset / 3, kui::type::caption(),
               THEME_TEXT_DIM, MC_DATUM);
    }
    bool onTouch(const kui::TouchEvent&) override { return true; }
    void onTick(uint32_t) override {
        ScreenPower::keepAwakeFor(3000);
        if (PhoneLink::passkey() == 0) {
            kui::Navigator::pop();
            if (PhoneLink::connected()) kui::Navigator::toast("Celular pareado", THEME_OK, 2500);
        }
    }
    bool allowsBackGesture() const override { return false; }
};
PairScreen s_pair;

class CallScreen : public kui::Screen {
public:
    void onEnter() override {
        m_ringAccum = 1500;  // toca ja no primeiro tick
        m_done = false;
        Backlight::noteActivity();
        PhoneLink::callInfo(m_name, m_number);
    }
    void onExit() override {
        // saiu sem escolher (BOOT/home): so para de tocar aqui
        m_done = true;
    }
    void draw(kui::Canvas& c) override {
        c.fill(THEME_BG);
        const int top = UI::sy(56) + UI::inset / 3;
        c.text("Chamada", UI::cx(), top, kui::type::body(), THEME_TEXT_DIM, MC_DATUM);
        const std::string who = m_name.empty() ? m_number : m_name;
        c.text(c.ellipsize(who.empty() ? "Desconhecido" : who, kui::type::title(), UI::W - UI::sx(30) - UI::inset),
               UI::cx(), UI::sy(120), kui::type::title(), THEME_TEXT, MC_DATUM);
        if (!m_name.empty() && !m_number.empty()) {
            c.text(m_number, UI::cx(), UI::sy(150), kui::type::caption(), THEME_TEXT_DIM, MC_DATUM);
        }
        kui::Rect r = rejectRect(), a = acceptRect();
        c.fillCircle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, kui::isPressed(r) ? 0x902020u : THEME_ERR);
        c.fillCircle(a.x + a.w / 2, a.y + a.h / 2, a.w / 2, kui::isPressed(a) ? 0x107040u : THEME_OK);
        c.text("Recusar", r.x + r.w / 2, r.y + r.h + UI::sy(12), kui::type::caption(), THEME_TEXT, MC_DATUM);
        c.text("Atender", a.x + a.w / 2, a.y + a.h + UI::sy(12), kui::type::caption(), THEME_TEXT, MC_DATUM);
    }
    bool onTouch(const kui::TouchEvent& ev) override {
        if (ev.type != kui::TouchEvent::Release) {
            markDirty();
            return true;
        }
        if (!ev.isTap()) return true;
        if (rejectRect().contains(ev.x, ev.y)) finish(0);
        else if (acceptRect().contains(ev.x, ev.y)) finish(1);
        return true;
    }
    void onTick(uint32_t dtMs) override {
        if (m_done) return;
        std::string n, num;
        if (!PhoneLink::callInfo(n, num)) {  // celular avisou o fim
            m_done = true;
            kui::Navigator::pop();
            return;
        }
        ScreenPower::keepAwakeFor(3000);
        m_ringAccum += dtMs;
        if (m_ringAccum >= 1500) {
            m_ringAccum = 0;
            BoardIO::toneAsync(1200, 200, 80);  // fora do tick: UI livre p/ tocar
            BoardIO::toneAsync(1500, 200);
        }
    }
    bool allowsBackGesture() const override { return false; }

private:
    kui::Rect rejectRect() const {
        const int d = UI::sx(64);
        return {UI::cx() - UI::sx(52) - d / 2, UI::sy(200), d, d};
    }
    kui::Rect acceptRect() const {
        const int d = UI::sx(64);
        return {UI::cx() + UI::sx(52) - d / 2, UI::sy(200), d, d};
    }
    void finish(int action) {
        if (m_done) return;
        m_done = true;
        BoardIO::toneStop();  // nao segue bipando apos atender/recusar
        PhoneLink::callAnswer(action);
        kui::Navigator::pop();
    }
    std::string m_name, m_number;
    uint32_t m_ringAccum = 0;
    bool m_done = false;
};
CallScreen s_call;

}  // namespace

namespace PhoneScreens {

void service() {
    kui::Screen* top = kui::Navigator::top();
    std::string n, num;
    if (PhoneLink::callInfo(n, num)) {
        if (top != &s_call) kui::Navigator::push(&s_call);
        return;
    }
    if (PhoneLink::passkey() != 0 && top != &s_pair && top != &s_call) kui::Navigator::push(&s_pair);
}

}  // namespace PhoneScreens
