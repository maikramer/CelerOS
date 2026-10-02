#include "AlarmScreen.h"
#include "../Kernel/Alarms.h"
#include "../Kernel/TimeManager.h"
#include "../Display/Backlight.h"
#include "../Display/ScreenPower.h"
#include "../Display/Theme.h"
#include "../Hardware/BoardIO.h"
#include <Arduino.h>

namespace {
AlarmScreen s_screen;
constexpr uint32_t kBeepEveryMs = 1500;
constexpr uint32_t kAutoSnoozeMs = 120000;  // 2 min sem resposta
}  // namespace

void AlarmScreen::service() {
    if (!Alarms::ringing()) return;
    if (kui::Navigator::top() == &s_screen) return;
    Backlight::noteActivity();  // acorda o vidro (o ScreenPower segue o edge)
    kui::Navigator::push(&s_screen);
}

void AlarmScreen::onEnter() {
    Alarms::Ring r;
    Alarms::ringing(&r);
    m_label = r.label;
    m_canSnooze = r.kind != Alarms::Kind::Timer;
    m_kind = r.kind == Alarms::Kind::Timer ? "Timer" : r.kind == Alarms::Kind::Snooze ? "Soneca" : "Alarme";
    m_startMs = millis();
    m_beepAccum = kBeepEveryMs;  // bipa ja no primeiro tick
    m_done = false;
    Backlight::noteActivity();
}

void AlarmScreen::onExit() {
    // Saiu sem escolher (BOOT/home): para o toque
    if (!m_done) Alarms::dismiss();
    m_done = true;
}

kui::Rect AlarmScreen::snoozeRect() const {
    const int m = UI::sx(16) + UI::inset / 2;
    return {m, UI::sy(200), UI::W - 2 * m, UI::sy(44)};
}

kui::Rect AlarmScreen::stopRect() const {
    const int m = UI::sx(16) + UI::inset / 2;
    return {m, UI::sy(256), UI::W - 2 * m, UI::sy(44)};
}

void AlarmScreen::draw(kui::Canvas& c) {
    c.fill(THEME_BG);
    c.text(m_kind, UI::cx(), UI::sy(40) + UI::inset / 2, kui::type::title(), THEME_WARN, MC_DATUM);
    c.text(TimeManager::getFormattedTime(), UI::cx(), UI::sy(100), kui::type::display(), THEME_TEXT, MC_DATUM);
    if (!m_label.empty()) {
        c.text(m_label, UI::cx(), UI::sy(150), kui::type::body(), THEME_TEXT_DIM, MC_DATUM);
    }
    if (m_canSnooze) {
        kui::Rect s = snoozeRect();
        c.fillRoundRect(s, s.h / 2, kui::isPressed(s) ? THEME_RAISED : THEME_CARD);
        c.text("Soneca 5 min", s.x + s.w / 2, s.y + s.h / 2, kui::type::body(), THEME_TEXT, MC_DATUM);
    }
    kui::Rect p = stopRect();
    c.fillRoundRect(p, p.h / 2, kui::isPressed(p) ? THEME_ACCENT_D : THEME_ACCENT);
    c.text("Parar", p.x + p.w / 2, p.y + p.h / 2, kui::type::body(), THEME_ON_ACCENT, MC_DATUM);
}

bool AlarmScreen::onTouch(const kui::TouchEvent& ev) {
    if (ev.type != kui::TouchEvent::Release) {
        markDirty();  // feedback de pressionado
        return true;
    }
    if (!ev.isTap()) return true;
    if (stopRect().contains(ev.x, ev.y)) {
        finish(false);
    } else if (m_canSnooze && snoozeRect().contains(ev.x, ev.y)) {
        finish(true);
    }
    return true;
}

void AlarmScreen::onTick(uint32_t dtMs) {
    if (m_done) return;
    if (!Alarms::ringing()) {  // dispensado por outro caminho
        m_done = true;
        kui::Navigator::pop();
        return;
    }
    ScreenPower::keepAwakeFor(3000);  // sem dim/AOD enquanto toca
    if (millis() - m_startMs >= kAutoSnoozeMs) {
        finish(m_canSnooze);
        return;
    }
    m_beepAccum += dtMs;
    if (m_beepAccum >= kBeepEveryMs) {
        m_beepAccum = 0;
        for (int i = 0; i < 3; i++) {
            BoardIO::tone(1800, 120);
            delay(80);
        }
        markDirty();  // relogio na tela acompanha
    }
}

void AlarmScreen::finish(bool snooze) {
    if (m_done) return;
    m_done = true;
    if (snooze) {
        Alarms::snooze();
        kui::Navigator::toast("Soneca: 5 min", THEME_ACCENT, 2000);
    } else {
        Alarms::dismiss();
    }
    kui::Navigator::pop();
}
