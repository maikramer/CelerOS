#ifndef CELEROS_KUI_H
#define CELEROS_KUI_H

// ============================================================================
// Kui — mini-framework de UI do CelerOS (W7).
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
//   5. Canvas com buffer: o frame e composto fora da tela e enviado de uma
//      vez (zero flicker). Com PSRAM: sprite full-screen; sem PSRAM (CYD):
//      faixas horizontais na RAM interna — a tela e desenhada N vezes, uma
//      por faixa, entao draw() deve ser idempotente (sem efeitos colaterais
//      alem de calcular geometria).
//   6. Feedback de toque: widgets consultam kui::isPressed(rect) no draw e
//      o Navigator redesenha no press/release — todo alvo tocavel "afunda".
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
    // (mesma tolerancia do TouchState::moved: o que "afunda" e o que dispara)
    bool isTap() const { return type == Release && holdMs < 800 && abs(dx()) < slopX() && abs(dy()) < slopY(); }
    static int slopX() { return UI::sx(18); }
    static int slopY() { return UI::sy(18); }
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

// ----------------------------------------------------------- TouchState ----
// Estado do toque em andamento (atualizado pelo TouchPump). Widgets usam no
// draw() para o estado "pressionado".
struct TouchState {
    bool down = false;
    bool moved = false;       // saiu da tolerancia de tap (virou arrasto)
    int x = 0, y = 0;         // posicao atual
    int startX = 0, startY = 0;
};
const TouchState& touchState();
// Toque ativo, sem arrasto, que comecou e continua dentro de r
bool isPressed(const Rect& r);

// ------------------------------------------------------------- Tipografia ----
// Papeis tipograficos (escolhem a fonte certa para a densidade da tela).
// Os numeros de fonte legados (1/2/4) seguem valendo via CelerFont().
namespace type {
const lgfx::IFont* caption();   // legendas, rotulos de icone
const lgfx::IFont* body();      // texto corrido, itens de lista, botoes
const lgfx::IFont* title();     // titulos de tela (negrito)
const lgfx::IFont* display();   // relogio / numeros grandes (negrito)
}  // namespace type

// ---------------------------------------------------------------- Canvas ----
// Superficie de desenho de um frame. O buffer (sprite full-screen na PSRAM
// ou faixa na RAM interna) e unico e compartilhado entre instancias.
class Canvas {
public:
    enum Mode { Direct, FullFrame, Bands };

    explicit Canvas(CelerDisplay& dev);

    // Desenho direto num alvo arbitrario (Direct fixo, sem render()): usado
    // pela ponte JS para compor widgets (teclado acoplado) no MESMO alvo do
    // app — gfx(): quadro PSRAM, sprite do app ou display.
    Canvas(CelerDisplay& dev, lgfx::LGFXBase* directTarget);

    // Compoe um frame: chama fn uma vez (FullFrame/Direct) ou uma vez por
    // faixa (Bands) e apresenta o resultado. direct=true desenha direto no
    // display (telas legacy, que limpam/desenham por conta propria).
    void render(const std::function<void(Canvas&)>& fn, bool direct = false);

    // Modo do frame em andamento
    Mode mode() const { return m_mode; }

    // Primitivas (unidades fisicas; cores RGB888 como o resto do firmware)
    void fill(uint32_t color);
    void fillRect(const Rect& r, uint32_t color);
    void fillRoundRect(const Rect& r, int radius, uint32_t color);
    void fillGradient(const Rect& r, int radius, uint32_t top, uint32_t bottom);
    void drawRoundRect(const Rect& r, int radius, uint32_t color);
    void drawRect(const Rect& r, uint32_t color);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color);
    void drawFastHLine(int x, int y, int w, uint32_t color);
    void drawCircle(int cx, int cy, int r, uint32_t color);
    void fillCircle(int cx, int cy, int r, uint32_t color);
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);
    // Arco preenchido (angulos em graus, 0 = 3h, sentido horario)
    void fillArc(int cx, int cy, int r0, int r1, float a0, float a1, uint32_t color);
    void pushImage(int x, int y, int w, int h, const uint16_t* data);
    // Icone do pacote (Icon::draw): compoe o alpha sobre o fundo atual
    void drawIcon(const char* name, int x, int y);
    // Tile de app do usuario (gradiente + inicial)
    void drawAppTile(const char* appName, int x, int y);
    // Escurece tudo que ja foi desenhado neste frame (fundo de modal)
    void dim();
    // Recorte (coordenadas fisicas); clearClip restaura a tela inteira
    void setClip(const Rect& r);
    void clearClip();

    // Texto SEMPRE transparente (o widget limpa o proprio fundo antes)
    void text(const char* s, int x, int y, uint8_t font, uint32_t color, int datum = TL_DATUM);
    void text(const std::string& s, int x, int y, uint8_t font, uint32_t color, int datum = TL_DATUM);
    void text(const char* s, int x, int y, const lgfx::IFont* font, uint32_t color, int datum = TL_DATUM);
    void text(const std::string& s, int x, int y, const lgfx::IFont* font, uint32_t color, int datum = TL_DATUM);
    int textWidth(const char* s, uint8_t font);
    int textWidth(const char* s, const lgfx::IFont* font);
    int fontHeight(const lgfx::IFont* font);
    // Corta s com ".." ate caber em maxW
    std::string ellipsize(const std::string& s, const lgfx::IFont* font, int maxW);

    int width() const;
    int height() const;

private:
    CelerDisplay& m_dev;
    lgfx::LGFXBase* m_target;
    int m_offY = 0;        // topo da faixa corrente (Bands)
    Mode m_mode = Direct;
    int Y(int y) const { return y - m_offY; }
};

// Cabecalho padrao das telas Kui: faixa com titulo; devolve a altura.
int headerHeight();
void drawHeader(Canvas& c, const char* title);

// ---------------------------------------------------------------- Screen ----
class Screen {
public:
    virtual ~Screen() = default;

    virtual void onEnter() {}                    // entrou na pilha
    virtual void onExit() {}                     // saiu da pilha
    virtual void draw(Canvas& c) = 0;            // redesenho completo da tela
    virtual bool onTouch(const TouchEvent& ev) { (void)ev; return false; }
    virtual void onTick(uint32_t dtMs) { (void)dtMs; }
    // Telas antigas (embrulhadas) desenham direto no display, sem sprite
    virtual bool wantsDirectDraw() const { return false; }
    // Swipe a partir da borda esquerda volta (Navigator::pop)
    virtual bool allowsBackGesture() const { return true; }

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

// Lista rolavel: itens de uma linha, selecao por tap, scroll por pixel com
// inercia (fling). O dono chama tick(dt) no onTick e marca dirty se true.
class List : public Widget {
public:
    struct Item {
        std::string label;
        std::string right;   // texto a direita (opcional)
        int bars = -1;       // 0..4: indicador de sinal a direita (-1 = nenhum)
        bool enabled = true;
    };

    std::vector<Item> items;
    int selected = -1;
    std::function<void(int)> onSelect;

    void draw(Canvas& c) override;
    bool onTouch(const TouchEvent& ev, Rect myRect) override;
    bool tick(uint32_t dtMs);   // anima a inercia; true = precisa redesenhar
    void ensureVisible(int idx);
    void scrollToTop() { m_scroll = 0; m_vel = 0; }

    int visibleRows(const Rect& myRect) const;
    static int rowH();

private:
    int maxScroll() const;
    void clampScroll();
    float m_scroll = 0;      // px rolados
    float m_vel = 0;         // px/ms (positivo = conteudo sobe)
    uint32_t m_lastDragMs = 0;
    bool m_dragging = false;
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

// Leitura unica do touch (fisico ou injetado pelo celerctl). Todo consumidor
// (TouchPump, apps JS) passa por aqui. Sem toque, x/y nao sao escritos.
bool readTouch(uint16_t* x, uint16_t* y);

// ------------------------------------------------------------- TouchPump ----
// Classificador de touch: converte o estado bruto do display em eventos
// Press/Drag/Release (com tap/swipe derivados no TouchEvent). O Navigator
// usa um; modais auto-contidos (ex.: Keyboard::getString) usam o proprio.
class TouchPump {
public:
    using Handler = std::function<void(const TouchEvent&)>;

    // Le o touch agora e entrega 0..1 eventos ao handler.
    void poll(const Handler& onEvent);

    // Janela de quarentena: os pumps descartam eventos ate a janela vencer E o
    // vidro ler dedo solto. Para transicoes feitas pelo sistema (app JS
    // fechando): o release do toque que fechou o app nao pode virar tap na
    // tela de baixo (ex.: X do canto -> icone de WiFi do launcher).
    static void quarantine(uint32_t ms);

private:
    bool m_down = false;
    int m_lastX = 0, m_lastY = 0;
    int m_pressX = 0, m_pressY = 0;
    uint32_t m_pressMs = 0;
};

// ------------------------------------------------------------ TouchInjector ----
// Fila de amostras de touch sinteticas (celerctl tap/swipe). Os TouchPump
// consomem a fila durante o poll; enquanto houver amostras pendentes (ou
// espera de timing), o touch fisico e ignorado — o gesto injetado e dono do
// pump. Preenchida pela task do CelerLink, drenada pelo loop da UI.
class TouchInjector {
public:
    struct Sample {
        bool down;
        uint16_t x = 0, y = 0;
        uint16_t delayMs = 0;  // espera ANTES de virar estado do dedo
    };

    static bool push(const Sample* samples, size_t n);  // false: fila cheia
    static bool active();                               // gesto em curso
    static bool take(Sample& out);                      // pop se o delay venceu

private:
    TouchInjector() = delete;
};

// -------------------------------------------------------------- Navigator ----
class Navigator {
public:
    static void begin(CelerDisplay& dev);

    // Pilha
    static void push(Screen* s);       // entra por cima (desenha s)
    static void pop();                 // volta para a anterior (redesenha)
    static void replace(Screen* s);    // troca o topo
    static void remove(Screen* s);     // tira s da pilha (mesmo sem ser o topo)
    static void home();                // esvazia ate a base (launcher)
    static Screen* top();
    static int depth();

    // Dialog/modal por cima da tela atual (nao entra na pilha)
    static void showDialog(Dialog* d);
    static void closeDialog();

    // Toasts (um por vez, fila)
    static void toast(const std::string& message, uint32_t color = THEME_ACCENT, uint32_t durationMs = 2500);

    // Loop: poll de touch + tick + redraw. Chamar do celerLoop.
    static void tick();

    // Redesenho forcado da tela do topo
    static void repaint();

    static void pumpEvents();  // gera eventos a partir do estado do touch

private:
    Navigator() = delete;
};

}  // namespace kui

#endif  // CELEROS_KUI_H
