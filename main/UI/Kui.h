#ifndef KRYONOS_KUI_H
#define KRYONOS_KUI_H

// ============================================================================
// Kui — mini-framework de UI do KryonOS (W7).
//
// Principios:
//   1. UM dispatcher de input: o Navigator faz o unico poll de touch e
//      roteia eventos (press/drag/release com classificacao tap/swipe
//      correta) para a Screen do topo da pilha.
//   2. Telas sao classes (Screen) numa pilha com back-stack; ninguem
//      escreve em estado global de navegacao.
//   3. Desenho e hit-test da MESMA geometria: widgets expoem Rect; o
//      mesmo Rect desenha e testa o toque — fim dos hit-tests manuais.
//   4. Texto sempre transparente (sem cor de fundo): quem limpa o fundo
//      e o widget, por retangulo inteiro. Fundos "estranhos" acabam.
//   5. Canvas: sprite full-screen na PSRAM quando disponivel (transicao
//      sem flicker); sem PSRAM desenha direto no display.
//
// Coordenadas: fisicas (px do display). Para layout, usar UI::sx/sy do
// design 240x320 como hoje.
// ============================================================================

#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <vector>
#include <string>

#include "Boards/Board.h"
#include "Display/Layout.h"
#include "Display/Theme.h"

namespace kui {

// ---------------------------------------------------------------- Rect ----
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// ----------------------------------------------------------- TouchEvent ----
struct TouchEvent {
    enum Type { Press, Drag, Release } type;
    int x = 0, y = 0;          // posicao atual (fisica)
    int startX = 0, startY = 0;  // posicao do press
    int prevX = 0, prevY = 0;  // posicao do evento anterior (delta p/ scroll)
    uint32_t holdMs = 0;     // duracao ate agora (Release: tempo total)

    int dx() const { return x - startX; }
    int dy() const { return y - startY; }

    // tap = release rapido sem deslocamento relevante
    bool isTap() const { return type == Release && holdMs < 400 && abs(dx()) < UI::sx(25) && abs(dy()) < UI::sy(25); }
    // swipe = release com deslocamento dominante em um eixo
    enum Swipe { SwipeNone, SwipeLeft, SwipeRight, SwipeUp, SwipeDown };
    Swipe swipe() const {
        if (type != Release || holdMs > 700) return SwipeNone;
        int adx = abs(dx()), ady = abs(dy());
        if (adx < UI::sx(60) && ady < UI::sy(60)) return SwipeNone;
        if (adx > ady) return dx() > 0 ? SwipeRight : SwipeLeft;
        return dy() > 0 ? SwipeDown : SwipeUp;
    }
};

// ---------------------------------------------------------------- Canvas ----
// Superficie de desenho de um frame: sprite full-screen (PSRAM) ou display.
class Canvas {
public:
    explicit Canvas(KryonDisplay& dev);

    // Ciclo de um redesenho: begin() ... primitivas ... end()
    void begin();
    void end();

    // Primitivas (unidades fisicas; cores RGB888 como o resto do firmware)
    void fill(uint32_t color);
    void fillRect(const Rect& r, uint32_t color);
    void fillRoundRect(const Rect& r, int radius, uint32_t color);
    void drawRoundRect(const Rect& r, int radius, uint32_t color);
    void drawRect(const Rect& r, uint32_t color);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color);
    void drawCircle(int cx, int cy, int r, uint32_t color);
    void fillCircle(int cx, int cy, int r, uint32_t color);
    // Texto SEMPRE transparente (o widget limpa o proprio fundo antes)
    void text(const char* s, int x, int y, uint8_t font, uint32_t color, int datum = TL_DATUM);
    void text(const std::string& s, int x, int y, uint8_t font, uint32_t color, int datum = TL_DATUM);
    int textWidth(const char* s, uint8_t font);

    int width() const;
    int height() const;

private:
    KryonDisplay& m_dev;
    KryonSprite* m_sprite = nullptr;  // full-screen quando cabe (PSRAM)
    bool m_active = false;
    lgfx::LGFXBase* target();  // sprite quando ativo, senao o display
};

// ---------------------------------------------------------------- Screen ----
class Screen {
public:
    virtual ~Screen() = default;

    virtual void onEnter() {}                    // entrou na pilha
    virtual void onExit() {}                     // saiu da pilha
    virtual void draw(Canvas& c) = 0;            // redesenho completo da tela
    virtual bool onTouch(const TouchEvent& ev) { (void)ev; return false; }
    virtual void onTick(uint32_t dtMs) { (void)dtMs; }

    void markDirty() { m_dirty = true; }
    bool consumeDirty() {
        bool d = m_dirty;
        m_dirty = false;
        return d;
    }

private:
    bool m_dirty = true;
};

// --------------------------------------------------------------- Widgets ----
class Widget {
public:
    virtual ~Widget() = default;
    virtual void draw(Canvas& c) = 0;
    virtual bool onTouch(const TouchEvent& ev, Rect myRect) { (void)ev; (void)myRect; return false; }
    virtual void onTick(uint32_t) {}

    Rect rect;
    bool visible = true;
};

class Button : public Widget {
public:
    enum Style { Primary, Ghost, Danger };

    std::string label;
    Style style = Primary;
    std::function<void()> onTap;

    void draw(Canvas& c) override;
    bool onTouch(const TouchEvent& ev, Rect myRect) override;
};

class Label : public Widget {
public:
    std::string text;
    uint8_t font = 2;
    uint32_t color = THEME_TEXT;
    int datum = TL_DATUM;

    void draw(Canvas& c) override;
};

// Lista rolavel: itens de uma linha, selecao por tap, scroll por drag.
class List : public Widget {
public:
    struct Item {
        std::string label;
        std::string right;   // texto a direita (opcional)
        bool enabled = true;
    };

    std::vector<Item> items;
    int selected = -1;
    int top = 0;             // primeiro item visivel
    std::function<void(int)> onSelect;

    void draw(Canvas& c) override;
    bool onTouch(const TouchEvent& ev, Rect myRect) override;
    void ensureVisible(int idx);

    int visibleRows(const Rect& myRect) const;

private:
    int rowH() const { return UI::ITEM_H; }
    int m_dragAccum = 0;
    int m_pressRow = -1;
};

// Dialog modal: overlay escuro + card + botoes. Exibido pelo Navigator
// ACIMA da tela corrente; nao bloqueia o loop.
class Dialog : public Widget {
public:
    std::string title;
    std::string body;
    std::vector<Button> buttons;  // desenham lado a lado

    void draw(Canvas& c) override;
    bool onTouch(const TouchEvent& ev, Rect myRect) override;

    Rect cardRect() const;
};

// Toast: mensagem curta no topo, auto-dismiss. Fila gerenciada pelo Navigator.
class Toast {
public:
    std::string message;
    uint32_t shownAtMs = 0;
    uint32_t durationMs = 2500;
    uint32_t color = THEME_ACCENT;
};

// -------------------------------------------------------------- Navigator ----
class Navigator {
public:
    static void begin(KryonDisplay& dev);

    // Pilha
    static void push(Screen* s);       // entra por cima (desenha s)
    static void pop();                 // volta para a anterior (redesenha)
    static void replace(Screen* s);    // troca o topo
    static void home();                // esvazia ate a base (launcher)
    static Screen* top();
    static int depth();

    // Dialog/modal por cima da tela atual (nao entra na pilha)
    static void showDialog(Dialog* d);
    static void closeDialog();

    // Toasts (um por vez, fila)
    static void toast(const std::string& message, uint32_t color = THEME_ACCENT, uint32_t durationMs = 2500);

    // Loop: poll de touch + tick + redraw. Chamar do kryonLoop.
    static void tick();

    // Redesenho forcado da tela do topo
    static void repaint();

    static bool handleTouchRaw(uint16_t x, uint16_t y, bool down);
    static void pumpEvents();  // gera eventos a partir do estado do touch

private:
    Navigator() = delete;
};

}  // namespace kui

#endif  // KRYONOS_KUI_H
