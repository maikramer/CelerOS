#include "Kui.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>

namespace kui {

// ============================================================ Canvas =======

Canvas::Canvas(KryonDisplay& dev) : m_dev(dev) {}

void Canvas::begin() {
    if (m_sprite == nullptr) {
        // Sprite full-screen quando cabe (PSRAM no S3; no CYD tenta o heap
        // e cai no modo direto se nao houver memoria)
        m_sprite = new KryonSprite(&m_dev);
        m_sprite->setColorDepth(16);
        if (m_sprite->createSprite(m_dev.width(), m_dev.height()) == nullptr) {
            delete m_sprite;
            m_sprite = nullptr;
        }
    }
    m_active = true;
    if (m_sprite != nullptr) {
        m_sprite->fillScreen(THEME_BG);
    } else {
        m_dev.fillScreen(THEME_BG);
    }
}

void Canvas::end() {
    if (m_sprite != nullptr) m_sprite->pushSprite(0, 0);
    m_active = false;
}

lgfx::LGFXBase* Canvas::target() {
    return m_sprite != nullptr ? (lgfx::LGFXBase*)m_sprite : (lgfx::LGFXBase*)&m_dev;
}

void Canvas::fill(uint32_t color) { target()->fillScreen(color); }

void Canvas::fillRect(const Rect& r, uint32_t color) { target()->fillRect(r.x, r.y, r.w, r.h, color); }

void Canvas::fillRoundRect(const Rect& r, int radius, uint32_t color) {
    target()->fillRoundRect(r.x, r.y, r.w, r.h, radius, color);
}

void Canvas::drawRoundRect(const Rect& r, int radius, uint32_t color) {
    target()->drawRoundRect(r.x, r.y, r.w, r.h, radius, color);
}

void Canvas::drawRect(const Rect& r, uint32_t color) { target()->drawRect(r.x, r.y, r.w, r.h, color); }

void Canvas::drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    target()->drawLine(x0, y0, x1, y1, color);
}

void Canvas::drawCircle(int cx, int cy, int r, uint32_t color) { target()->drawCircle(cx, cy, r, color); }

void Canvas::fillCircle(int cx, int cy, int r, uint32_t color) { target()->fillCircle(cx, cy, r, color); }

void Canvas::text(const char* s, int x, int y, uint8_t font, uint32_t color, int datum) {
    lgfx::LGFXBase* t = target();
    t->setTextDatum(datum);
    t->setTextColor(color);  // transparente: sem cor de fundo
    t->drawString(s, x, y, lgfx::fontdata[font]);
}

void Canvas::text(const std::string& s, int x, int y, uint8_t font, uint32_t color, int datum) {
    text(s.c_str(), x, y, font, color, datum);
}

int Canvas::textWidth(const char* s, uint8_t font) {
    return target()->textWidth(s, lgfx::fontdata[font]);
}

int Canvas::width() const { return m_dev.width(); }

int Canvas::height() const { return m_dev.height(); }

// ============================================================ Widgets ======

void Button::draw(Canvas& c) {
    uint32_t fillc, textc;
    switch (style) {
        case Primary: fillc = THEME_ACCENT; textc = 0x08222A; break;
        case Danger: fillc = THEME_ERR; textc = 0xFFFFFF; break;
        default: fillc = THEME_CARD; textc = THEME_TEXT; break;
    }
    c.fillRoundRect(rect, UI::sx(8), fillc);
    if (style == Ghost) c.drawRoundRect(rect, UI::sx(8), THEME_STROKE);
    c.text(label, rect.x + rect.w / 2, rect.y + rect.h / 2, UI::font(2), textc, MC_DATUM);
}

bool Button::onTouch(const TouchEvent& ev, Rect myRect) {
    if (ev.isTap() && myRect.contains(ev.x, ev.y)) {
        if (onTap) onTap();
        return true;
    }
    return false;
}

void Label::draw(Canvas& c) {
    c.text(text, rect.x, rect.y, UI::font(font), color, datum);
}

int List::visibleRows(const Rect& myRect) const {
    int rows = myRect.h / rowH();
    return rows < 1 ? 1 : rows;
}

void List::ensureVisible(int idx) {
    int rows = visibleRows(rect);
    if (idx < top) top = idx;
    if (idx >= top + rows) top = idx - rows + 1;
    if (top > (int)items.size() - 1) top = (int)items.size() - 1;
    if (top < 0) top = 0;
}

void List::draw(Canvas& c) {
    c.fillRoundRect(rect, UI::sx(8), THEME_CARD);
    int rows = visibleRows(rect);
    int maxTop = (int)items.size() - rows;
    if (maxTop < 0) maxTop = 0;
    if (top > maxTop) top = maxTop;
    for (int row = 0; row < rows; row++) {
        int idx = top + row;
        if (idx >= (int)items.size()) break;
        int y = rect.y + row * rowH();
        if (idx == selected) {
            c.fillRect({rect.x + UI::sx(4), y + UI::sy(2), rect.w - UI::sx(8), rowH() - UI::sy(4)}, THEME_ACCENT_D);
        }
        uint32_t color = items[idx].enabled ? THEME_TEXT : THEME_TEXT_DIM;
        c.text(items[idx].label, rect.x + UI::sx(10), y + rowH() / 2, UI::font(2), color, ML_DATUM);
        if (!items[idx].right.empty()) {
            c.text(items[idx].right, rect.x + rect.w - UI::sx(10), y + rowH() / 2, UI::font(1), THEME_TEXT_DIM, MR_DATUM);
        }
    }
    // indicador de scroll
    if ((int)items.size() > rows) {
        int barH = rect.h * rows / (int)items.size();
        int barY = rect.y + (rect.h - barH) * top / maxTop;
        c.fillRect({rect.x + rect.w - UI::sx(3), barY, UI::sx(2), barH}, THEME_STROKE);
    }
}

bool List::onTouch(const TouchEvent& ev, Rect myRect) {
    if (ev.type == TouchEvent::Drag) {
        // scroll: acumula o movimento vertical
        m_dragAccum += ev.y - ev.prevY;
        int step = rowH();
        while (m_dragAccum <= -step) {
            if (top > 0) top--;
            m_dragAccum += step;
        }
        while (m_dragAccum >= step) {
            if (top < (int)items.size() - 1) top++;
            m_dragAccum -= step;
        }
        return true;
    }
    if (ev.isTap() && myRect.contains(ev.x, ev.y)) {
        int row = (ev.y - rect.y) / rowH();
        int idx = top + row;
        if (idx >= 0 && idx < (int)items.size() && items[idx].enabled) {
            selected = idx;
            if (onSelect) onSelect(idx);
            return true;
        }
    }
    return false;
}

Rect Dialog::cardRect() const {
    int w = UI::W - UI::sx(40);
    int h = UI::sy(140);
    return {UI::sx(20), (UI::H - h) / 2, w, h};
}

void Dialog::draw(Canvas& c) {
    // overlay escurecido e card central
    c.fill(0x05070A);
    Rect card = cardRect();
    c.fillRoundRect(card, UI::sx(10), THEME_CARD);
    c.drawRoundRect(card, UI::sx(10), THEME_STROKE);
    c.text(title, card.x + card.w / 2, card.y + UI::sy(18), UI::font(2), THEME_ACCENT, MC_DATUM);
    c.text(body, card.x + card.w / 2, card.y + UI::sy(55), UI::font(1), THEME_TEXT, MC_DATUM);

    // botoes lado a lado na base do card
    int n = (int)buttons.size();
    if (n == 0) return;
    int gap = UI::sx(10);
    int bw = (card.w - gap * (n + 1)) / n;
    int by = card.y + card.h - UI::sy(38);
    for (int i = 0; i < n; i++) {
        buttons[i].rect = {card.x + gap + i * (bw + gap), by, bw, UI::sy(30)};
        buttons[i].draw(c);
    }
}

bool Dialog::onTouch(const TouchEvent& ev, Rect myRect) {
    (void)myRect;
    // modal: consome qualquer toque; so repassa aos botoes dentro do card
    for (auto& b : buttons) {
        if (b.onTouch(ev, b.rect)) return true;
    }
    return true;
}

// ============================================================ Navigator ====

namespace {

Canvas* s_canvas = nullptr;
std::vector<Screen*> s_stack;
Dialog* s_dialog = nullptr;
std::vector<Toast> s_toasts;
bool s_repaint = true;
uint32_t s_lastTickMs = 0;

// estado do toque (pump de eventos)
bool s_down = false;
int s_lastX = 0, s_lastY = 0, s_pressX = 0, s_pressY = 0, s_prevX = 0, s_prevY = 0;
uint32_t s_pressMs = 0;

void drawFrame() {
    if (s_canvas == nullptr || s_stack.empty()) return;
    s_canvas->begin();
    s_stack.back()->draw(*s_canvas);
    if (s_dialog != nullptr) s_dialog->draw(*s_canvas);
    if (!s_toasts.empty()) {
        const Toast& t = s_toasts.front();
        int h = UI::sy(26);
        Rect r{UI::sx(10), UI::sy(8), UI::W - UI::sx(20), h};
        s_canvas->fillRoundRect(r, UI::sx(6), 0x0A141C);
        s_canvas->drawRoundRect(r, UI::sx(6), t.color);
        s_canvas->text(t.message, r.x + r.w / 2, r.y + h / 2, UI::font(1), t.color, MC_DATUM);
    }
    s_canvas->end();
}

void dispatchTouch(const TouchEvent& ev) {
    if (s_dialog != nullptr) {
        s_dialog->onTouch(ev, s_dialog->cardRect());
        s_repaint = true;  // botao pode ter mudado algo
        return;
    }
    if (!s_stack.empty()) {
        if (s_stack.back()->onTouch(ev)) s_repaint = true;
    }
}

}  // namespace

void Navigator::begin(KryonDisplay& dev) {
    if (s_canvas == nullptr) s_canvas = new Canvas(dev);
}

void Navigator::push(Screen* s) {
    if (s == nullptr) return;
    if (!s_stack.empty()) s_stack.back()->onExit();
    s_stack.push_back(s);
    s->onEnter();
    s_repaint = true;
}

void Navigator::pop() {
    if (s_stack.size() <= 1) return;  // base (launcher) permanece
    s_stack.back()->onExit();
    s_stack.pop_back();
    if (!s_stack.empty()) s_stack.back()->onEnter();
    s_repaint = true;
}

void Navigator::replace(Screen* s) {
    if (s == nullptr || s_stack.empty()) {
        push(s);
        return;
    }
    s_stack.back()->onExit();
    s_stack.pop_back();
    s_stack.push_back(s);
    s->onEnter();
    s_repaint = true;
}

void Navigator::home() {
    while (s_stack.size() > 1) {
        s_stack.back()->onExit();
        s_stack.pop_back();
    }
    if (!s_stack.empty()) s_stack.back()->onEnter();
    closeDialog();
    s_repaint = true;
}

Screen* Navigator::top() { return s_stack.empty() ? nullptr : s_stack.back(); }

int Navigator::depth() { return (int)s_stack.size(); }

void Navigator::showDialog(Dialog* d) {
    s_dialog = d;
    s_repaint = true;
}

void Navigator::closeDialog() {
    if (s_dialog != nullptr) {
        s_dialog = nullptr;
        s_repaint = true;
    }
}

void Navigator::toast(const std::string& message, uint32_t color, uint32_t durationMs) {
    Toast t;
    t.message = message;
    t.color = color;
    t.durationMs = durationMs;
    t.shownAtMs = millis();
    s_toasts.push_back(t);
    s_repaint = true;
}

void Navigator::repaint() { s_repaint = true; }

void Navigator::pumpEvents() {
    uint16_t x = 0, y = 0;
    bool down = Board::display().getTouch(&x, &y) != 0;

    TouchEvent ev;
    if (down && !s_down) {
        // press
        s_down = true;
        s_lastX = s_prevX = s_pressX = x;
        s_lastY = s_prevY = s_pressY = y;
        s_pressMs = millis();
        ev.type = TouchEvent::Press;
        ev.x = x; ev.y = y; ev.startX = x; ev.startY = y; ev.prevX = x; ev.prevY = y;
        ev.holdMs = 0;
        dispatchTouch(ev);
    } else if (down && s_down) {
        if (abs((int)x - s_lastX) > 2 || abs((int)y - s_lastY) > 2) {
            ev.type = TouchEvent::Drag;
            ev.x = x; ev.y = y;
            ev.startX = s_pressX; ev.startY = s_pressY;
            ev.prevX = s_lastX; ev.prevY = s_lastY;
            ev.holdMs = millis() - s_pressMs;
            s_lastX = x; s_lastY = y;
            dispatchTouch(ev);
        }
    } else if (!down && s_down) {
        // release com as ULTIMAS coordenadas validas (getTouch nao escreve
        // x/y quando solto — usar coordenadas velhas era o bug dos toques
        // aleatorios do launcher antigo)
        s_down = false;
        ev.type = TouchEvent::Release;
        ev.x = s_lastX; ev.y = s_lastY;
        ev.startX = s_pressX; ev.startY = s_pressY;
        ev.prevX = s_lastX; ev.prevY = s_lastY;
        ev.holdMs = millis() - s_pressMs;
        dispatchTouch(ev);
    }
}

void Navigator::tick() {
    uint32_t now = millis();
    uint32_t dt = s_lastTickMs == 0 ? 10 : now - s_lastTickMs;
    s_lastTickMs = now;

    pumpEvents();

    if (s_dialog != nullptr) {
        s_dialog->onTick(dt);
    } else if (!s_stack.empty()) {
        s_stack.back()->onTick(dt);
    }

    // toast expirado
    if (!s_toasts.empty()) {
        if (now - s_toasts.front().shownAtMs > s_toasts.front().durationMs) {
            s_toasts.erase(s_toasts.begin());
            s_repaint = true;
        }
    }

    if (!s_stack.empty() && (s_repaint || s_stack.back()->consumeDirty())) {
        s_repaint = false;
        drawFrame();
    }
}

}  // namespace kui
