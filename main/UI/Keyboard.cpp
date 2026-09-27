#include "Keyboard.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <cstring>

namespace kui {

// ---- keysets ---------------------------------------------------------------
// Letras em minusculo (Upper aplica toupper ao desenhar/digitar). Simbolos
// cobrem todo o ASCII imprimivel entre as duas paginas (chars comuns de
// senha se repetem entre paginas de proposito).

static const char* LET_ROWS[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
static const char* SY1_ROWS[3] = {"1234567890", "@#$%&-+()/", "*\"':;!?"};
static const char* SY2_ROWS[3] = {"~`|\\_<>[]{", "}=^*\"'-+/", "*\"'=+!?"};

// ---- glifos desenhados (fontes nativas nao tem setas) ----------------------

static void drawShiftGlyph(Canvas& c, const Rect& r, uint32_t col) {
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;
    int half = r.h / 3;
    if (half > r.w / 2) half = r.w / 2;
    c.drawLine(cx - half, cy, cx, cy - half, col);      // chevron
    c.drawLine(cx, cy - half, cx + half, cy, col);
    c.drawLine(cx, cy - half + 1, cx, cy + half, col);   // haste
}

static void drawBackGlyph(Canvas& c, const Rect& r, uint32_t col) {
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;
    int half = r.h / 3;
    if (half > r.w / 2) half = r.w / 2;
    c.drawLine(cx - half / 2, cy, cx + half, cy, col);                 // corpo
    c.drawLine(cx - half / 2, cy, cx - half / 2 + half / 2, cy - half / 2, col);  // ponta
    c.drawLine(cx - half / 2, cy, cx - half / 2 + half / 2, cy + half / 2, col);
}

// ============================================================ KeyboardScreen

KeyboardScreen::KeyboardScreen(const std::string& prompt, const std::string& initial, int maxLen)
    : m_prompt(prompt), m_text(initial), m_maxLen(maxLen > 0 ? maxLen : 64) {
    if (m_maxLen > 256) m_maxLen = 256;
    rebuild();
}

Rect KeyboardScreen::fieldRect() const {
    int m = UI::sx(4);
    return {m, UI::sy(26), UI::W - 2 * m, UI::sy(26)};
}

Rect KeyboardScreen::cancelRect() const {
    int m = UI::sx(4);
    return {UI::W - m - UI::sx(34), UI::sy(2), UI::sx(34), UI::sy(20)};
}

void KeyboardScreen::rebuild() {
    m_keys.clear();

    const int g = UI::sx(2);
    // unidade = largura de uma tecla da linha de cima (10 keys)
    const int u = (UI::W - 2 * UI::sx(4) - 9 * g) / 10;
    if (u < 4) return;  // display impossivel: sem keys, so field

    Rect f = fieldRect();
    const int top = f.y + f.h + UI::sy(5);
    const int rowGap = UI::sy(2);
    int keyH = (UI::H - top - UI::sy(2) - 3 * rowGap) / 4;
    if (keyH < 12) keyH = 12;

    const bool letters = isLetters(m_mode);
    const char* const* rows = (m_mode == Sym1) ? SY1_ROWS : (m_mode == Sym2) ? SY2_ROWS : LET_ROWS;

    auto addRow = [&](int row, const float* wu, int n, const Kind* kinds, const char* chars) {
        float units = 0;
        for (int i = 0; i < n; i++) units += wu[i];
        int total = (int)(units * u) + (n - 1) * g;
        int x = (UI::W - total) / 2;
        int y = top + row * (keyH + rowGap);
        for (int i = 0; i < n; i++) {
            int w = (int)(wu[i] * u);
            m_keys.push_back({{x, y, w, keyH}, kinds[i], chars ? chars[i] : '\0'});
            x += w + g;
        }
    };

    // linhas 0/1: so caracteres, centradas (9 keys na do meio = escalonado)
    float one10[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    float one9[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    Kind kChar[10] = {KChar, KChar, KChar, KChar, KChar, KChar, KChar, KChar, KChar, KChar};
    addRow(0, one10, (int)strlen(rows[0]), kChar, rows[0]);
    addRow(1, one9, (int)strlen(rows[1]), kChar, rows[1]);

    // linha 2: [shift/simbolos 1.5u] chars [backspace 1.5u]
    float w2[9] = {1.5f, 1, 1, 1, 1, 1, 1, 1, 1.5f};
    Kind k2[9] = {letters ? KShift : KSymPage, KChar, KChar, KChar, KChar, KChar, KChar, KChar, KBksp};
    char c2[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 7 && i < (int)strlen(rows[2]); i++) c2[i + 1] = rows[2][i];
    addRow(2, w2, 9, k2, c2);

    // linha 3: [modo] [,] [espaco 4u] [.] [OK]
    float w3[5] = {1.5f, 1, 4, 1, 1.5f};
    Kind k3[5] = {KMode, KChar, KSpace, KChar, KOk};
    char c3[5] = {0, ',', 0, '.', 0};
    addRow(3, w3, 5, k3, c3);
}

char KeyboardScreen::labelChar(char ch) const {
    if (m_mode == Upper && ch >= 'a' && ch <= 'z') return ch - ('a' - 'A');
    return ch;
}

void KeyboardScreen::draw(Canvas& c) {
    // cabecalho: prompt (esquerda, cortado se nao couber) + X cancelar
    Rect xr = cancelRect();
    c.fillRoundRect(xr, UI::sx(6), THEME_CARD);
    c.drawRoundRect(xr, UI::sx(6), THEME_STROKE);
    c.text("X", xr.x + xr.w / 2, xr.y + xr.h / 2, UI::font(2), THEME_TEXT_DIM, MC_DATUM);

    Rect f = fieldRect();
    if (!m_prompt.empty()) {
        std::string p = m_prompt;
        int avail = xr.x - UI::sx(8);
        while (!p.empty() && c.textWidth(p.c_str(), UI::font(1)) > avail) p.pop_back();
        c.text(p, UI::sx(4), f.y / 2, UI::font(1), THEME_TEXT_DIM, ML_DATUM);
    }

    // campo de texto com cursor piscando; texto longo mostra o final
    c.fillRoundRect(f, UI::sx(6), THEME_CARD);
    c.drawRoundRect(f, UI::sx(6), THEME_STROKE);
    const int pad = UI::sx(8);
    const int avail = f.w - 2 * pad - UI::sx(4);
    std::string shown = m_text;
    while (!shown.empty() && c.textWidth(shown.c_str(), UI::font(2)) > avail) {
        shown.erase(shown.begin());
    }
    int ty = f.y + f.h / 2;
    if (!shown.empty()) {
        c.text(shown, f.x + pad, ty, UI::font(2), THEME_TEXT, ML_DATUM);
    }
    if (m_cursorOn) {
        int tw = c.textWidth(shown.c_str(), UI::font(2));
        int ch = c.fontHeight(KryonFont(UI::font(2)));
        c.fillRect({f.x + pad + tw + UI::sx(1), ty - ch / 2, UI::sx(2), ch}, THEME_ACCENT);
    }

    // teclas
    uint32_t now = millis();
    for (int i = 0; i < (int)m_keys.size(); i++) {
        const Key& k = m_keys[i];
        const Rect& r = k.r;
        bool flash = (i == m_flashKey && now - m_flashMs < 150) || isPressed(r);

        switch (k.kind) {
            case KOk:
                c.fillRoundRect(r, UI::sx(3), flash ? THEME_ACCENT_D : THEME_ACCENT);
                c.text("OK", r.x + r.w / 2, r.y + r.h / 2, UI::font(2), flash ? THEME_TEXT : THEME_ON_ACCENT, MC_DATUM);
                break;
            case KChar: {
                char s[2] = {labelChar(k.ch), 0};
                c.fillRoundRect(r, UI::sx(3), flash ? THEME_ACCENT_D : THEME_CARD);
                c.drawRoundRect(r, UI::sx(3), THEME_STROKE);
                c.text(s, r.x + r.w / 2, r.y + r.h / 2, UI::font(2), THEME_TEXT, MC_DATUM);
                break;
            }
            case KSpace:
                c.fillRoundRect(r, UI::sx(3), flash ? THEME_ACCENT_D : THEME_CARD);
                c.drawRoundRect(r, UI::sx(3), THEME_STROKE);
                c.text("space", r.x + r.w / 2, r.y + r.h / 2, UI::font(1), THEME_TEXT_DIM, MC_DATUM);
                break;
            case KShift:
                c.fillRoundRect(r, UI::sx(3), (m_mode == Upper || flash) ? THEME_ACCENT_D : THEME_CARD);
                c.drawRoundRect(r, UI::sx(3), THEME_STROKE);
                drawShiftGlyph(c, r, m_mode == Upper ? THEME_TEXT : THEME_TEXT_DIM);
                break;
            case KSymPage:
                c.fillRoundRect(r, UI::sx(3), flash ? THEME_ACCENT_D : THEME_CARD);
                c.drawRoundRect(r, UI::sx(3), THEME_STROKE);
                c.text(m_mode == Sym2 ? "?123" : "*+=", r.x + r.w / 2, r.y + r.h / 2, UI::font(1),
                       THEME_TEXT, MC_DATUM);
                break;
            case KBksp:
                c.fillRoundRect(r, UI::sx(3), flash ? THEME_ACCENT_D : THEME_CARD);
                c.drawRoundRect(r, UI::sx(3), THEME_STROKE);
                drawBackGlyph(c, r, flash ? THEME_TEXT : THEME_TEXT_DIM);
                break;
            case KMode:
                c.fillRoundRect(r, UI::sx(3), flash ? THEME_ACCENT_D : THEME_CARD);
                c.drawRoundRect(r, UI::sx(3), THEME_STROKE);
                c.text(isLetters(m_mode) ? "?123" : "ABC", r.x + r.w / 2, r.y + r.h / 2, UI::font(1),
                       THEME_TEXT, MC_DATUM);
                break;
        }
    }
}

bool KeyboardScreen::onTouch(const TouchEvent& ev) {
    if (m_done || !ev.isTap()) return false;
    if (cancelRect().contains(ev.x, ev.y)) {
        finish(false);
        return true;
    }
    for (int i = 0; i < (int)m_keys.size(); i++) {
        if (m_keys[i].r.contains(ev.x, ev.y)) {
            handleKey(i);
            return true;
        }
    }
    return false;
}

void KeyboardScreen::handleKey(int idx) {
    const Key& k = m_keys[idx];
    m_flashKey = idx;
    m_flashMs = millis();
    markDirty();

    switch (k.kind) {
        case KChar:
            if ((int)m_text.size() < m_maxLen) {
                m_text += labelChar(k.ch);
                if (m_mode == Upper) {  // shift one-shot
                    m_mode = Lower;
                    rebuild();
                }
            }
            break;
        case KSpace:
            if ((int)m_text.size() < m_maxLen) m_text += ' ';
            break;
        case KBksp:
            if (!m_text.empty()) m_text.pop_back();
            break;
        case KShift:
            m_mode = (m_mode == Upper) ? Lower : Upper;
            rebuild();
            break;
        case KSymPage:
            m_mode = (m_mode == Sym2) ? Sym1 : Sym2;
            rebuild();
            break;
        case KMode:
            m_mode = isLetters(m_mode) ? Sym1 : Lower;
            rebuild();
            break;
        case KOk:
            finish(true);
            break;
    }
}

void KeyboardScreen::finish(bool ok) {
    if (m_done) return;
    m_done = true;
    if (onResult) onResult(ok ? m_text : std::string(), ok);
}

void KeyboardScreen::onTick(uint32_t dtMs) {
    m_blinkMs += dtMs;
    if (m_blinkMs >= 530) {
        m_blinkMs = 0;
        m_cursorOn = !m_cursorOn;
        markDirty();
    }
    if (m_flashKey >= 0 && millis() - m_flashMs >= 150) {
        m_flashKey = -1;
        markDirty();
    }
}

// ============================================================ getString =====

std::string getString(const std::string& initialText, const std::string& promptMsg, int maxLen) {
    KeyboardScreen kb(promptMsg, initialText, maxLen);

    std::string result;
    bool done = false;
    bool ok = false;
    kb.onResult = [&](const std::string& text, bool confirmed) {
        result = text;
        ok = confirmed;
        done = true;
    };

    // Loop modal auto-contido: nao passa pelo Navigator para nao clobberar a
    // tela por baixo ao fechar (apps JS redesenham por conta propria).
    Canvas canvas(Board::display());
    TouchPump pump;
    while (!done) {
        pump.poll([&](const TouchEvent& ev) {
            kb.onTouch(ev);
            if (ev.type != TouchEvent::Drag) kb.markDirty();  // tecla "afunda"
        });
        kb.onTick(5);
        if (kb.consumeDirty()) canvas.render([&](Canvas& c) { kb.draw(c); });
        delay(5);
    }
    return ok ? result : std::string();
}

}  // namespace kui
