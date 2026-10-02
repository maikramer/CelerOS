#include "Kui.h"
#include "../Assets/Fonts/CelerFonts.h"

#include "../Display/Icon.h"
#include "../Display/Backlight.h"
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <cmath>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace kui {

static const char* TAG = "kui";

// ============================================================ TouchState ===

namespace {
TouchState s_touch;
}  // namespace

const TouchState& touchState() { return s_touch; }

bool isPressed(const Rect& r) {
    return s_touch.down && !s_touch.moved && r.contains(s_touch.startX, s_touch.startY) &&
           r.contains(s_touch.x, s_touch.y);
}

// ============================================================ Tipografia ===

namespace type {
// BoardTraits::largeUi e constexpr: na CYD os ramos da tela grande (e as
// fontes deles) somem do binario
const lgfx::IFont* caption() {
    if constexpr (BoardTraits::largeUi) return &celer::fonts::FreeSans9pt;
    return &celer::fonts::DejaVu9;
}
const lgfx::IFont* body() {
    if constexpr (BoardTraits::largeUi) return &celer::fonts::FreeSans12pt;
    return &celer::fonts::DejaVu12;
}
const lgfx::IFont* title() {
    if constexpr (BoardTraits::largeUi) return &celer::fonts::FreeSansBold18pt;
    return &celer::fonts::FreeSansBold12pt;
}
const lgfx::IFont* display() {
    if constexpr (BoardTraits::largeUi) return &celer::fonts::FreeSansBold24pt;
    return &celer::fonts::FreeSansBold18pt;
}
}  // namespace type

// ============================================================ Canvas =======

namespace {
// Buffer de composicao (um loop de UI): alocado no primeiro render.
// Full-frame (PSRAM): so s_bufs[0]. Faixas (RAM interna): DUAS faixas em
// ping-pong — o pushSprite de um sprite na RAM interna sai por DMA
// ASSINCRONO; desenhar a faixa seguinte no mesmo buffer enquanto o DMA
// ainda le era a origem dos "riscos" no vidro da CYD. Com duas, uma faixa
// e composta enquanto a outra transmite (sem corrida e mais rapido).
CelerSprite* s_bufs[2] = {nullptr, nullptr};
CelerSprite*& s_buf = s_bufs[0];
Canvas::Mode s_bufMode = Canvas::Direct;
bool s_bufTried = false;
int s_bandH = 0;

constexpr size_t BAND_BYTES = 16 * 1024;  // total das duas faixas (sem PSRAM)

CelerSprite* newSprite(CelerDisplay& dev, bool psram) {
    auto* s = new CelerSprite(&dev);
    s->setColorDepth(16);
    // Mesma convencao do display: arrays RGB565 LE (icones) e readRect
    s->setSwapBytes(true);
    s->setPsram(psram);
    return s;
}

void freeBuffers() {
    for (auto*& b : s_bufs) {
        if (b == nullptr) continue;
        b->deleteSprite();
        delete b;
        b = nullptr;
    }
}

bool ensureBuffer(CelerDisplay& dev) {
    if (s_bufTried) return s_buf != nullptr;
    s_bufTried = true;
    const int w = dev.width(), h = dev.height();

    if (Board::profile().hasPsram) {
        s_bufs[0] = newSprite(dev, true);
        if (s_bufs[0]->createSprite(w, h) != nullptr) {
            s_bufMode = Canvas::FullFrame;
            ESP_LOGI(TAG, "canvas: full-frame %dx%d (PSRAM)", w, h);
            return true;
        }
        freeBuffers();
    }
    s_bandH = (int)(BAND_BYTES / 2 / ((size_t)w * 2));
    if (s_bandH > h) s_bandH = h;
    if (s_bandH >= 8) {
        s_bufs[0] = newSprite(dev, false);
        s_bufs[1] = newSprite(dev, false);
        if (s_bufs[0]->createSprite(w, s_bandH) != nullptr && s_bufs[1]->createSprite(w, s_bandH) != nullptr) {
            s_bufMode = Canvas::Bands;
            ESP_LOGI(TAG, "canvas: faixas 2x %dx%d (%d passadas)", w, s_bandH, (h + s_bandH - 1) / s_bandH);
            return true;
        }
    }
    ESP_LOGW(TAG, "canvas: sem memoria para buffer, desenho direto");
    freeBuffers();
    return false;
}
}  // namespace

void releaseCanvasBuffer() {
    Board::display().waitDMA();
    freeBuffers();
    s_bufTried = false;  // o proximo render realoca
}

Canvas::Canvas(CelerDisplay& dev) : m_dev(dev), m_target(&dev) {}

Canvas::Canvas(CelerDisplay& dev, lgfx::LGFXBase* directTarget)
    : m_dev(dev), m_target(directTarget ? directTarget : &dev) {}

void Canvas::render(const std::function<void(Canvas&)>& fn, bool direct) {
    if (direct || !ensureBuffer(m_dev)) {
        m_mode = Direct;
        m_target = &m_dev;
        m_offY = 0;
        fn(*this);
        return;
    }

    m_target = s_buf;
    m_mode = s_bufMode;
    if (m_mode == FullFrame) {
        m_offY = 0;
        s_buf->clearClipRect();
        s_buf->fillScreen(THEME_BG);
        fn(*this);
        s_buf->pushSprite(&m_dev, 0, 0);
    } else {
        // Uma passada por faixa: tudo fora dela e recortado pelo sprite.
        // Ping-pong: compoe a faixa N no buffer livre enquanto o DMA envia a
        // N-1; antes de reusar um buffer, espera o DMA que le dele.
        m_dev.startWrite();
        int cur = 0;
        for (int y = 0; y < m_dev.height(); y += s_bandH) {
            CelerSprite* b = s_bufs[cur];
            m_target = b;
            m_offY = y;
            b->clearClipRect();
            b->fillScreen(THEME_BG);
            fn(*this);
            m_dev.waitDMA();  // faixa anterior (outro buffer) terminou de sair
            b->pushSprite(&m_dev, 0, y);
            cur ^= 1;
        }
        m_dev.waitDMA();
        m_dev.endWrite();
    }
    m_target = &m_dev;
    m_offY = 0;
    m_mode = Direct;
}

void Canvas::fill(uint32_t color) { m_target->fillScreen(color); }

void Canvas::fillRect(const Rect& r, uint32_t color) { m_target->fillRect(r.x, Y(r.y), r.w, r.h, color); }

void Canvas::fillRoundRect(const Rect& r, int radius, uint32_t color) {
    m_target->fillRoundRect(r.x, Y(r.y), r.w, r.h, radius, color);
}

void Canvas::fillGradient(const Rect& r, int radius, uint32_t top, uint32_t bottom) {
    Icon::fillGradientRoundRect(m_target, r.x, Y(r.y), r.w, r.h, radius, top, bottom);
}

void Canvas::drawRoundRect(const Rect& r, int radius, uint32_t color) {
    m_target->drawRoundRect(r.x, Y(r.y), r.w, r.h, radius, color);
}

void Canvas::drawRect(const Rect& r, uint32_t color) { m_target->drawRect(r.x, Y(r.y), r.w, r.h, color); }

void Canvas::drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    m_target->drawLine(x0, Y(y0), x1, Y(y1), color);
}

void Canvas::drawFastHLine(int x, int y, int w, uint32_t color) { m_target->drawFastHLine(x, Y(y), w, color); }

void Canvas::drawCircle(int cx, int cy, int r, uint32_t color) { m_target->drawCircle(cx, Y(cy), r, color); }

void Canvas::fillCircle(int cx, int cy, int r, uint32_t color) { m_target->fillCircle(cx, Y(cy), r, color); }

void Canvas::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    m_target->fillTriangle(x0, Y(y0), x1, Y(y1), x2, Y(y2), color);
}

void Canvas::fillArc(int cx, int cy, int r0, int r1, float a0, float a1, uint32_t color) {
    m_target->fillArc(cx, Y(cy), r0, r1, a0, a1, color);
}

void Canvas::pushImage(int x, int y, int w, int h, const uint16_t* data) {
    m_target->pushImage(x, Y(y), w, h, data);
}

void Canvas::drawIcon(const char* name, int x, int y) {
    // icone totalmente fora da faixa: nada a compor (poupa o readRect)
    if (Y(y) + Icon::SIZE <= 0 || Y(y) >= m_target->height()) return;
    Icon::draw(m_target, name, x, Y(y));
}

void Canvas::drawAppTile(const char* appName, int x, int y) {
    if (Y(y) + Icon::SIZE <= 0 || Y(y) >= m_target->height()) return;
    Icon::drawAppTile(m_target, appName, x, Y(y));
}

void Canvas::dim() {
    if (m_mode != Direct && m_target != &m_dev) {
        // Buffer 16-bit guarda o 565 com bytes trocados: desfaz, escurece
        // para ~37% (1/4 + 1/8 por canal) e troca de volta.
        auto* spr = static_cast<CelerSprite*>(m_target);
        uint16_t* p = (uint16_t*)spr->getBuffer();
        size_t n = (size_t)spr->width() * spr->height();
        for (size_t i = 0; i < n; i++) {
            uint16_t c = (uint16_t)((p[i] >> 8) | (p[i] << 8));
            c = (uint16_t)(((c >> 2) & 0x39E7) + ((c >> 3) & 0x18E3));
            p[i] = (uint16_t)((c >> 8) | (c << 8));
        }
        return;
    }
    m_target->fillScreen((uint32_t)0x04060C);  // direto: sem leitura do fundo, cobre
}

void Canvas::setClip(const Rect& r) { m_target->setClipRect(r.x, Y(r.y), r.w, r.h); }

void Canvas::clearClip() { m_target->clearClipRect(); }

void Canvas::text(const char* s, int x, int y, const lgfx::IFont* font, uint32_t color, int datum) {
    m_target->setTextDatum(datum);
    m_target->setTextColor(color);  // transparente: sem cor de fundo
    m_target->drawString(s, x, Y(y), font);
}

void Canvas::text(const std::string& s, int x, int y, const lgfx::IFont* font, uint32_t color, int datum) {
    text(s.c_str(), x, y, font, color, datum);
}

void Canvas::text(const char* s, int x, int y, uint8_t font, uint32_t color, int datum) {
    text(s, x, y, CelerFont(font), color, datum);
}

void Canvas::text(const std::string& s, int x, int y, uint8_t font, uint32_t color, int datum) {
    text(s.c_str(), x, y, CelerFont(font), color, datum);
}

int Canvas::textWidth(const char* s, uint8_t font) { return m_target->textWidth(s, CelerFont(font)); }

int Canvas::textWidth(const char* s, const lgfx::IFont* font) { return m_target->textWidth(s, font); }

int Canvas::fontHeight(const lgfx::IFont* font) { return m_target->fontHeight(font); }

std::string Canvas::ellipsize(const std::string& s, const lgfx::IFont* font, int maxW) {
    if (textWidth(s.c_str(), font) <= maxW) return s;
    std::string out = s;
    while (!out.empty()) {
        out.pop_back();
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (textWidth((out + "..").c_str(), font) <= maxW) return out + "..";
    }
    return "..";
}

int Canvas::width() const { return m_dev.width(); }

int Canvas::height() const { return m_dev.height(); }

// ============================================================ Header =======

int headerHeight() { return UI::sy(52); }

void drawHeader(Canvas& c, const char* title) {
    int h = headerHeight();
    c.fillRect({0, 0, UI::W, h}, THEME_CARD);
    c.drawFastHLine(0, h - 1, UI::W, THEME_STROKE);
    // marcador de acento a esquerda do titulo
    int mh = h / 3;
    c.fillRoundRect({UI::sx(12), (h - mh) / 2, UI::sx(4), mh}, UI::sx(2), THEME_ACCENT);
    c.text(title, UI::sx(24), h / 2, type::title(), THEME_TEXT, ML_DATUM);
}

// ============================================================ Widgets ======

void Button::draw(Canvas& c) {
    bool pressed = isPressed(rect);
    uint32_t fillc, textc, stroke = 0;
    int r = UI::sx(8);
    switch (style) {
        case Primary:
            fillc = pressed ? Icon::mix(THEME_ACCENT, 0x000000, 70) : THEME_ACCENT;
            textc = THEME_ON_ACCENT;
            break;
        case Danger:
            fillc = pressed ? Icon::mix(THEME_ERR, 0x000000, 70) : THEME_ERR;
            textc = 0xFFFFFF;
            break;
        default:
            fillc = pressed ? THEME_RAISED : THEME_CARD;
            textc = THEME_TEXT;
            stroke = pressed ? THEME_ACCENT : THEME_STROKE;
            break;
    }
    c.fillRoundRect(rect, r, fillc);
    if (stroke) c.drawRoundRect(rect, r, stroke);
    const lgfx::IFont* f = type::body();
    c.text(c.ellipsize(label, f, rect.w - UI::sx(8)), rect.x + rect.w / 2, rect.y + rect.h / 2, f, textc,
           MC_DATUM);
}

bool Button::onTouch(const TouchEvent& ev, Rect myRect) {
    if (!myRect.contains(ev.startX, ev.startY)) return false;
    if (ev.isTap() && myRect.contains(ev.x, ev.y)) {
        if (onTap) onTap();
        return true;
    }
    // press/release dentro do botao: redesenha o estado pressionado
    return ev.type != TouchEvent::Drag;
}

void Label::draw(Canvas& c) {
    c.text(text, rect.x, rect.y, UI::font(font), color, datum);
}

// ---- List --------------------------------------------------------------

int List::rowH() { return UI::sy(36); }

int List::visibleRows(const Rect& myRect) const {
    int rows = myRect.h / rowH();
    return rows < 1 ? 1 : rows;
}

int List::maxScroll() const {
    int m = (int)items.size() * rowH() - rect.h;
    return m < 0 ? 0 : m;
}

void List::clampScroll() {
    if (m_scroll < 0) {
        m_scroll = 0;
        m_vel = 0;
    }
    if (m_scroll > maxScroll()) {
        m_scroll = (float)maxScroll();
        m_vel = 0;
    }
}

void List::ensureVisible(int idx) {
    int y0 = idx * rowH();
    if (y0 < m_scroll) m_scroll = (float)y0;
    if (y0 + rowH() > m_scroll + rect.h) m_scroll = (float)(y0 + rowH() - rect.h);
    clampScroll();
}

static void drawSignalBars(Canvas& c, int right, int cy, int level, uint32_t on, uint32_t off) {
    int bw = UI::sx(3), gap = UI::sx(2), maxH = UI::sy(14);
    int x = right - 4 * bw - 3 * gap;
    for (int i = 0; i < 4; i++) {
        int h = maxH * (i + 1) / 4;
        c.fillRect({x + i * (bw + gap), cy + maxH / 2 - h, bw, h}, i < level ? on : off);
    }
}

void List::draw(Canvas& c) {
    int radius = UI::sx(10);
    c.fillRoundRect(rect, radius, THEME_CARD);
    clampScroll();

    const lgfx::IFont* f = type::body();
    const int rh = rowH();
    const int scroll = (int)m_scroll;
    const int first = scroll / rh;
    const int padX = UI::sx(12);

    c.setClip({rect.x, rect.y + UI::sy(2), rect.w, rect.h - UI::sy(4)});
    for (int idx = first; idx < (int)items.size(); idx++) {
        int y = rect.y + idx * rh - scroll;
        if (y >= rect.y + rect.h) break;
        Rect row{rect.x, y, rect.w, rh};
        if (!c.visible(row)) continue;  // faixa atual nao cruza a linha
        const Item& it = items[idx];

        if (idx == selected) {
            c.fillRoundRect({row.x + UI::sx(4), y + UI::sy(2), row.w - UI::sx(8), rh - UI::sy(4)}, UI::sx(6),
                            THEME_ACCENT_D);
        } else if (it.enabled && !m_dragging && isPressed(row) && rect.contains(s_touch.x, s_touch.y)) {
            c.fillRoundRect({row.x + UI::sx(4), y + UI::sy(2), row.w - UI::sx(8), rh - UI::sy(4)}, UI::sx(6),
                            THEME_RAISED);
        }
        // separador entre linhas
        if (idx + 1 < (int)items.size()) {
            c.drawFastHLine(row.x + padX, y + rh - 1, row.w - 2 * padX, THEME_BG);
        }

        int right = row.x + row.w - padX;
        if (it.bars >= 0) {
            drawSignalBars(c, right, y + rh / 2, it.bars, it.enabled ? THEME_ACCENT : THEME_TEXT_DIM, THEME_STROKE);
            right -= UI::sx(26);
        }
        if (!it.right.empty()) {
            c.text(it.right, right, y + rh / 2, type::caption(), THEME_TEXT_DIM, MR_DATUM);
            right -= c.textWidth(it.right.c_str(), type::caption()) + UI::sx(8);
        }
        uint32_t color = it.enabled ? THEME_TEXT : THEME_TEXT_DIM;
        c.text(c.ellipsize(it.label, f, right - row.x - padX), row.x + padX, y + rh / 2, f, color, ML_DATUM);
    }
    c.clearClip();

    // indicador de scroll (so quando o conteudo excede a area)
    int total = (int)items.size() * rh;
    if (total > rect.h) {
        int trackH = rect.h - 2 * radius;
        int barH = trackH * rect.h / total;
        if (barH < UI::sy(16)) barH = UI::sy(16);
        int barY = rect.y + radius + (trackH - barH) * scroll / maxScroll();
        c.fillRoundRect({rect.x + rect.w - UI::sx(5), barY, UI::sx(3), barH}, UI::sx(1), THEME_STROKE);
    }
}

bool List::onTouch(const TouchEvent& ev, Rect myRect) {
    uint32_t now = millis();
    switch (ev.type) {
        case TouchEvent::Press:
            if (!myRect.contains(ev.x, ev.y)) return false;
            m_vel = 0;  // toque segura a inercia
            m_dragging = false;
            m_lastDragMs = now;
            return true;  // redesenha o estado pressionado

        case TouchEvent::Drag: {
            if (!myRect.contains(ev.startX, ev.startY)) return false;
            if (!s_touch.moved) return false;  // ainda dentro da tolerancia de tap
            if (!m_dragging) {
                m_dragging = true;
                m_lastDragMs = now;
            }
            float dy = (float)(ev.y - ev.prevY);
            m_scroll -= dy;
            uint32_t dt = now - m_lastDragMs;
            if (dt > 0) {
                float v = -dy / (float)dt;
                m_vel = 0.6f * m_vel + 0.4f * v;  // media movel da velocidade
            }
            m_lastDragMs = now;
            clampScroll();
            return true;
        }

        case TouchEvent::Release: {
            if (!myRect.contains(ev.startX, ev.startY)) return false;
            bool wasDragging = m_dragging;
            m_dragging = false;
            if (wasDragging) {
                // dedo parado antes de soltar: sem fling
                if (now - m_lastDragMs > 80) m_vel = 0;
                if (m_vel > 3.0f) m_vel = 3.0f;
                if (m_vel < -3.0f) m_vel = -3.0f;
                return true;
            }
            if (ev.isTap() && myRect.contains(ev.x, ev.y)) {
                // Seleciona pela posicao do POUSO (startY), nao do release:
                // no resistivo o dedo deriva ao pressionar e a linha da vez
                // trocava no fim (mesmo fix do teclado, Keyboard.cpp).
                int idx = (ev.startY - rect.y + (int)m_scroll) / rowH();
                if (idx >= 0 && idx < (int)items.size() && items[idx].enabled) {
                    selected = idx;
                    if (onSelect) onSelect(idx);
                }
            }
            return true;
        }
    }
    return false;
}

bool List::tick(uint32_t dtMs) {
    if (m_dragging || std::fabs(m_vel) < 0.02f) {
        m_vel = m_dragging ? m_vel : 0;
        return false;
    }
    if (dtMs > 50) dtMs = 50;
    m_scroll += m_vel * (float)dtMs;
    m_vel *= std::pow(0.995f, (float)dtMs);  // atrito
    clampScroll();
    return true;
}

// ---- Switch / Slider / ProgressBar / Spinner (API 12 do toolkit) --------

void Switch::draw(Canvas& c) {
    bool pressed = isPressed(rect);
    int h = rect.h, w = h * 2;                 // pilula 2:1 dentro do rect
    if (w > rect.w) { w = rect.w; h = w / 2; }
    int x = rect.x + (rect.w - w) / 2, y = rect.y + (rect.h - h) / 2;
    int r = h / 2;
    uint32_t fillc = on ? THEME_ACCENT : (pressed ? THEME_RAISED : THEME_CARD);
    c.fillRoundRect(Rect{x, y, w, h}, r, fillc);
    c.drawRoundRect(Rect{x, y, w, h}, r, THEME_STROKE);
    int knob = h - UI::sy(6);
    c.fillCircle(x + (on ? w - knob / 2 - UI::sx(3) : knob / 2 + UI::sx(3)), y + h / 2, knob / 2,
                 pressed ? THEME_TEXT_DIM : THEME_TEXT);
}

bool Switch::onTouch(const TouchEvent& ev, Rect myRect) {
    if (!myRect.contains(ev.startX, ev.startY)) return false;
    if (ev.isTap() && myRect.contains(ev.x, ev.y)) {
        on = !on;
        if (onChange) onChange(on);
        return true;
    }
    if (ev.type == TouchEvent::Press) return true;  // consome o press (feedback)
    return false;
}

void Slider::draw(Canvas& c) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    int h = UI::sy(12);
    int y = rect.y + (rect.h - h) / 2;
    int r = h / 2;
    c.fillRoundRect(Rect{rect.x, y, rect.w, h}, r, THEME_CARD);
    c.drawRoundRect(Rect{rect.x, y, rect.w, h}, r, THEME_STROKE);
    int fw = (int)((long)rect.w * value / 100);
    if (fw > r * 2) c.fillRoundRect(Rect{rect.x, y, fw, h}, r, isPressed(rect) ? THEME_ACCENT_D : THEME_ACCENT);
    c.fillCircle(rect.x + fw, y + h / 2, UI::sy(9), THEME_TEXT);
}

bool Slider::onTouch(const TouchEvent& ev, Rect myRect) {
    bool inside = myRect.contains(ev.startX, ev.startY);
    if (!inside && ev.type != TouchEvent::Drag) return false;
    if (!inside) return false;
    if (ev.type == TouchEvent::Press || ev.type == TouchEvent::Drag) {
        int v = (int)((long)(ev.x - myRect.x) * 100 / myRect.w);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        if (v != value) {
            value = v;
            if (onLiveChange) onLiveChange(value);
        }
        return true;
    }
    if (ev.type == TouchEvent::Release) {
        if (onChange) onChange(value);
        return true;
    }
    return false;
}

void ProgressBar::draw(Canvas& c) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    int h = UI::sy(10);
    int y = rect.y + (rect.h - h) / 2;
    int r = h / 2;
    c.fillRoundRect(Rect{rect.x, y, rect.w, h}, r, THEME_CARD);
    c.drawRoundRect(Rect{rect.x, y, rect.w, h}, r, THEME_STROKE);
    int fw = (int)((long)rect.w * value / 100);
    if (fw > r * 2) c.fillRoundRect(Rect{rect.x, y, fw, h}, r, THEME_ACCENT);
}

void Spinner::draw(Canvas& c) {
    // arco de 90 graus girando ~360 graus/s — barato e sem alocar
    uint32_t ms = millis() % 1000;
    int a = (int)(ms * 360 / 1000);
    int r = rect.h / 2 - UI::sy(2);
    if (r < UI::sy(4)) r = UI::sy(4);
    c.fillArc(rect.x + rect.w / 2, rect.y + rect.h / 2, r - UI::sy(3), r, (float)a, (float)a + 90, color);
}

// ---- Dialog -------------------------------------------------------------

Rect Dialog::cardRect() const {
    int w = UI::W - UI::sx(40);
    int h = UI::sy(150);
    return {UI::sx(20), (UI::H - h) / 2, w, h};
}

void Dialog::draw(Canvas& c) {
    // tela de baixo escurecida (buffer) + card central
    c.dim();
    Rect card = cardRect();
    int r = UI::sx(12);
    c.fillRoundRect(card, r, THEME_CARD);
    c.drawRoundRect(card, r, THEME_STROKE);
    c.text(c.ellipsize(title, type::title(), card.w - UI::sx(24)), card.x + card.w / 2, card.y + UI::sy(24),
           type::title(), THEME_TEXT, MC_DATUM);
    c.text(c.ellipsize(body, type::body(), card.w - UI::sx(20)), card.x + card.w / 2, card.y + UI::sy(62),
           type::body(), THEME_TEXT_DIM, MC_DATUM);

    // botoes lado a lado na base do card
    int n = (int)buttons.size();
    if (n == 0) return;
    int gap = UI::sx(10);
    int bw = (card.w - gap * (n + 1)) / n;
    int by = card.y + card.h - UI::sy(44);
    for (int i = 0; i < n; i++) {
        buttons[i].rect = {card.x + gap + i * (bw + gap), by, bw, UI::sy(34)};
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

// ========================================================= TouchInjector ===

namespace {
constexpr size_t INJ_CAP = 64;
TouchInjector::Sample s_injQ[INJ_CAP];
size_t s_injHead = 0, s_injCount = 0;
portMUX_TYPE s_injMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_injLastMs = 0;        // quando a ultima amostra virou estado
bool s_injDown = false;          // estado do dedo sintetico
uint16_t s_injX = 0, s_injY = 0;
}  // namespace

bool TouchInjector::push(const Sample* samples, size_t n) {
    bool ok = false;
    portENTER_CRITICAL(&s_injMux);
    if (s_injCount + n <= INJ_CAP) {
        if (s_injCount == 0) s_injLastMs = millis();  // delay do 1o conta a partir de agora
        for (size_t i = 0; i < n; i++) s_injQ[(s_injHead + s_injCount + i) % INJ_CAP] = samples[i];
        s_injCount += n;
        ok = true;
    }
    portEXIT_CRITICAL(&s_injMux);
    return ok;
}

bool TouchInjector::active() {
    portENTER_CRITICAL(&s_injMux);
    bool a = s_injCount > 0 || s_injDown;
    portEXIT_CRITICAL(&s_injMux);
    return a;
}

bool TouchInjector::take(Sample& out) {
    bool got = false;
    uint32_t now = millis();
    portENTER_CRITICAL(&s_injMux);
    if (s_injCount > 0 && now - s_injLastMs >= s_injQ[s_injHead].delayMs) {
        out = s_injQ[s_injHead];
        s_injHead = (s_injHead + 1) % INJ_CAP;
        s_injCount--;
        s_injLastMs = now;
        got = true;
    }
    portEXIT_CRITICAL(&s_injMux);
    return got;
}

// ============================================================ TouchPump =====

namespace {
uint32_t s_quarantineUntilMs = 0;
}  // namespace

void TouchPump::quarantine(uint32_t ms) { s_quarantineUntilMs = millis() + ms; }

void TouchPump::reset() {
    m_down = false;
    m_releaseSinceMs = 0;
}

bool readTouch(uint16_t* x, uint16_t* y) {
    bool down;
    if (TouchInjector::active()) {
        // gesto sintetico (celerctl) e dono do touch ate a fila esvaziar e soltar
        TouchInjector::Sample smp;
        if (TouchInjector::take(smp)) {
            s_injDown = smp.down;
            if (smp.down) {
                s_injX = smp.x;
                s_injY = smp.y;
            }
        }
        if (s_injDown) {
            *x = s_injX;
            *y = s_injY;
        }
        down = s_injDown;
    } else {
        down = Board::display().getTouch(x, y) != 0;
    }
    // Timeout de tela: toque valido conta como atividade; se ele SO acordou
    // a tela, engole (o primeiro toque nao clica em nada as cegas)
    if (down && Backlight::noteActivity()) return false;
    return down;
}

void TouchPump::poll(const Handler& onEvent) {
    uint16_t x = 0, y = 0;
    bool down = readTouch(&x, &y);

    // Quarentena: descarta tudo ate a janela vencer com o vidro limpo. Zerar
    // m_down evita um release fantasma do toque que motivou a quarentena.
    if (s_quarantineUntilMs != 0) {
        if (millis() < s_quarantineUntilMs || down) {
            m_down = false;
            s_touch = {};
            return;
        }
        s_quarantineUntilMs = 0;
        // o frame desenhado durante a quarentena pode ter o realce do toque
        // que fechou o app (ex.: WiFi sob o X): redesenha ja com o vidro limpo
        Navigator::repaint();
    }

    // Debounce do release (touch resistivo XPT2046: a pressao oscila no
    // fim/lateral do dedo e, principalmente, ao DESLIZAR — o getTouch pisca
    // solto/pressionado e cada re-deteccao disparava um redraw completo).
    // So considera solto apos ~60ms de leituras vazias seguidas. Por TEMPO,
    // nao por contagem: com o loop lento (um redraw por poll) 6 leituras
    // viravam 600ms+ e todo tap disparava o toque longo do launcher.
    if (!down && m_down) {
        uint32_t now = millis();
        if (m_releaseSinceMs == 0) {
            m_releaseSinceMs = now ? now : 1;
            return;
        }
        if (now - m_releaseSinceMs < 60) return;
        m_releaseSinceMs = 0;
    } else if (down) {
        m_releaseSinceMs = 0;
    }

    TouchEvent ev;
    if (down && !m_down) {
        // press
        m_down = true;
        m_lastX = x;
        m_lastY = y;
        m_pressX = x;
        m_pressY = y;
        m_pressMs = millis();
        s_touch = {true, false, x, y, x, y};
        ev.type = TouchEvent::Press;
        ev.x = x; ev.y = y; ev.startX = x; ev.startY = y; ev.prevX = x; ev.prevY = y;
        ev.holdMs = 0;
        onEvent(ev);
    } else if (down && m_down) {
        if (abs((int)x - m_lastX) > 2 || abs((int)y - m_lastY) > 2) {
            ev.type = TouchEvent::Drag;
            ev.x = x; ev.y = y;
            ev.startX = m_pressX; ev.startY = m_pressY;
            ev.prevX = m_lastX; ev.prevY = m_lastY;
            ev.holdMs = millis() - m_pressMs;
            m_lastX = x; m_lastY = y;
            s_touch.x = x;
            s_touch.y = y;
            if (abs(ev.dx()) >= TouchEvent::slopX() || abs(ev.dy()) >= TouchEvent::slopY()) s_touch.moved = true;
            onEvent(ev);
        }
    } else if (!down && m_down) {
        // release com as ULTIMAS coordenadas validas (getTouch nao escreve
        // x/y quando solto — usar coordenadas velhas era o bug dos toques
        // aleatorios do launcher antigo)
        m_down = false;
        s_touch.down = false;
        ev.type = TouchEvent::Release;
        ev.x = m_lastX; ev.y = m_lastY;
        ev.startX = m_pressX; ev.startY = m_pressY;
        ev.prevX = m_lastX; ev.prevY = m_lastY;
        ev.holdMs = millis() - m_pressMs;
        onEvent(ev);
    }
}

// ============================================================ Navigator ====

namespace {

Canvas* s_canvas = nullptr;
std::vector<Screen*> s_stack;
Dialog* s_dialog = nullptr;
// O dialog so aceita toques que COMECARAM com ele aberto: aberto no meio de
// um toque (ex.: pressionar-e-segurar), o release desse toque nao pode
// acionar o botao que por acaso ficou sob o dedo.
bool s_dialogArmed = false;
std::vector<Toast> s_toasts;

// Navigator NAO e thread-safe: o estado acima so e tocado pela task da UI.
// Chamadas de OUTRA task (app JS na task "celerapp" com CELEROS_APP_TASK,
// eventos de rede) entram numa fila com mutex e a UI aplica no tick.
TaskHandle_t s_uiTask = nullptr;
SemaphoreHandle_t s_pendMux = nullptr;
std::vector<Toast> s_pendToasts;
std::vector<Screen*> s_pendPush;

bool offUiTask() {
    return s_uiTask != nullptr && xTaskGetCurrentTaskHandle() != s_uiTask && s_pendMux != nullptr;
}

bool s_repaint = true;
bool s_inputSuspended = false;  // app JS em task propria: UI pausa o pump
uint32_t s_lastTickMs = 0;
TouchPump s_pump;

void drawToast(Canvas& c, const Toast& t) {
    const lgfx::IFont* f = type::body();
    int h = UI::sy(34);
    // inset: no vidro de cantos arredondados (watch) o toast sobe e estreita
    int maxW = UI::W - UI::sx(32) - UI::inset;
    std::string msg = c.ellipsize(t.message, f, maxW - UI::sx(34));
    int w = c.textWidth(msg.c_str(), f) + UI::sx(40);
    if (w > maxW) w = maxW;
    Rect r{(UI::W - w) / 2, UI::H - h - UI::sy(40) - UI::inset / 2, w, h};
    c.fillRoundRect(r, h / 2, THEME_RAISED);
    c.drawRoundRect(r, h / 2, THEME_STROKE);
    c.fillCircle(r.x + UI::sx(16), r.y + h / 2, UI::sx(4), t.color);  // ponto de status
    c.text(msg, r.x + UI::sx(28), r.y + h / 2, f, THEME_TEXT, ML_DATUM);
}

void drawFrame() {
    if (s_canvas == nullptr || s_stack.empty()) return;
    Screen* top = s_stack.back();
    s_canvas->render(
        [top](Canvas& c) {
            top->draw(c);
            if (s_dialog != nullptr) s_dialog->draw(c);
            if (!s_toasts.empty()) {
                if (s_toasts.front().shownAtMs == 0) s_toasts.front().shownAtMs = millis() | 1;
                drawToast(c, s_toasts.front());
            }
        },
        top->wantsDirectDraw());
}

bool (*s_gestureHook)(const TouchEvent&) = nullptr;

bool isBackGesture(const TouchEvent& ev) {
    return ev.type == TouchEvent::Release && ev.startX < UI::sx(20) && ev.swipe() == TouchEvent::SwipeRight;
}

void dispatchTouch(const TouchEvent& ev) {
    if (s_dialog != nullptr) {
        if (!s_dialogArmed) {
            if (ev.type != TouchEvent::Press) return;
            s_dialogArmed = true;
        }
        s_dialog->onTouch(ev, s_dialog->cardRect());
        s_repaint = true;  // botao pode ter mudado algo / estado pressionado
        return;
    }
    if (s_stack.empty()) return;
    Screen* top = s_stack.back();

    // Gestos de sistema (bordas do watch: quick settings/notificacoes).
    // Telas que travam o voltar (alarme tocando, conexao) ficam de fora.
    if (s_gestureHook != nullptr && top->allowsBackGesture() && s_gestureHook(ev)) {
        s_repaint = true;
        return;
    }

    // Voltar: swipe para a direita a partir da borda esquerda
    if (isBackGesture(ev) && s_stack.size() > 1 && top->allowsBackGesture()) {
        Navigator::pop();
        return;
    }

    if (top->onTouch(ev)) s_repaint = true;
    // Estado pressionado das telas Kui: redesenha no press/release
    if (!top->wantsDirectDraw() && ev.type != TouchEvent::Drag) s_repaint = true;
}

}  // namespace

bool canvasBuffered() { return s_buf != nullptr; }

void Navigator::begin(CelerDisplay& dev) {
    if (s_canvas == nullptr) s_canvas = new Canvas(dev);
    s_uiTask = xTaskGetCurrentTaskHandle();
    if (s_pendMux == nullptr) s_pendMux = xSemaphoreCreateMutex();
}

void Navigator::push(Screen* s) {
    if (s == nullptr) return;
    if (offUiTask()) {  // aplicado no proximo tick, na task da UI
        xSemaphoreTake(s_pendMux, portMAX_DELAY);
        s_pendPush.push_back(s);
        xSemaphoreGive(s_pendMux);
        return;
    }
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
    if (!s_stack.empty()) s_stack.back()->onResume();
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

void Navigator::remove(Screen* s) {
    if (s == nullptr) return;
    for (size_t i = s_stack.size(); i-- > 0;) {
        if (s_stack[i] != s) continue;
        bool wasTop = (i == s_stack.size() - 1);
        s->onExit();
        s_stack.erase(s_stack.begin() + i);
        // Espelha o pop(): a tela de baixo reajusta estado visual no
        // onResume (ex.: LauncherScreen::m_needClear limpa o vidro stale
        // do app que saiu no modo Direct da CYD). Faltava so o onEnter,
        // que nao reseta quem ja entrou antes.
        if (wasTop && !s_stack.empty()) {
            s_stack.back()->onEnter();
            s_stack.back()->onResume();
        }
        s_repaint = true;
        return;
    }
    if (!s_stack.empty()) s_stack.back()->onResume();
}

void Navigator::home() {
    while (s_stack.size() > 1) {
        s_stack.back()->onExit();
        s_stack.pop_back();
    }
    if (!s_stack.empty()) s_stack.back()->onEnter();
    closeDialog();
    s_repaint = true;
    if (!s_stack.empty()) s_stack.back()->onResume();
}

Screen* Navigator::top() { return s_stack.empty() ? nullptr : s_stack.back(); }

int Navigator::depth() { return (int)s_stack.size(); }

void Navigator::showDialog(Dialog* d) {
    s_dialog = d;
    s_dialogArmed = false;
    s_repaint = true;
}

void Navigator::closeDialog() {
    if (s_dialog != nullptr) {
        s_dialog = nullptr;
        s_repaint = true;
    }
    if (!s_stack.empty()) s_stack.back()->onResume();
}

void Navigator::toast(const std::string& message, uint32_t color, uint32_t durationMs) {
    Toast t;
    t.message = message;
    t.color = color;
    t.durationMs = durationMs;
    // 0 = ainda nao apareceu: a duracao conta do 1o desenho. Com app JS no
    // caminho sincrono a UI nao desenha ate ele sair — contando da chamada,
    // o toast "vencia" antes de aparecer (System.toast/notify sumiam).
    t.shownAtMs = 0;
    if (offUiTask()) {
        xSemaphoreTake(s_pendMux, portMAX_DELAY);
        if (s_pendToasts.size() < 8) s_pendToasts.push_back(t);  // rajada nao cresce sem fim
        xSemaphoreGive(s_pendMux);
        return;
    }
    s_toasts.push_back(t);
    s_repaint = true;
}

void Navigator::repaint() { s_repaint = true; }

void Navigator::setGestureHook(bool (*hook)(const TouchEvent& ev)) { s_gestureHook = hook; }

void Navigator::setInputSuspended(bool suspended) {
    s_inputSuspended = suspended;
    if (suspended) {
        // solta o estado do pump: um press pendente nao pode virar release
        // fantasma quando a UI retomar apos o app
        s_pump.reset();
    }
}

void Navigator::pumpEvents() {
    if (s_inputSuspended) return;  // app JS em task propria: dono do touch
    s_pump.poll(dispatchTouch);
}

void Navigator::tick() {
    uint32_t now = millis();
    uint32_t dt = s_lastTickMs == 0 ? 10 : now - s_lastTickMs;
    s_lastTickMs = now;
    // Um app sincrono roda DENTRO de um tick (launchApp no onTouch): quando
    // ele sai, o tick seguinte herda o tempo de vida inteiro do app como dt
    // e estoira acumuladores de tela (anel da chamada, prazo do alerta de
    // notificacao — expirava em um frame). dt nao representa mais que um
    // intervalo de loop.
    if (dt > 250) dt = 250;

    // pedidos de outras tasks (toast/push): aplicados aqui, na task da UI
    if (s_pendMux != nullptr) {
        std::vector<Toast> toasts;
        std::vector<Screen*> pushes;
        xSemaphoreTake(s_pendMux, portMAX_DELAY);
        toasts.swap(s_pendToasts);
        pushes.swap(s_pendPush);
        xSemaphoreGive(s_pendMux);
        for (Toast& t : toasts) {
            t.shownAtMs = 0;  // conta a partir de quando aparece
            s_toasts.push_back(t);
            s_repaint = true;
        }
        for (Screen* sc : pushes) push(sc);
    }

    pumpEvents();

    if (s_dialog != nullptr) {
        s_dialog->onTick(dt);
    } else if (!s_stack.empty()) {
        s_stack.back()->onTick(dt);
    }

    // toast expirado (o proximo da fila comeca a contar agora)
    if (!s_toasts.empty() && s_toasts.front().shownAtMs != 0) {
        if (now - s_toasts.front().shownAtMs > s_toasts.front().durationMs) {
            s_toasts.erase(s_toasts.begin());
            if (!s_toasts.empty()) s_toasts.front().shownAtMs = 0;  // conta do desenho
            s_repaint = true;
        }
    }

    // suppressRedraw: app JS rodando em task propria e dono do vidro — nem
    // o frame do topo nem toasts podem pintar por cima dele
    if (!s_stack.empty() && !s_stack.back()->suppressRedraw() &&
        (s_repaint || s_stack.back()->consumeDirty())) {
        s_repaint = false;
        drawFrame();
    }
}

}  // namespace kui
