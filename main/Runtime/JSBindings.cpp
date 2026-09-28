#include "JSBindings.h"
#include "../USBDevice/LogSink.h"
#include "../Display/Layout.h"
#include "../FileSystem/FileSystem.h"
#include "../UI/Keyboard.h"
#include "../WebManager/WebManager.h"
#include "../WebManager/WebAuth.h"
#include "../Kernel/TimeManager.h"
#include "../Utils/StrUtils.h"
#include "../Utils/PinStore.h"
#include "HttpClient.h"
#include "SystemInfo.h"
#include "esp_rom_md5.h"
#include "../Display/Backlight.h"
#include "../Display/Theme.h"
#include "../Display/Icon.h"
#include "../OTA/OtaManager.h"
#include "../Launcher/LauncherUI.h"
#include "../Launcher/Screens.h"
#include <lgfx/v1/misc/DataWrapper.hpp>

// ---------------------------------------------------------------------------
// Camada de compatibilidade JS: canvas virtual 240x320 + cores RGB565.
//
// Os apps da loja sao escritos para o alvo classico (CYD): cores literais
// RGB565 (0xF800, 0x18E3...) e geometria 240x320. Em paineis maiores o
// runtime "mente" para o app: reporta 240x320, escala coordenadas/tamanhos
// para a tela fisica (UI::sx/sy) e converte cores RGB565 -> formato nativo.
// No alvo classico os fatores sao 1:1 e a camada e transparente.
// ---------------------------------------------------------------------------
static CelerDisplay* s_jsTft = nullptr;  // setado no JSBindings::init

static inline uint32_t jsc(uint32_t c) {
    // Cores JS sao RGB565. No LovyanGFX o TIPO decide o formato: uint32_t e
    // lido como RGB888 — passar o 565 cru (como antes, "painel 16-bit")
    // trocava as cores. Expande sempre para 888; o LGFX converte ao nativo.
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}
static inline int jsx(int v) { return UI::sx(v); }

// Topbar do sistema (titulo + X de sair): s_exitArmed = dedo sobre o X no
// ultimo poll (tambem e o estado "hot" da faixa); s_barOnGlass/s_barHotOnGlass
// = estado da faixa que ja esta no vidro (present so recompoem quando muda).
//
// Dois modos (app.json "topbar"):
//   fixo    — faixa sempre visivel; o canvas do app e a area abaixo dela;
//   retratil — app em tela cheia; swipe de cima para baixo na borda revela a
//              faixa por alguns segundos (RETRACT_MS). Todo o gesto de revelar
//              e consumido: o app nunca ve press/release dele.
static bool s_exitArmed = false;
static bool s_barOnGlass = false;
static bool s_barHotOnGlass = false;
static int s_armX = 0, s_armY = 0;   // onde o toque no X comecou
static const char* s_appTitle = "";
static bool s_topbarFixed = true;    // setado por app pelo launcher (init)
static bool s_barShown = false;      // retratil: faixa visivel agora
static uint32_t s_barShownAt = 0;
static bool s_tbSwipe = false;       // gesto de revelar em andamento
static int s_tbSwipeY0 = 0;
static const uint32_t RETRACT_MS = 3000;  // "aparece por alguns segundos"

// Conteudo custom da faixa (System.topbarText/topbarButtons — API 6):
// texto no lugar do nome do app e chips tocaveis a direita, antes do X. Os
// toques nos chips viram fila para System.topbarPop() (devolve o label).
struct TbButton {
    std::string label;
    int x = 0, w = 0;   // rect virtual (240 de largura; altura = faixa inteira)
};
static std::vector<TbButton> s_tbButtons;      // [0] = o mais a direita
static std::vector<std::string> s_tbTaps;      // FIFO de toques pendentes
static std::string s_tbText;                   // vazio = usa o nome do app
static bool s_tbTextCustom = false;
static int s_tbHotBtn = -1;                    // chip sob o dedo (feedback)
static int s_tbBtnArmed = 0;                   // chip armado p/ disparo no release (indice+1)
static bool s_tbDirty = false;                 // faixa precisa recompor
static int s_tbHotOnGlass = -2;                // hot state ja composto (-2 = nada)

// expira a faixa retratil; true se ela acabou de sair
static bool retractTick() {
    if (!s_topbarFixed && s_barShown && millis() - s_barShownAt > RETRACT_MS) {
        s_barShown = false;
        s_exitArmed = false;
        celer_log_println("[TB] auto-hide");
        return true;
    }
    return false;
}

// Escala vertical do canvas JS: no modo fixo a topbar fica FORA do canvas (o
// app desenha em 240x320 mapeado na area abaixo da faixa); no retratil o app
// e tela cheia (mapa identico ao pre-topbar).
static inline int appSh(int v) {
    return s_topbarFixed ? (v * (UI::H - UI::topbarH()) / 320) : v * UI::H / 320;
}
// Escala vertical fracionaria (drawBMP/drawPNG escalam por float)
static inline float appScaleY() {
    return (float)(s_topbarFixed ? (UI::H - UI::topbarH()) : UI::H) / 320.0f;
}
// jsy (alvo-dependent: quadro/display x sprite do app) vive depois das
// declaracoes de tftSprite/useSprite, logo abaixo.
static inline int jsu(int v) { return (UI::sx(v) + appSh(v)) / 2; }  // uniforme (raios)
// jsy e mapa de COORDENADA (soma o offset da faixa no modo fixo); jsH e a
// escala pura de TAMANHO vertical — altura/largura-vertical nunca podem
// receber o offset (uma linha de 3px viraria uma faixa de ~31px)
static inline int jsH(int v) { return appSh(v); }

// Wrapper stdio para o drawPngFile do LGFX (a especializacao DataWrapperT<FILE>
// do upstream so ativa com macros do newlib que nao estao definidas no IDF)
struct CelerFileWrapper : public lgfx::DataWrapper {
    bool open(const char* path) override {
        while (nullptr == (_fp = fopen(path, "rb")) && path[0] == '/') ++path;
        return _fp != nullptr;
    }
    int read(uint8_t* buf, uint32_t len) override { return (int)fread(buf, 1, len, _fp); }
    void skip(int32_t offset) override { fseek(_fp, offset, SEEK_CUR); }
    bool seek(uint32_t offset) override { return fseek(_fp, offset, SEEK_SET) == 0; }
    void close(void) override {
        if (_fp) {
            fclose(_fp);
            _fp = nullptr;
        }
    }
    int32_t tell(void) override { return ftell(_fp); }

private:
    FILE* _fp = nullptr;
};

CelerDisplay* JSBindings::tftInstance = nullptr;

// ---------------------------------------------------------------------------
// Quadro automatico (double buffer transparente para o app).
//
// Com PSRAM, TODO desenho do app vai para um sprite do tamanho da tela e so
// aparece no vidro quando o script "cede" (delay/getTouch/prompt ou antes de
// uma chamada bloqueante — rede, OTA, scan). Apps que fazem fillScreen +
// redesenho completo a cada evento deixam de piscar sem mudar uma linha.
// Sem PSRAM (CYD) o desenho segue direto no display, como antes.
// Prioridade do alvo: sprite do app (bindSprite) > quadro > display.
// ---------------------------------------------------------------------------
static CelerSprite* s_frame = nullptr;
static bool s_frameDirty = false;

// Topbar no estilo da barra que o Terminal desenhava: card, linha de stroke,
// titulo a esquerda e X a direita. hot (dedo sobre o X) clareia o traco e
// acende a pastilha — feedback antes de soltar.
static void drawAppTopbar(lgfx::LGFXBase& g, bool hot) {
    int h = UI::topbarH();
    g.fillRect(0, 0, UI::W, h, THEME_CARD);
    g.drawFastHLine(0, h - 1, UI::W, THEME_STROKE);

    // X: glifo menor e colado a direita (a zona de TOQUE continua 40 px —
    // alvo generoso, glifo discreto)
    if (hot) {
        g.fillRoundRect(UI::W - UI::topbarExitW() + UI::sx(3), 1,
                        UI::topbarExitW() - 2 * UI::sx(3), h - 2, UI::sx(6), THEME_RAISED);
    }
    int gx = UI::W - UI::sx(13);
    int r = h / 3;       // meia-diagonal do X (menor: era h*2/5)
    int t = h / 9;       // meia-espessura do traco
    if (t < 1) t = 1;
    uint32_t col = hot ? THEME_TEXT : THEME_TEXT_DIM;
    // barras diagonais espessas: 2 triangulos por barra (offset perpendicular)
    auto bar = [&](int ax, int ay, int bx, int by, int px, int py) {
        g.fillTriangle(ax + px, ay + py, bx + px, by + py, bx - px, by - py, col);
        g.fillTriangle(ax + px, ay + py, bx - px, by - py, ax - px, ay - py, col);
    };
    bar(gx - r, h / 2 - r, gx + r, h / 2 + r, t, -t);  // "\"
    bar(gx - r, h / 2 + r, gx + r, h / 2 - r, t, t);   // "/"

    // chips custom (topbarButtons): da direita p/ a esquerda, antes do X
    for (int i = 0; i < (int)s_tbButtons.size(); i++) {
        const TbButton& b = s_tbButtons[i];
        int bx = UI::sx(b.x), bw = UI::sx(b.w);
        bool chipHot = (i == s_tbHotBtn);
        g.fillRoundRect(bx, 2, bw, h - 4, UI::sx(5), chipHot ? THEME_ACCENT_D : THEME_RAISED);
        g.drawRoundRect(bx, 2, bw, h - 4, UI::sx(5), THEME_STROKE);
        g.setTextDatum(MC_DATUM);
        g.setTextColor(chipHot ? THEME_TEXT : THEME_TEXT_DIM);
        g.drawString(b.label.c_str(), bx + bw / 2, h / 2, kui::type::caption());
    }

    // texto: custom (topbarText) ou nome do app; corta antes do 1o chip
    std::string label = s_tbTextCustom ? s_tbText : (s_appTitle ? s_appTitle : "");
    int rightLimit = UI::W - UI::topbarExitW();
    if (!s_tbButtons.empty()) rightLimit = UI::sx(s_tbButtons.back().x);
    while (!label.empty() &&
           g.textWidth(label.c_str(), kui::type::caption()) > rightLimit - UI::sx(10)) {
        label.pop_back();
    }
    g.setTextDatum(ML_DATUM);
    g.setTextColor(THEME_TEXT);
    g.drawString(label.c_str(), UI::sx(6), h / 2, kui::type::caption());
}

// indice do chip sob o x virtual (ou -1)
static int tbButtonAt(int jx) {
    for (int i = 0; i < (int)s_tbButtons.size(); i++) {
        if (jx >= s_tbButtons[i].x && jx < s_tbButtons[i].x + s_tbButtons[i].w) return i;
    }
    return -1;
}

// Saida de app com "debounce": dispara so no RELEASE em que o toque comecou
// no X (armado) e o dedo nao se afastou mais que a tolerancia de tap — jitter
// do controlador nao desarma (bug do glifo: mudava de cor e nao saia), e
// arrasto para a area do app devolve o gesto a ele. Toque na faixa e
// mascarado: o app nunca ve coordenadas do chrome. Chips (topbarButtons)
// seguem o mesmo contrato e viram fila em System.topbarPop().
static bool pollAppChrome(bool& touched, uint16_t& x, uint16_t& y) {
    const int slopX = UI::sx(18), slopY = UI::sy(18);

    if (!s_topbarFixed) {
        // ---- modo retratil: gesto de revelar + faixa transitoria ----
        retractTick();
        if (s_tbSwipe) {  // dedo ainda descendo a partir da borda
            if (!touched) {
                s_tbSwipe = false;             // soltou sem arrastar o bastante
            } else if ((int)y - s_tbSwipeY0 >= UI::sy(12)) {
                s_tbSwipe = false;
                s_barShown = true;             // revela por RETRACT_MS
                s_barShownAt = millis();
                celer_log_println("[TB] show");
            }
            touched = false;                   // gesto inteiro e chrome
            return false;
        }
        if (touched && !s_barShown && UI::inTopbar(x, y)) {
            s_tbSwipe = true;                  // pode virar o gesto de revelar
            s_tbSwipeY0 = y;
            touched = false;
            return false;
        }
        if (!s_barShown) return false;         // faixa oculta: tudo e do app
        // visivel: cai no chrome comum abaixo (X/chips armaveis, faixa mascarada)
    }

    if (touched && UI::inTopbar(x, y)) {
        int jx = s_jsTft ? ((int)x * 240 / s_jsTft->width()) : 0;
        int btn = tbButtonAt(jx);
        if (btn >= 0) {
            // chip: armamento proprio com disparo no release (mesmo contrato do X)
            if (s_tbBtnArmed != btn + 1) {
                s_tbBtnArmed = btn + 1;
                s_tbHotBtn = btn;
                s_armX = x;
                s_armY = y;
            } else if (abs((int)x - s_armX) > slopX || abs((int)y - s_armY) > slopY) {
                s_tbBtnArmed = 0;              // deslizou: nao e tap
                s_tbHotBtn = -1;
            }
            s_exitArmed = false;
        } else {
            if (s_tbBtnArmed) {                // saiu do chip para a faixa
                s_tbBtnArmed = 0;
                s_tbHotBtn = -1;
            }
            if (!s_exitArmed) {
                if (UI::hitTopbarExit(x, y)) {
                    s_exitArmed = true;
                    s_armX = x;
                    s_armY = y;
                }
            } else if (abs((int)x - s_armX) > slopX || abs((int)y - s_armY) > slopY) {
                s_exitArmed = false;  // deslizou dentro da faixa: nao e tap no X
            }
        }
        touched = false;
        return false;
    }
    if (touched) {
        if (s_tbBtnArmed) {                    // arrastou para a area do app
            s_tbBtnArmed = 0;
            s_tbHotBtn = -1;
        }
        if (s_exitArmed && (abs((int)x - s_armX) > slopX || abs((int)y - s_armY) > slopY)) {
            s_exitArmed = false;  // deslizou para a area do app: gesto do app
        }
        return false;
    }
    if (s_tbBtnArmed) {                        // release no chip: evento ao app
        int b = s_tbBtnArmed - 1;
        s_tbBtnArmed = 0;
        s_tbHotBtn = -1;
        if (b < (int)s_tbButtons.size()) s_tbTaps.push_back(s_tbButtons[b].label);
        return false;
    }
    bool fire = s_exitArmed;
    s_exitArmed = false;
    return fire;
}

CelerSprite* JSBindings::tftSprite = nullptr;
bool JSBindings::useSprite = false;

int JSBindings::mapY(int v) {
    return (useSprite && tftSprite) ? appSh(v)
                                    : (s_topbarFixed ? UI::topbarH() : 0) + appSh(v);
}
static inline int jsy(int v) { return JSBindings::mapY(v); }


lgfx::LGFXBase* JSBindings::gfx() {
    if (useSprite && tftSprite) return tftSprite;
    if (s_frame != nullptr) {
        s_frameDirty = true;
        return s_frame;
    }
    return tftInstance;
}

void JSBindings::present() {
    if (tftInstance == nullptr) return;
    retractTick();
    bool wantBar = s_topbarFixed || s_barShown;
    if (s_frame != nullptr) {
        // So recompoem se algo mudou (desenho do app, topbarText/Buttons ou
        // estado hot da faixa)
        if (!s_frameDirty && !s_tbDirty && wantBar == s_barOnGlass &&
            (!wantBar || (s_exitArmed == s_barHotOnGlass && s_tbHotBtn == s_tbHotOnGlass))) return;
        // A topbar vai DENTRO do quadro, antes do push: chega ao vidro atomica
        // com o conteudo do app (nao pisca) e o app nao consegue cobri-la. No
        // retratil, parar de compo-la restaura a area no proximo push.
        if (wantBar) drawAppTopbar(*s_frame, s_exitArmed);
        s_frame->pushSprite(tftInstance, 0, 0);
        s_frameDirty = false;
        s_tbDirty = false;
        s_barOnGlass = wantBar;
        s_barHotOnGlass = wantBar && s_exitArmed;
        s_tbHotOnGlass = s_tbHotBtn;
    } else {
        // Sem quadro (CYD): o app desenha direto no display, entao a barra so
        // pode ir no vidro apos cada present (reaparece a cada cedida). Ao
        // esconder a retratil, a faixa permanece ate o app repintar a regiao
        // (os jogos a cobrem no frame seguinte).
        if (wantBar) drawAppTopbar(*tftInstance, s_exitArmed);
        s_tbDirty = false;
        s_barOnGlass = wantBar;
        s_barHotOnGlass = wantBar && s_exitArmed;
        s_tbHotOnGlass = s_tbHotBtn;
    }
}

void JSBindings::fatalErrorHandler(void *udata, const char *msg) {
    (void) udata;
    celer_log_print("*** FATAL ERROR: ");
    celer_log_println(msg ? msg : "no message");
    
    if (msg && strstr(msg, "alloc")) {
        celer_log_println("out of memory");
        if (tftInstance) {
            tftInstance->fillScreen(TFT_RED);
            tftInstance->setTextColor(TFT_WHITE, TFT_RED);
            tftInstance->drawString("OUT OF MEMORY", 10, 10, 4);
        }
    }
    
    abort();
}

// =====================================================
// GPIO Bindings
// =====================================================

duk_ret_t JSBindings::js_pinMode(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int mode = duk_require_int(ctx, 1);
    pinMode(pin, mode);
    return 0;
}

duk_ret_t JSBindings::js_digitalWrite(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int val = duk_require_int(ctx, 1);
    digitalWrite(pin, val);
    return 0;
}

duk_ret_t JSBindings::js_digitalRead(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int val = digitalRead(pin);
    duk_push_int(ctx, val);
    return 1;
}

duk_ret_t JSBindings::js_analogRead(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int val = analogRead(pin);
    duk_push_int(ctx, val);
    return 1;
}

duk_ret_t JSBindings::js_analogWrite(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int val = duk_require_int(ctx, 1);
    analogWrite(pin, val);
    return 0;
}

duk_ret_t JSBindings::js_pulseIn(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int state = duk_require_int(ctx, 1);
    unsigned long timeout = 1000000L; // default 1 second timeout
    if (duk_get_top(ctx) >= 3) {
        timeout = duk_require_uint(ctx, 2);
    }
    
    unsigned long duration = pulseIn(pin, state, timeout);
    duk_push_uint(ctx, duration);
    return 1;
}


// =====================================================
// Double Buffering
// =====================================================

duk_ret_t JSBindings::js_createSprite(duk_context *ctx) {
    if (!tftInstance) return 0;

    int w = duk_require_int(ctx, 0);
    int h = duk_require_int(ctx, 1);
    // Canvas virtual: o sprite e alocado no tamanho FISICO equivalente
    int pw = jsx(w);
    int ph = appSh(h);  // sprite = canvas do app: area abaixo da topbar

    if (tftSprite) {
        tftSprite->deleteSprite();
        delete tftSprite;
        tftSprite = nullptr;
    }

    tftSprite = new CelerSprite(tftInstance);
    // Prefere PSRAM quando disponivel (sem PSRAM o LovyanGFX usa o heap)
    tftSprite->setPsram(true);

    void* ptr = nullptr;

    // First try 16-bit color if we have plenty of contiguous RAM
    if (ESP.getMaxAllocHeap() > (uint32_t)(pw * ph * 2 + 10000)) {
        tftSprite->setColorDepth(16);
        ptr = tftSprite->createSprite(pw, ph);
    }

    // Fallback to 8-bit color if 16-bit failed or wasn't attempted
    if (!ptr) {
        tftSprite->setColorDepth(8);
        ptr = tftSprite->createSprite(pw, ph);
    }
    
    if (!ptr) {
        delete tftSprite;
        tftSprite = nullptr;
        duk_push_boolean(ctx, false);
        return 1;
    }
    
    duk_push_boolean(ctx, true);
    return 1;
}

duk_ret_t JSBindings::js_deleteSprite(duk_context *ctx) {
    if (tftSprite) {
        tftSprite->deleteSprite();
        delete tftSprite;
        tftSprite = nullptr;
    }
    useSprite = false;
    return 0;
}

duk_ret_t JSBindings::js_pushSprite(duk_context *ctx) {
    if (!tftInstance || !tftSprite) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    // Destino (quadro/display): origem do app (abaixo da topbar no fixo)
    if (s_frame != nullptr) {
        tftSprite->pushSprite(s_frame, jsx(x), JSBindings::mapY(y));
        s_frameDirty = true;
    } else {
        tftSprite->pushSprite(jsx(x), JSBindings::mapY(y));
    }
    return 0;
}

duk_ret_t JSBindings::js_bindSprite(duk_context *ctx) {
    bool enable = duk_require_boolean(ctx, 0);
    if (tftSprite) useSprite = enable;
    else useSprite = false;
    return 0;
}

duk_ret_t JSBindings::js_drawFastVLine(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int h = duk_require_int(ctx, 2);
    uint32_t color = duk_require_uint(ctx, 3);
    gfx()->drawFastVLine(jsx(x), jsy(y), jsH(h), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawFastHLine(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    uint32_t color = duk_require_uint(ctx, 3);
    gfx()->drawFastHLine(jsx(x), jsy(y), jsx(w), jsc(color));
    return 0;
}

// =====================================================
// Display Bindings - Drawing Primitives
// =====================================================

duk_ret_t JSBindings::js_fillScreen(duk_context *ctx) {
    if (!tftInstance) return 0;
    uint32_t color = duk_require_uint(ctx, 0);
    gfx()->fillScreen(jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_fillRect(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    int h = duk_require_int(ctx, 3);
    uint32_t color = duk_require_uint(ctx, 4);
    gfx()->fillRect(jsx(x), jsy(y), jsx(w), jsH(h), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawRect(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    int h = duk_require_int(ctx, 3);
    uint32_t color = duk_require_uint(ctx, 4);
    gfx()->drawRect(jsx(x), jsy(y), jsx(w), jsH(h), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawLine(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x0 = duk_require_int(ctx, 0);
    int y0 = duk_require_int(ctx, 1);
    int x1 = duk_require_int(ctx, 2);
    int y1 = duk_require_int(ctx, 3);
    uint32_t color = duk_require_uint(ctx, 4);
    gfx()->drawLine(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawPixel(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    uint32_t color = duk_require_uint(ctx, 2);
    gfx()->drawPixel(jsx(x), jsy(y), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawCircle(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int r = duk_require_int(ctx, 2);
    uint32_t color = duk_require_uint(ctx, 3);
    gfx()->drawCircle(jsx(x), jsy(y), jsu(r), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_fillCircle(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int r = duk_require_int(ctx, 2);
    uint32_t color = duk_require_uint(ctx, 3);
    gfx()->fillCircle(jsx(x), jsy(y), jsu(r), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawTriangle(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x0 = duk_require_int(ctx, 0);
    int y0 = duk_require_int(ctx, 1);
    int x1 = duk_require_int(ctx, 2);
    int y1 = duk_require_int(ctx, 3);
    int x2 = duk_require_int(ctx, 4);
    int y2 = duk_require_int(ctx, 5);
    uint32_t color = duk_require_uint(ctx, 6);
    gfx()->drawTriangle(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsx(x2), jsy(y2), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_fillTriangle(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x0 = duk_require_int(ctx, 0);
    int y0 = duk_require_int(ctx, 1);
    int x1 = duk_require_int(ctx, 2);
    int y1 = duk_require_int(ctx, 3);
    int x2 = duk_require_int(ctx, 4);
    int y2 = duk_require_int(ctx, 5);
    uint32_t color = duk_require_uint(ctx, 6);
    gfx()->fillTriangle(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsx(x2), jsy(y2), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawRoundRect(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    int h = duk_require_int(ctx, 3);
    int r = duk_require_int(ctx, 4);
    uint32_t color = duk_require_uint(ctx, 5);
    gfx()->drawRoundRect(jsx(x), jsy(y), jsx(w), jsH(h), jsu(r), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_fillRoundRect(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    int h = duk_require_int(ctx, 3);
    int r = duk_require_int(ctx, 4);
    uint32_t color = duk_require_uint(ctx, 5);
    gfx()->fillRoundRect(jsx(x), jsy(y), jsx(w), jsH(h), jsu(r), jsc(color));
    return 0;
}

// Caminhos de imagem aceitos: pontos de montagem reais do VFS
static bool imagePathOk(const char* path) {
    return strncmp(path, "/sd", 3) == 0 || strncmp(path, "/local", 6) == 0;
}

duk_ret_t JSBindings::js_drawBMP(duk_context *ctx) {
    // Decoder do LovyanGFX (16/24/32 bpp, RLE) com a escala do canvas
    // virtual: a imagem ocupa na tela fisica o mesmo espaco que no 240x320.
    if (!tftInstance) return 0;
    const char *path = duk_require_string(ctx, 0);
    int x = duk_require_int(ctx, 1);
    int y = duk_require_int(ctx, 2);
    if (!imagePathOk(path)) {
        duk_push_boolean(ctx, 0);
        return 1;
    }
    CelerFileWrapper file;
    bool ok = gfx()->drawBmpFile(&file, path, jsx(x), jsy(y), 0, 0, 0, 0,
                                 (float)UI::W / 240.0f,
                                 appScaleY());
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}

// =====================================================
// Display Bindings - Text
// =====================================================

duk_ret_t JSBindings::js_drawString(duk_context *ctx) {
    if (!tftInstance) return 0;
    const char *str = duk_require_string(ctx, 0);
    int x = duk_require_int(ctx, 1);
    int y = duk_require_int(ctx, 2);
    int font = duk_get_int_default(ctx, 3, 2); // default to font 2
    gfx()->setTextDatum(TL_DATUM);
    gfx()->drawString(str, jsx(x), jsy(y), CelerFont(UI::font(font)));
    return 0;
}

duk_ret_t JSBindings::js_setTextColor(duk_context *ctx) {
    if (!tftInstance) return 0;
    uint32_t fg = duk_require_uint(ctx, 0);
    // Optional background color (defaults to foreground = transparent)
    if (duk_is_number(ctx, 1)) {
        uint32_t bg = duk_require_uint(ctx, 1);
        gfx()->setTextColor(jsc(fg), jsc(bg));
    } else {
        gfx()->setTextColor(jsc(fg));
    }
    return 0;
}

duk_ret_t JSBindings::js_setTextSize(duk_context *ctx) {
    if (!tftInstance) return 0;
    int size = duk_require_int(ctx, 0);
    gfx()->setTextSize(size);
    return 0;
}

// =====================================================
// Display Bindings - Utility
// =====================================================

// Convert RGB888 (0-255 per channel) to the target's native color format
duk_ret_t JSBindings::js_color(duk_context *ctx) {
    int r = duk_require_int(ctx, 0);
    int g = duk_require_int(ctx, 1);
    int b = duk_require_int(ctx, 2);
    // Clamp values
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    // Convencao JS: cores sao RGB565 (mesmo valor em qualquer placa)
    duk_push_uint(ctx, ((uint32_t)(r & 0xF8) << 8) | ((uint32_t)(g & 0xFC) << 3) | ((uint32_t)b >> 3));
    return 1;
}

duk_ret_t JSBindings::js_screenWidth(duk_context *ctx) {
    // Canvas virtual: os apps veem o tamanho de projeto (240)
    duk_push_int(ctx, 240);
    return 1;
}

duk_ret_t JSBindings::js_screenHeight(duk_context *ctx) {
    // Canvas virtual: os apps veem o tamanho de projeto (320)
    duk_push_int(ctx, 320);
    return 1;
}

// =====================================================
// Touch Input
// =====================================================

// Returns an object { x, y, touched } 
duk_ret_t JSBindings::js_getTouch(duk_context *ctx) {
    uint16_t tx = 0, ty = 0;
    bool touched = false;
    present();  // app cedeu: o frame desenhado ate aqui vai ao vidro
    if (tftInstance) {
        touched = kui::readTouch(&tx, &ty);

        // Topbar (X de sair): dispara so no release; toque na faixa e chrome
        if (pollAppChrome(touched, tx, ty)) {
            duk_error(ctx, DUK_ERR_ERROR, "OS_EXIT");
            return 0; // Unreachable, but good practice
        }
    }

    // Coordenadas no espaco de projeto 240x320 (hit-zones dos apps batem);
    // vertical desconta a topbar do sistema
    int jx = tftInstance ? ((int)tx * 240 / tftInstance->width()) : 0;
    int jy = 0;
    if (tftInstance) {
        // fixo: vertical desconta a topbar; retratil: tela cheia 1:1
        int offY = s_topbarFixed ? UI::topbarH() : 0;
        jy = ((int)ty - offY) * 320 / ((int)tftInstance->height() - offY);
    }
    duk_push_object(ctx);
    duk_push_int(ctx, touched ? jx : 0);
    duk_put_prop_string(ctx, -2, "x");
    duk_push_int(ctx, touched ? jy : 0);
    duk_put_prop_string(ctx, -2, "y");
    duk_push_boolean(ctx, touched ? 1 : 0);
    duk_put_prop_string(ctx, -2, "touched");
    return 1;
}

// =====================================================
// System Utilities
// =====================================================

duk_ret_t JSBindings::js_millis(duk_context *ctx) {
    duk_push_uint(ctx, millis());
    return 1;
}

duk_ret_t JSBindings::js_micros(duk_context *ctx) {
    duk_push_uint(ctx, micros());
    return 1;
}

duk_ret_t JSBindings::js_delay(duk_context *ctx) {
    int ms = duk_require_int(ctx, 0);
    present();
    uint32_t t0 = millis();
    // Mark-and-sweep completo (so ciclos; o resto e refcount) custa ms em
    // heaps grandes: antes rodava a CADA delay (loops de 20 ms gastavam boa
    // parte do tempo aqui). Agora no maximo 1x/s, ou ja se o heap aperta.
    static uint32_t lastGcMs = 0;
    if (t0 - lastGcMs > 1000 || ESP.getMaxAllocHeap() < 24 * 1024) {
        duk_gc(ctx, 0);
        lastGcMs = t0;
    }
    if (ms > 0 && ms < 30000) { // Safety cap at 30 seconds
        uint32_t spent = millis() - t0;
        if ((uint32_t)ms > spent) delay(ms - spent);
    }
    return 0;
}

duk_ret_t JSBindings::js_delayMicroseconds(duk_context *ctx) {
    int us = duk_require_int(ctx, 0);
    if (us > 0) {
        delayMicroseconds(us);
    }
    return 0;
}

duk_ret_t JSBindings::js_print(duk_context *ctx) {
    const char *msg = duk_require_string(ctx, 0);
    celer_log_println(msg);
    return 0;
}

duk_ret_t JSBindings::js_getTemperature(duk_context *ctx) {
    float temp = temperatureRead();
    duk_push_number(ctx, temp);
    return 1;
}

duk_ret_t JSBindings::js_hasTemperatureSensor(duk_context *ctx) {
    float temp = temperatureRead();
    // 53.33 is a common return value when the sensor is unsupported or disconnected internally
    bool hasSensor = (temp != 53.33f);
    duk_push_boolean(ctx, hasSensor);
    return 1;
}

duk_ret_t JSBindings::js_getInfo(duk_context *ctx) {
    SystemInfo& sys = SystemInfo::instance();
    MemoryInfo mem = sys.getMemoryInfo();
    ChipInfo chip = sys.getChipInfo();

    duk_push_object(ctx);

    // RAM
    duk_push_uint(ctx, mem.totalHeap);
    duk_put_prop_string(ctx, -2, "totalRAM");

    duk_push_uint(ctx, mem.freeHeap);
    duk_put_prop_string(ctx, -2, "freeRAM");

    duk_push_uint(ctx, mem.minFreeHeap);
    duk_put_prop_string(ctx, -2, "minFreeRAM");

    duk_push_uint(ctx, mem.largestFreeBlock);
    duk_put_prop_string(ctx, -2, "maxAllocRAM");

    duk_push_uint(ctx, mem.totalPsram);
    duk_put_prop_string(ctx, -2, "totalPSRAM");

    duk_push_uint(ctx, mem.freePsram);
    duk_put_prop_string(ctx, -2, "freePSRAM");

    // Chip & CPU (frequencia vem do Compat — SystemInfo nao expoe)
    duk_push_uint(ctx, ESP.getCpuFreqMHz());
    duk_put_prop_string(ctx, -2, "cpuFreqMHz");

    duk_push_string(ctx, sys.getChipModel().c_str());
    duk_put_prop_string(ctx, -2, "chipModel");

    duk_push_uint(ctx, chip.cores);
    duk_put_prop_string(ctx, -2, "chipCores");

    duk_push_uint(ctx, chip.revision);
    duk_put_prop_string(ctx, -2, "chipRevision");

    duk_push_uint(ctx, sys.getFlashSize());
    duk_put_prop_string(ctx, -2, "flashSize");

    // Uptime e identidade
    duk_push_uint(ctx, (uint32_t)sys.getUptimeMillis());
    duk_put_prop_string(ctx, -2, "uptimeMs");

    duk_push_string(ctx, sys.getMacAddress().c_str());
    duk_put_prop_string(ctx, -2, "macAddress");

    duk_push_string(ctx, sys.getResetReasonString().c_str());
    duk_put_prop_string(ctx, -2, "resetReason");

    duk_push_string(ctx, sys.getIdfVersion().c_str());
    duk_put_prop_string(ctx, -2, "idfVersion");

    return 1;
}

duk_ret_t JSBindings::js_restart(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    ESP.restart();
    return 0;
}

duk_ret_t JSBindings::js_getTime(duk_context *ctx) {
    duk_push_string(ctx, TimeManager::getFormattedTime().c_str());
    return 1;
}

duk_ret_t JSBindings::js_getSeconds(duk_context *ctx) {
    duk_push_int(ctx, TimeManager::getSeconds());
    return 1;
}

duk_ret_t JSBindings::js_getDate(duk_context *ctx) {
    duk_push_string(ctx, TimeManager::getFormattedDate().c_str());
    return 1;
}

duk_ret_t JSBindings::js_getYear(duk_context *ctx) {
    duk_push_int(ctx, TimeManager::getYear());
    return 1;
}

duk_ret_t JSBindings::js_getMonth(duk_context *ctx) {
    duk_push_int(ctx, TimeManager::getMonth());
    return 1;
}

duk_ret_t JSBindings::js_getDay(duk_context *ctx) {
    duk_push_int(ctx, TimeManager::getDay());
    return 1;
}

duk_ret_t JSBindings::js_getTimezone(duk_context *ctx) {
    duk_push_string(ctx, TimeManager::currentTimezone.c_str());
    return 1;
}

duk_ret_t JSBindings::js_getOSVersion(duk_context *ctx) {
    duk_push_string(ctx, CELEROS_VERSION);
    return 1;
}

duk_ret_t JSBindings::js_getAPILevel(duk_context *ctx) {
    duk_push_int(ctx, CELEROS_API_LEVEL);
    return 1;
}

// UI Bindings=====================================================
// Network Bindings
// =====================================================

duk_ret_t JSBindings::js_getIPAddress(duk_context *ctx) {
    duk_push_string(ctx, WebManager::getIPAddress().c_str());
    return 1;
}

duk_ret_t JSBindings::js_isWiFiActive(duk_context *ctx) {
    duk_push_boolean(ctx, WebManager::isActive());
    return 1;
}

// =====================================================
// Network Bindings - HTTP (objeto Net, API level 2)
// =====================================================

// Custo de memoria: o TLS (https) pede ~45KB de heap durante a chamada,
// concorrendo com o heap do Duktape — respostas grandes podem estourar o
// ~90KB do runtime JS. Limitar payloads a dezenas de KB.
#define NET_MAX_BODY 32768

// Executa GET/POST e devolve o body em "out". Sem WiFi conectado: duk_error
// (o script ve um erro legivel em vez de um null silencioso).
static bool netFetch(duk_context *ctx, bool isPost, std::string &out) {
    JSBindings::present();  // "Carregando..." do app aparece durante a requisicao
    if (!WebManager::isWifiConnected()) {
        duk_error(ctx, DUK_ERR_ERROR, "Net: WiFi is not connected");
        return false;
    }
    const char *url = duk_require_string(ctx, 0);

    std::string body;
    std::string contentType = "text/plain";
    if (isPost) {
        body = duk_require_string(ctx, 1);
        if (duk_is_string(ctx, 2)) contentType = duk_get_string(ctx, 2);
    }

    // Componente Http (esp_http_client): https usa o cert bundle do sistema
    HttpClient http;
    http.setTimeout(10000);
    HttpResponse resp;
    if (isPost) {
        resp = http.post(url, body, contentType);
    } else {
        resp = http.get(url);
    }
    if (!resp.isOk()) return false;
    out = resp.body;
    if (out.length() > NET_MAX_BODY) out.resize(NET_MAX_BODY);
    return true;
}

duk_ret_t JSBindings::js_netGet(duk_context *ctx) {
    std::string body;
    if (!netFetch(ctx, false, body)) { duk_push_null(ctx); return 1; }
    duk_push_string(ctx, body.c_str());
    return 1;
}

duk_ret_t JSBindings::js_netGetJSON(duk_context *ctx) {
    std::string body;
    if (!netFetch(ctx, false, body)) { duk_push_null(ctx); return 1; }
    duk_push_string(ctx, body.c_str());
    duk_json_decode(ctx, -1);  // parse falho vira erro visivel no script
    return 1;
}

duk_ret_t JSBindings::js_netPost(duk_context *ctx) {
    std::string body;
    if (!netFetch(ctx, true, body)) { duk_push_null(ctx); return 1; }
    duk_push_string(ctx, body.c_str());
    return 1;
}

// API 6: download em streaming direto para arquivo — o corpo NAO passa pela
// heap do Duktape (chunk a chunk vai pro FILE*), entao nao sofre o teto de
// 32KB do Net.get. Uso: Net.download(url, path[, onProgress]) -> true|false;
// onProgress(bytes, total) por chunk (total = -1 se o server nao mandou
// Content-Length). O callback roda DENTRO do esp_http_client: duk_pcall para
// um erro de script nao estourar o longjmp no meio do download.
duk_ret_t JSBindings::js_netDownload(duk_context *ctx) {
    JSBindings::present();  // "Carregando..." do app aparece durante a requisicao
    if (!WebManager::isWifiConnected()) {
        duk_error(ctx, DUK_ERR_ERROR, "Net: WiFi is not connected");
    }
    const char *url = duk_require_string(ctx, 0);
    const char *path = duk_require_string(ctx, 1);
    const bool hasProgress = duk_is_function(ctx, 2);

    HttpClient http;
    http.setTimeout(15000);
    http.setBufferSize(4096);  // chunk maior = menos chamadas do callback
    if (hasProgress) {
        http.setProgressCallback([ctx](int64_t got, int64_t total) {
            duk_dup(ctx, 2);  // funcao segue no stack (arg 2 da chamada)
            duk_push_number(ctx, (duk_double_t)got);
            duk_push_number(ctx, (duk_double_t)total);
            if (duk_pcall(ctx, 2) != DUK_EXEC_SUCCESS) duk_pop(ctx);
        });
    }
    HttpResponse resp = http.downloadToFile(url, path);
    if (!resp.isOk()) { duk_push_false(ctx); return 1; }
    duk_push_true(ctx);
    return 1;
}

duk_ret_t JSBindings::js_netIsConnected(duk_context *ctx) {
    duk_push_boolean(ctx, WebManager::isWifiConnected());
    return 1;
}

// =====================================================
// FileSystem Bindings
// =====================================================

duk_ret_t JSBindings::js_readTextFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    std::string content = FileSystem::readTextFile(path);
    if (content.length() == 0 && !FileSystem::exists(path)) {
        duk_push_null(ctx);
    } else {
        duk_push_string(ctx, content.c_str());
    }
    return 1;
}

duk_ret_t JSBindings::js_writeTextFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    const char *content = duk_require_string(ctx, 1);
    bool success = FileSystem::writeTextFile(path, content);
    duk_push_boolean(ctx, success);
    return 1;
}

duk_ret_t JSBindings::js_deleteFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    bool success = FileSystem::deleteFile(path);
    duk_push_boolean(ctx, success);
    return 1;
}

duk_ret_t JSBindings::js_fileExists(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    bool exists = FileSystem::exists(path);
    duk_push_boolean(ctx, exists);
    return 1;
}

duk_ret_t JSBindings::js_listDir(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    std::vector<std::string> files(128);
    int count = FileSystem::listDir(path, files.data(), (int)files.size());
    
    duk_push_array(ctx);
    for (int i = 0; i < count; i++) {
        duk_push_string(ctx, files[i].c_str());
        duk_put_prop_index(ctx, -2, i);
    }
    return 1;
}

duk_ret_t JSBindings::js_appendTextFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    const char *content = duk_require_string(ctx, 1);
    duk_push_boolean(ctx, FileSystem::appendTextFile(path, content));
    return 1;
}

duk_ret_t JSBindings::js_renameFile(duk_context *ctx) {
    const char *pathFrom = duk_require_string(ctx, 0);
    const char *pathTo = duk_require_string(ctx, 1);
    duk_push_boolean(ctx, FileSystem::renameFile(pathFrom, pathTo));
    return 1;
}

duk_ret_t JSBindings::js_mkdir(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, FileSystem::mkdir(path));
    return 1;
}

duk_ret_t JSBindings::js_rmdir(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, FileSystem::rmdir(path));
    return 1;
}

duk_ret_t JSBindings::js_isDirectory(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, FileSystem::isDirectory(path));
    return 1;
}

duk_ret_t JSBindings::js_isFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, FileSystem::isFile(path));
    return 1;
}

duk_ret_t JSBindings::js_getFileSize(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_uint(ctx, FileSystem::getFileSize(path));
    return 1;
}

duk_ret_t JSBindings::js_getTotalSpace(duk_context *ctx) {
    const char *drive = duk_require_string(ctx, 0);
    duk_push_number(ctx, (double)FileSystem::getTotalSpace(drive));  // > 4 GB no SD
    return 1;
}

duk_ret_t JSBindings::js_getUsedSpace(duk_context *ctx) {
    const char *drive = duk_require_string(ctx, 0);
    duk_push_number(ctx, (double)FileSystem::getUsedSpace(drive));  // > 4 GB no SD
    return 1;
}

duk_ret_t JSBindings::js_getFreeSpace(duk_context *ctx) {
    const char *drive = duk_require_string(ctx, 0);
    duk_push_number(ctx, (double)FileSystem::getFreeSpace(drive));  // > 4 GB no SD
    return 1;
}

duk_ret_t JSBindings::js_getFileMD5(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    const char *path = duk_require_string(ctx, 0);
    duk_push_string(ctx, FileSystem::getFileMD5(path).c_str());
    return 1;
}

duk_ret_t JSBindings::js_mountSD(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    duk_push_boolean(ctx, FileSystem::mountSD());
    return 1;
}

duk_ret_t JSBindings::js_unmountSD(duk_context *ctx) {
    FileSystem::unmountSD();
    return 0;
}

// =====================================================
// Keyboard Bindings
// =====================================================

duk_ret_t JSBindings::js_prompt(duk_context *ctx) {
    const char *promptMsg = "";
    if (duk_is_string(ctx, 0)) promptMsg = duk_require_string(ctx, 0);
    
    const char *initialText = "";
    if (duk_is_string(ctx, 1)) initialText = duk_require_string(ctx, 1);

    present();
    std::string result = kui::getString(initialText, promptMsg);
    // o teclado desenhou direto no display: o proximo present repoe o app
    s_frameDirty = true;

    duk_push_string(ctx, result.c_str());

    return 1;
}

// =====================================================
// Keyboard acoplado (API level 5) — sessao NAO-bloqueante
//
// O app abre o teclado (keypadOpen), bombeia com keypadPoll no proprio loop
// e desenha em volta: o teclado vai no MESMO alvo do app (gfx(): sprite do
// app > quadro PSRAM > display) e sobrevive ao present(). Eventos chegam um
// por poll: change (buffer mudou), enter (OK: texto em ev.text) e cancel
// (X — encerra a sessao sozinho). O canto de saida do OS segue valendo.
// =====================================================

static kui::KeyboardScreen *s_kb = nullptr;
static kui::TouchPump s_kbPump;
static uint32_t s_kbLastPollMs = 0;
enum KbEvent { KB_EV_NONE = 0, KB_EV_CHANGE, KB_EV_ENTER, KB_EV_CANCEL };
static int s_kbEvent = KB_EV_NONE;
static std::string s_kbEnterText;

static void keypadCloseSession() {
    if (s_kb) {
        delete s_kb;
        s_kb = nullptr;
    }
    s_kbEvent = KB_EV_NONE;
    s_kbEnterText.clear();
    s_frameDirty = true;  // proximo present restaura o frame do app
}

duk_ret_t JSBindings::js_keypadOpen(duk_context *ctx) {
    if (!tftInstance) {
        duk_push_false(ctx);
        return 1;
    }
    if (s_kb) {  // so uma sessao por vez
        duk_push_false(ctx);
        return 1;
    }

    std::string title, initial;
    int maxLen = 64;
    bool field = true;
    if (duk_is_object(ctx, 0)) {
        if (duk_get_prop_string(ctx, 0, "title") && duk_is_string(ctx, -1)) title = duk_get_string(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "initial") && duk_is_string(ctx, -1)) initial = duk_get_string(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "maxLen") && duk_is_number(ctx, -1)) maxLen = duk_get_int(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "field") && duk_is_boolean(ctx, -1)) field = duk_get_boolean(ctx, -1) != 0;
        duk_pop(ctx);
    }
    if (maxLen < 1) maxLen = 1;

    present();
    s_kb = new kui::KeyboardScreen(title, initial, maxLen);
    s_kb->setPersistent(true);
    s_kb->setShowField(field);
    s_kb->onChange = [] { s_kbEvent = KB_EV_CHANGE; };
    s_kb->onEnter = [](const std::string& t) {
        s_kbEnterText = t;
        s_kbEvent = KB_EV_ENTER;
    };
    s_kb->onResult = [](const std::string&, bool ok) {
        if (!ok) s_kbEvent = KB_EV_CANCEL;  // X (so existe com field)
    };
    s_kbEvent = KB_EV_NONE;
    s_kbEnterText.clear();
    s_kbLastPollMs = millis();

    {
        kui::Canvas c(*tftInstance, JSBindings::gfx());
        s_kb->draw(c);
    }
    duk_push_boolean(ctx, true);
    return 1;
}

duk_ret_t JSBindings::js_keypadPoll(duk_context *ctx) {
    if (!s_kb || !tftInstance) {
        duk_push_null(ctx);
        return 1;
    }
    present();

    uint16_t tx = 0, ty = 0;
    // pollAppChrome SEMPRE roda: o disparo da saida e no release (readTouch
    // false) — dentro de um if(readTouch) o release nunca seria visto e o X
    // acendia sem sair (bug do Terminal)
    bool touched = kui::readTouch(&tx, &ty);
    if (pollAppChrome(touched, tx, ty)) {
        keypadCloseSession();
        duk_error(ctx, DUK_ERR_ERROR, "OS_EXIT");
        return 0;
    }

    s_kbEvent = KB_EV_NONE;
    s_kbPump.poll([](const kui::TouchEvent& ev) {
        s_kb->onTouch(ev);
        if (ev.type != kui::TouchEvent::Drag) s_kb->markDirty();  // tecla "afunda"
    });
    uint32_t now = millis();
    uint32_t dt = now - s_kbLastPollMs;
    if (dt > 100) dt = 100;
    s_kbLastPollMs = now;
    s_kb->onTick(dt);
    if (s_kb->consumeDirty()) {
        kui::Canvas c(*tftInstance, JSBindings::gfx());
        s_kb->draw(c);
    }

    if (s_kbEvent == KB_EV_NONE) {
        duk_push_null(ctx);
        return 1;
    }

    duk_push_object(ctx);
    if (s_kbEvent == KB_EV_ENTER) {
        duk_push_string(ctx, "enter");
        duk_put_prop_string(ctx, -2, "type");
        duk_push_string(ctx, s_kbEnterText.c_str());
        duk_put_prop_string(ctx, -2, "text");
        s_kbEnterText.clear();
    } else if (s_kbEvent == KB_EV_CHANGE) {
        duk_push_string(ctx, "change");
        duk_put_prop_string(ctx, -2, "type");
    } else {  // cancel: o X encerrou a sessao
        duk_push_string(ctx, "cancel");
        duk_put_prop_string(ctx, -2, "type");
        keypadCloseSession();
    }
    return 1;
}

duk_ret_t JSBindings::js_keypadText(duk_context *ctx) {
    duk_push_string(ctx, s_kb ? s_kb->text().c_str() : "");
    return 1;
}

duk_ret_t JSBindings::js_keypadRect(duk_context *ctx) {
    // Area das teclas no espaco virtual 240x320; fechado: faixa nula no rodape.
    // O espaco do app comeca abaixo da topbar do sistema.
    int topV = 320;
    if (s_kb && tftInstance) {
        int offY = s_topbarFixed ? UI::topbarH() : 0;
        int appH = tftInstance->height() - offY;
        if (appH < 1) appH = 1;
        topV = (int)(((long)s_kb->keysTop() - offY) * 320 / appH);
        if (topV < 0) topV = 0;
        if (topV > 320) topV = 320;
    }
    duk_push_object(ctx);
    duk_push_int(ctx, 0);
    duk_put_prop_string(ctx, -2, "x");
    duk_push_int(ctx, topV);
    duk_put_prop_string(ctx, -2, "y");
    duk_push_int(ctx, 240);
    duk_put_prop_string(ctx, -2, "w");
    duk_push_int(ctx, 320 - topV);
    duk_put_prop_string(ctx, -2, "h");
    return 1;
}

duk_ret_t JSBindings::js_keypadDraw(duk_context *ctx) {
    // App redesenhou a tela: repoe o teclado por cima (mesmo alvo do app)
    if (s_kb && tftInstance) {
        kui::Canvas c(*tftInstance, JSBindings::gfx());
        s_kb->draw(c);
    }
    return 0;
}

duk_ret_t JSBindings::js_keypadClose(duk_context *ctx) {
    keypadCloseSession();
    return 0;
}

// =====================================================
// Topbar custom (API level 6) — texto e chips da faixa
// =====================================================

duk_ret_t JSBindings::js_topbarText(duk_context *ctx) {
    const char *t = duk_require_string(ctx, 0);
    s_tbText = t ? t : "";
    s_tbTextCustom = (s_tbText[0] != '\0');   // "" volta ao nome do app
    s_tbDirty = true;
    return 0;
}

duk_ret_t JSBindings::js_topbarButtons(duk_context *ctx) {
    if (!tftInstance) return 0;
    s_tbButtons.clear();
    s_tbHotBtn = -1;
    s_tbBtnArmed = 0;
    s_tbDirty = true;
    if (!duk_is_array(ctx, 0)) {
        duk_push_int(ctx, 0);
        return 1;
    }
    int n = (int)duk_get_length(ctx, 0);
    if (n > 3) n = 3;                          // espaco nao comporta mais que isso
    int cursor = 240 - 40 - 4;                 // borda direita disponivel (antes do X)
    for (int i = 0; i < n; i++) {
        if (!duk_get_prop_index(ctx, 0, (duk_uarridx_t)i) || !duk_is_string(ctx, -1)) {
            duk_pop(ctx);
            continue;
        }
        const char *lb = duk_get_string(ctx, -1);
        duk_pop(ctx);
        if (!lb || !lb[0]) continue;
        int pw = tftInstance->textWidth(lb, kui::type::caption());
        int vw = (int)((long)pw * 240 / tftInstance->width()) + 12;   // padding virtual
        if (vw < 28) vw = 28;
        if (cursor - vw < 60) break;           // sem espaco: reserva o titulo
        TbButton b;
        b.label = lb;
        b.w = vw;
        b.x = cursor - vw;
        cursor = b.x - 6;
        s_tbButtons.push_back(b);
    }
    duk_push_int(ctx, (int)s_tbButtons.size());   // quantos couberam
    return 1;
}

duk_ret_t JSBindings::js_topbarPop(duk_context *ctx) {
    if (s_tbTaps.empty()) {
        duk_push_null(ctx);
        return 1;
    }
    duk_push_string(ctx, s_tbTaps.front().c_str());
    s_tbTaps.erase(s_tbTaps.begin());
    return 1;
}

duk_ret_t JSBindings::js_textWidth(duk_context *ctx) {
    if (!tftInstance) { duk_push_int(ctx, 0); return 1; }
    const char* str = duk_require_string(ctx, 0);
    int font = duk_get_int_default(ctx, 1, 2);
    int w = tftInstance->textWidth(str, UI::font(font));
    // devolve no espaco virtual 240x320 (inverso do jsx())
    duk_push_int(ctx, (int)((long)w * 240 / tftInstance->width()));
    return 1;
}

duk_ret_t JSBindings::js_fontHeight(duk_context *ctx) {
    // Altura da fonte no canvas virtual (para centralizar texto de verdade:
    // as fontes proporcionais nao tem a altura fixa das numericas antigas)
    if (!tftInstance) { duk_push_int(ctx, 0); return 1; }
    int font = duk_get_int_default(ctx, 0, 2);
    int h = tftInstance->fontHeight(CelerFont(UI::font(font)));
    duk_push_int(ctx, (int)(h / appScaleY()));
    return 1;
}

duk_ret_t JSBindings::js_theme(duk_context *ctx) {
    // Cores do tema do OS ja em RGB565 (espaco de cor do JS) — os apps de
    // sistema herdam a identidade visual do Kui em qualquer placa.
    auto put = [&](const char* k, uint32_t rgb888) {
        uint32_t r = (rgb888 >> 16) & 0xFF, g = (rgb888 >> 8) & 0xFF, b = rgb888 & 0xFF;
        duk_push_uint(ctx, (duk_uint_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
        duk_put_prop_string(ctx, -2, k);
    };
    duk_push_object(ctx);
    put("bg", THEME_BG);
    put("card", THEME_CARD);
    put("raised", THEME_RAISED);
    put("stroke", THEME_STROKE);
    put("accent", THEME_ACCENT);
    put("accentD", THEME_ACCENT_D);
    put("onAccent", THEME_ON_ACCENT);
    put("text", THEME_TEXT);
    put("textDim", THEME_TEXT_DIM);
    put("ok", THEME_OK);
    put("warn", THEME_WARN);
    put("err", THEME_ERR);
    return 1;
}

duk_ret_t JSBindings::js_drawIcon(duk_context *ctx) {
    if (!tftInstance) return 0;
    const char* name = duk_require_string(ctx, 0);
    int x = duk_require_int(ctx, 1);
    int y = duk_require_int(ctx, 2);
    Icon::draw(gfx(), name, jsx(x), jsy(y));
    return 0;
}

duk_ret_t JSBindings::js_drawPNG(duk_context *ctx) {
    // Desenho streaming (linha a linha via pngle do LovyanGFX): nao aloca
    // framebuffer da imagem inteira, so a janela do deflate (~44 KB durante
    // o decode). Alpha do PNG e composto sobre o fundo pelo proprio LGFX.
    if (!tftInstance) return 0;
    const char *path = duk_require_string(ctx, 0);
    int x = duk_require_int(ctx, 1);
    int y = duk_require_int(ctx, 2);
    if (!imagePathOk(path)) {
        duk_push_boolean(ctx, 0);
        return 1;
    }
    CelerFileWrapper file;
    duk_push_boolean(ctx, gfx()->drawPngFile(&file, path, jsx(x), jsy(y), 0, 0, 0, 0,
                                            (float)UI::W / 240.0f,
                                            appScaleY()));
    return 1;
}
duk_ret_t JSBindings::js_copyFile(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    duk_push_boolean(ctx, FileSystem::copyFile(duk_require_string(ctx, 0),
                                               duk_require_string(ctx, 1)) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_copyDirectory(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    duk_push_boolean(ctx, FileSystem::copyDirectory(duk_require_string(ctx, 0),
                                                    duk_require_string(ctx, 1)) ? 1 : 0);
    return 1;
}

// Remocao recursiva (app = pasta com app.json/main.js/icon.bin...). rmdir so
// aceita pasta vazia; sem isso o Settings nao conseguiria desinstalar apps.
static bool removeTree(const std::string& dir) {
    FileEntry entries[50];
    int n = FileSystem::listDirectory(dir.c_str(), entries, 50);
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        if (entries[i].isDir) {
            if (!removeTree(entries[i].path)) return false;
        } else if (!FileSystem::deleteFile(entries[i].path.c_str())) {
            return false;
        }
    }
    return FileSystem::rmdir(dir.c_str());
}

duk_ret_t JSBindings::js_removeDirectory(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    duk_push_boolean(ctx, removeTree(duk_require_string(ctx, 0)) ? 1 : 0);
    return 1;
}

// =====================================================
// System nivel 3 — suporte aos apps de sistema em JS (W8)
// =====================================================

duk_ret_t JSBindings::js_setBrightness(duk_context *ctx) {
    Backlight::set(duk_require_int(ctx, 0));
    return 0;
}

duk_ret_t JSBindings::js_getBrightness(duk_context *ctx) {
    duk_push_int(ctx, Backlight::get());
    return 1;
}

duk_ret_t JSBindings::js_backlightSupported(duk_context *ctx) {
    duk_push_boolean(ctx, Backlight::isSupported() ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_openWifiSetup(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    // Empilha a tela nativa de WiFi. Como o app JS roda sincrono, a tela
    // so entra em cena quando o script devolver o controle ao loop do Kui
    // (o app deve chamar System.exitApp() logo em seguida).
    kui::Navigator::push(WifiSetupScreen::instance());
    return 0;
}

duk_ret_t JSBindings::js_setClip(duk_context *ctx) {
    // Recorte no canvas virtual: desenho fora de (x,y,w,h) e descartado
    gfx()->setClipRect(jsx(duk_require_int(ctx, 0)), jsy(duk_require_int(ctx, 1)),
                       jsx(duk_require_int(ctx, 2)), jsH(duk_require_int(ctx, 3)));
    return 0;
}

duk_ret_t JSBindings::js_clearClip(duk_context *ctx) {
    (void)ctx;
    gfx()->clearClipRect();
    return 0;
}

duk_ret_t JSBindings::js_present(duk_context *ctx) {
    // Apresentacao explicita do quadro (animacoes/loops sem delay/getTouch)
    (void)ctx;
    present();
    return 0;
}

duk_ret_t JSBindings::js_isBuffered(duk_context *ctx) {
    duk_push_boolean(ctx, s_frame != nullptr ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_exitApp(duk_context *ctx) {
    // Mesmo protocolo do canto superior direito: erro "OS_EXIT" e
    // interceptado como saida limpa pelo CelerKernel.
    duk_error(ctx, DUK_ERR_ERROR, "OS_EXIT");
    return 0;  // unreachable
}

duk_ret_t JSBindings::js_wifiStatus(duk_context *ctx) {
    duk_push_object(ctx);
    duk_push_boolean(ctx, WebManager::isActive() ? 1 : 0);
    duk_put_prop_string(ctx, -2, "connected");
    duk_push_string(ctx, WebManager::getIPAddress().c_str());
    duk_put_prop_string(ctx, -2, "ip");
    duk_push_boolean(ctx, WebManager::isServerRunning() ? 1 : 0);
    duk_put_prop_string(ctx, -2, "webServer");
    duk_push_boolean(ctx, WebManager::hasSavedNetworks() ? 1 : 0);
    duk_put_prop_string(ctx, -2, "savedNetworks");
    return 1;
}

duk_ret_t JSBindings::js_md5(duk_context *ctx) {
    // Mesmo formato do FileSystem::getFileMD5 (hex minusculo) — o PIN do
    // Settings JS tem que bater com o arquivo legado settings_pin.txt.
    const char* s = duk_require_string(ctx, 0);
    md5_context_t c;
    esp_rom_md5_init(&c);
    esp_rom_md5_update(&c, s, (uint32_t)strlen(s));
    uint8_t hash[16];
    esp_rom_md5_final(hash, &c);

    char hex[33];
    for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", hash[i]);
    hex[32] = 0;
    duk_push_string(ctx, hex);
    return 1;
}

// ---- PIN do Settings (nativo, com salt) -----------------------------------
// Substitui o fluxo JS antigo (md5 em settings_pin.txt): qualquer app podia
// ler o hash e apagar o arquivo para destravar o Settings. O estado mora no
// nativo (PinStore: settings_pin2.bin + flag NVS) e nao ha hash exposto.

duk_ret_t JSBindings::js_setPin(duk_context *ctx) {
    const char* pin = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, PinStore::set(pin) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_verifyPin(duk_context *ctx) {
    const char* pin = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, PinStore::verify(pin) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_pinClear(duk_context *ctx) {
    PinStore::clear();
    return 0;
}

duk_ret_t JSBindings::js_pinState(duk_context *ctx) {
    // 0 = sem PIN, 1 = ativo, 2 = corrompido (flag NVS sem arquivo)
    duk_push_int(ctx, PinStore::state());
    return 1;
}

// ---- senha do Web Server ---------------------------------------------------

duk_ret_t JSBindings::js_webAuthInfo(duk_context *ctx) {
    duk_push_object(ctx);
    duk_push_string(ctx, "admin");
    duk_put_prop_string(ctx, -2, "user");
    duk_push_string(ctx, WebAuth::password());
    duk_put_prop_string(ctx, -2, "pass");
    return 1;
}

duk_ret_t JSBindings::js_webAuthSetPass(duk_context *ctx) {
    const char* p = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, WebAuth::setPassword(p) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_rescanApps(duk_context *ctx) {
    LauncherUI::requestRescan();
    return 0;
}

duk_ret_t JSBindings::js_factoryReset(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    const char* mode = duk_is_string(ctx, 0) ? duk_require_string(ctx, 0) : "configs";

    if (strcmp(mode, "total") == 0) {
        // LittleFS inteiro (apps + icones + configs). Recuperacao exige
        // reflashe de data/ (tools/flash_data.sh) ou celerctl apps install.
        WebManager::forgetAllNetworks();
        FileSystem::formatLittleFS();
        PinStore::clear();     // flag NVS nao vive no LittleFS
        WebAuth::regenerate();
        duk_push_boolean(ctx, 1);
        return 1;
    }

    // "configs": zera configuracoes e credenciais, preserva apps/icones
    const char* cfgFiles[] = {
        "/local/brightness.txt", "/local/settings_pin.txt", "/local/nowifi.txt",
        "/local/web_on.txt", "/local/config_install_sd.txt", "/local/ota_url.txt",
        "/local/config_time.txt", "/local/touch_cal_p.bin", "/local/wifi.txt",
        "/local/settings_pin2.bin", "/local/ota_allow_http.txt",
    };
    for (const char* f : cfgFiles) FileSystem::deleteFile(f);
    PinStore::clear();        // limpa tambem a flag NVS do PIN
    WebAuth::regenerate();    // senha web nova (a antiga era "config")
    WebManager::forgetAllNetworks();
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_otaCheck(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    OtaManager::checkForUpdates();
    const OtaUpdateInfo& info = OtaManager::info;
    duk_push_object(ctx);
    duk_push_boolean(ctx, info.fetchFailed ? 1 : 0);
    duk_put_prop_string(ctx, -2, "fetchFailed");
    duk_push_boolean(ctx, info.available ? 1 : 0);
    duk_put_prop_string(ctx, -2, "available");
    duk_push_boolean(ctx, info.hasFirmware ? 1 : 0);
    duk_put_prop_string(ctx, -2, "hasFirmware");
    duk_push_string(ctx, info.version.c_str());
    duk_put_prop_string(ctx, -2, "version");
    duk_push_string(ctx, info.firmwareUrl.c_str());
    duk_put_prop_string(ctx, -2, "url");
    duk_push_string(ctx, info.changelog.c_str());
    duk_put_prop_string(ctx, -2, "changelog");
    duk_push_string(ctx, info.guide.c_str());
    duk_put_prop_string(ctx, -2, "guide");
    duk_push_string(ctx, info.type.c_str());
    duk_put_prop_string(ctx, -2, "type");
    return 1;
}

// performUpdate roda na mesma task do script e invoca onProgress de dentro
// do loop de flash: reentrar no Duktape pelo stash e seguro.
static duk_context* s_otaCtx = nullptr;
static void otaProgressTrampoline(int percent) {
    if (!s_otaCtx) return;
    duk_push_global_stash(s_otaCtx);
    duk_get_prop_string(s_otaCtx, -1, "_otaCb");
    if (duk_is_function(s_otaCtx, -1)) {
        duk_push_int(s_otaCtx, percent);
        duk_pcall(s_otaCtx, 1);
    }
    duk_pop_2(s_otaCtx);
    JSBindings::present();
}

duk_ret_t JSBindings::js_otaStart(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    const char* url = duk_require_string(ctx, 0);
    bool hasCb = duk_is_function(ctx, 1) ? true : false;

    if (hasCb) {
        duk_push_global_stash(ctx);
        duk_dup(ctx, 1);
        duk_put_prop_string(ctx, -2, "_otaCb");
        duk_pop(ctx);
        s_otaCtx = ctx;
    }

    bool ok = OtaManager::performUpdate(url, hasCb ? otaProgressTrampoline : nullptr);

    if (hasCb) {
        duk_push_global_stash(ctx);
        duk_push_undefined(ctx);
        duk_put_prop_string(ctx, -2, "_otaCb");
        duk_pop(ctx);
        s_otaCtx = nullptr;
    }

    duk_push_object(ctx);
    duk_push_boolean(ctx, ok ? 1 : 0);
    duk_put_prop_string(ctx, -2, "ok");
    if (!ok) {
        duk_push_string(ctx, OtaManager::lastError.c_str());
        duk_put_prop_string(ctx, -2, "error");
    }
    return 1;
}

duk_ret_t JSBindings::js_setTimezone(duk_context *ctx) {
    TimeManager::setTimezone(duk_require_string(ctx, 0));
    return 0;
}

duk_ret_t JSBindings::js_setManualTime(duk_context *ctx) {
    TimeManager::setManualTime(duk_require_int(ctx, 0), duk_require_int(ctx, 1),
                               duk_require_int(ctx, 2), duk_require_int(ctx, 3),
                               duk_require_int(ctx, 4));
    return 0;
}

duk_ret_t JSBindings::js_set24hFormat(duk_context *ctx) {
    TimeManager::setTimeFormat(duk_require_boolean(ctx, 0) ? true : false);
    return 0;
}

duk_ret_t JSBindings::js_get24hFormat(duk_context *ctx) {
    duk_push_boolean(ctx, TimeManager::use24hFormat ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_setNtpEnabled(duk_context *ctx) {
    TimeManager::setNTPEnabled(duk_require_boolean(ctx, 0) ? true : false);
    return 0;
}

duk_ret_t JSBindings::js_getNtpEnabled(duk_context *ctx) {
    duk_push_boolean(ctx, TimeManager::ntpEnabled ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_webActive(duk_context *ctx) {
    duk_push_boolean(ctx, WebManager::isServerRunning() ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_webSetActive(duk_context *ctx) {
    // Toggle do servidor web (persiste em web_on.txt e age ao vivo), igual a
    // tela C++ original — mas sem reboot. NAO desliga o WiFi (isso e o
    // nowifi.txt / WebManager::disable).
    if (duk_require_boolean(ctx, 0)) {
        FileSystem::writeTextFile("/local/web_on.txt", "1");
        if (WebManager::isActive()) WebManager::startWebServerIfNeeded();
    } else {
        FileSystem::deleteFile("/local/web_on.txt");
        WebManager::stopWebServer();
    }
    return 0;
}

// --- Net nivel 3 (WiFi) ---

duk_ret_t JSBindings::js_wifiScan(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    // Scan bloqueante (~2s) — mesmo comportamento da tela nativa.
    CelerScanEntry entries[20];
    int n = WebManager::scanNetworks(entries, 20);

    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        duk_push_object(ctx);
        duk_push_string(ctx, entries[i].ssid.c_str());
        duk_put_prop_string(ctx, -2, "ssid");
        duk_push_int(ctx, entries[i].rssi);
        duk_put_prop_string(ctx, -2, "rssi");
        duk_push_boolean(ctx, entries[i].secure ? 1 : 0);
        duk_put_prop_string(ctx, -2, "secure");
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

duk_ret_t JSBindings::js_wifiConnect(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    const char* ssid = duk_require_string(ctx, 0);
    const char* pass = duk_is_string(ctx, 1) ? duk_require_string(ctx, 1) : "";
    duk_push_boolean(ctx, WebManager::connect(ssid, pass) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_wifiDisconnect(duk_context *ctx) {
    WebManager::disconnect();
    return 0;
}

// =====================================================
// Init - Register ALL bindings
// =====================================================

void JSBindings::init(duk_context *ctx, CelerDisplay *tft, const char* appTitle,
                      bool topbarFixed) {
    s_jsTft = tft;
    tftInstance = tft;
    s_appTitle = appTitle ? appTitle : "";
    s_topbarFixed = topbarFixed;
    celer_log_printf("[TB] init app='%s' fixed=%d\n", s_appTitle ? s_appTitle : "", topbarFixed);

    // Estado grafico limpo por app: o sprite de um app anterior (que saiu
    // sem deleteSprite) vazava e ainda capturava o desenho do proximo.
    if (tftSprite) {
        tftSprite->deleteSprite();
        delete tftSprite;
        tftSprite = nullptr;
    }
    useSprite = false;

    // Sessao de teclado acoplado de um app anterior (saiu sem keypadClose)
    keypadCloseSession();

    // Topbar limpa: sem faixa/hot/gesto/conteudo custom herdados do app anterior
    s_exitArmed = false;
    s_barOnGlass = false;
    s_barHotOnGlass = false;
    s_barShown = false;
    s_tbSwipe = false;
    s_tbButtons.clear();
    s_tbTaps.clear();
    s_tbText.clear();
    s_tbTextCustom = false;
    s_tbHotBtn = -1;
    s_tbBtnArmed = 0;
    s_tbDirty = false;
    s_tbHotOnGlass = -2;

    // Quadro automatico: alocado uma vez (PSRAM) e reaproveitado entre apps
    if (s_frame == nullptr && Board::profile().hasPsram) {
        s_frame = new CelerSprite(tft);
        s_frame->setPsram(true);
        s_frame->setColorDepth(16);
        s_frame->setSwapBytes(true);  // mesma convencao do display (icones, readRect)
        if (s_frame->createSprite(tft->width(), tft->height()) == nullptr) {
            delete s_frame;
            s_frame = nullptr;
        }
    }
    if (s_frame != nullptr) {
        s_frame->fillScreen(TFT_BLACK);  // mesmo fundo que o launcher pinta antes do app
        s_frame->setTextColor(TFT_WHITE);
        s_frame->setTextSize(1);
        s_frame->setTextDatum(TL_DATUM);
        s_frame->clearClipRect();
    }
    tft->clearClipRect();
    s_frameDirty = false;
    tft->setTextSize(1);

    // --- System Object ---
    duk_push_global_object(ctx);
    duk_push_object(ctx); // System

    // System.gpio sub-object
    duk_push_object(ctx);
    duk_push_c_function(ctx, js_pinMode, 2);
    duk_put_prop_string(ctx, -2, "pinMode");
    duk_push_c_function(ctx, js_digitalWrite, 2);
    duk_put_prop_string(ctx, -2, "digitalWrite");
    duk_push_c_function(ctx, js_digitalRead, 1);
    duk_put_prop_string(ctx, -2, "digitalRead");
    duk_push_c_function(ctx, js_analogRead, 1);
    duk_put_prop_string(ctx, -2, "analogRead");
    duk_push_c_function(ctx, js_analogWrite, 2);
    duk_put_prop_string(ctx, -2, "analogWrite");
    duk_push_c_function(ctx, js_pulseIn, 3); // max 3 args
    duk_put_prop_string(ctx, -2, "pulseIn");
    
    // GPIO Constants
    duk_push_int(ctx, OUTPUT); duk_put_prop_string(ctx, -2, "OUTPUT");
    duk_push_int(ctx, INPUT); duk_put_prop_string(ctx, -2, "INPUT");
    duk_push_int(ctx, INPUT_PULLUP); duk_put_prop_string(ctx, -2, "INPUT_PULLUP");
    duk_push_int(ctx, HIGH); duk_put_prop_string(ctx, -2, "HIGH");
    duk_push_int(ctx, LOW); duk_put_prop_string(ctx, -2, "LOW");
    
    duk_put_prop_string(ctx, -2, "gpio");

    // --- Drawing Primitives ---

    duk_push_c_function(ctx, js_createSprite, 2);
    duk_put_prop_string(ctx, -2, "createSprite");
    duk_push_c_function(ctx, js_deleteSprite, 0);
    duk_put_prop_string(ctx, -2, "deleteSprite");
    duk_push_c_function(ctx, js_pushSprite, 2);
    duk_put_prop_string(ctx, -2, "pushSprite");
    duk_push_c_function(ctx, js_bindSprite, 1);
    duk_put_prop_string(ctx, -2, "bindSprite");
    duk_push_c_function(ctx, js_drawFastVLine, 4);
    duk_put_prop_string(ctx, -2, "drawFastVLine");
    duk_push_c_function(ctx, js_drawFastHLine, 4);
    duk_put_prop_string(ctx, -2, "drawFastHLine");

    duk_push_c_function(ctx, js_fillScreen, 1);
    duk_put_prop_string(ctx, -2, "fillScreen");
    duk_push_c_function(ctx, js_fillRect, 5);
    duk_put_prop_string(ctx, -2, "fillRect");
    duk_push_c_function(ctx, js_drawRect, 5);
    duk_put_prop_string(ctx, -2, "drawRect");
    duk_push_c_function(ctx, js_drawLine, 5);
    duk_put_prop_string(ctx, -2, "drawLine");
    duk_push_c_function(ctx, js_drawPixel, 3);
    duk_put_prop_string(ctx, -2, "drawPixel");
    duk_push_c_function(ctx, js_drawCircle, 4);
    duk_put_prop_string(ctx, -2, "drawCircle");
    duk_push_c_function(ctx, js_fillCircle, 4);
    duk_put_prop_string(ctx, -2, "fillCircle");
    duk_push_c_function(ctx, js_drawTriangle, 7);
    duk_put_prop_string(ctx, -2, "drawTriangle");
    duk_push_c_function(ctx, js_fillTriangle, 7);
    duk_put_prop_string(ctx, -2, "fillTriangle");
    duk_push_c_function(ctx, js_drawRoundRect, 6);
    duk_put_prop_string(ctx, -2, "drawRoundRect");
    duk_push_c_function(ctx, js_fillRoundRect, 6);
    duk_put_prop_string(ctx, -2, "fillRoundRect");
    
    duk_push_c_function(ctx, js_drawBMP, 3);
    duk_put_prop_string(ctx, -2, "drawBMP");
    duk_push_c_function(ctx, js_drawPNG, 3);
    duk_put_prop_string(ctx, -2, "drawPNG");

    // --- Text ---
    duk_push_c_function(ctx, js_drawString, 4);
    duk_put_prop_string(ctx, -2, "drawString");
    duk_push_c_function(ctx, js_setTextColor, 2);
    duk_put_prop_string(ctx, -2, "setTextColor");
    duk_push_c_function(ctx, js_setTextSize, 1);
    duk_put_prop_string(ctx, -2, "setTextSize");
    duk_push_c_function(ctx, js_textWidth, 2);
    duk_put_prop_string(ctx, -2, "textWidth");
    duk_push_c_function(ctx, js_fontHeight, 1);
    duk_put_prop_string(ctx, -2, "fontHeight");

    // --- Utility ---
    duk_push_c_function(ctx, js_color, 3);
    duk_put_prop_string(ctx, -2, "color");
    duk_push_c_function(ctx, js_screenWidth, 0);
    duk_put_prop_string(ctx, -2, "screenWidth");
    duk_push_c_function(ctx, js_screenHeight, 0);
    duk_put_prop_string(ctx, -2, "screenHeight");

    // --- Touch Input ---
    duk_push_c_function(ctx, js_getTouch, 0);
    duk_put_prop_string(ctx, -2, "getTouch");
    duk_push_c_function(ctx, js_millis, 0);
    duk_put_prop_string(ctx, -2, "millis");
    duk_push_c_function(ctx, js_micros, 0);
    duk_put_prop_string(ctx, -2, "micros");
    duk_push_c_function(ctx, js_delay, 1);
    duk_put_prop_string(ctx, -2, "delay");
    duk_push_c_function(ctx, js_delayMicroseconds, 1);
    duk_put_prop_string(ctx, -2, "delayMicroseconds");
    duk_push_c_function(ctx, js_print, 1);
    duk_put_prop_string(ctx, -2, "print");
    duk_push_c_function(ctx, js_getTemperature, 0);
    duk_put_prop_string(ctx, -2, "getTemperature");
    duk_push_c_function(ctx, js_hasTemperatureSensor, 0);
    duk_put_prop_string(ctx, -2, "hasTemperatureSensor");
    duk_push_c_function(ctx, js_getInfo, 0);
    duk_put_prop_string(ctx, -2, "getInfo");
    duk_push_c_function(ctx, js_restart, 0);
    duk_put_prop_string(ctx, -2, "restart");
    
    duk_push_c_function(ctx, js_getTime, 0);
    duk_put_prop_string(ctx, -2, "getTime");
    duk_push_c_function(ctx, js_getSeconds, 0);
    duk_put_prop_string(ctx, -2, "getSeconds");
    duk_push_c_function(ctx, js_getDate, 0);
    duk_put_prop_string(ctx, -2, "getDate");
    duk_push_c_function(ctx, js_getYear, 0);
    duk_put_prop_string(ctx, -2, "getYear");
    duk_push_c_function(ctx, js_getMonth, 0);
    duk_put_prop_string(ctx, -2, "getMonth");
    duk_push_c_function(ctx, js_getDay, 0);
    duk_put_prop_string(ctx, -2, "getDay");
    duk_push_c_function(ctx, js_getTimezone, 0);
    duk_put_prop_string(ctx, -2, "getTimezone");

    duk_push_c_function(ctx, js_getOSVersion, 0);
    duk_put_prop_string(ctx, -2, "getOSVersion");

    duk_push_c_function(ctx, js_getAPILevel, 0);
    duk_put_prop_string(ctx, -2, "getAPILevel");

    duk_push_c_function(ctx, js_getIPAddress, 0);
    duk_put_prop_string(ctx, -2, "getIPAddress");

    duk_push_c_function(ctx, js_isWiFiActive, 0);
    duk_put_prop_string(ctx, -2, "isWiFiActive");

    // --- Keyboard ---
    duk_push_c_function(ctx, js_prompt, 2);
    duk_put_prop_string(ctx, -2, "prompt");

    // --- Keyboard acoplado (API level 5): sessao nao-bloqueante p/ apps ---
    duk_push_c_function(ctx, js_keypadOpen, 1);
    duk_put_prop_string(ctx, -2, "keypadOpen");
    duk_push_c_function(ctx, js_keypadPoll, 0);
    duk_put_prop_string(ctx, -2, "keypadPoll");
    duk_push_c_function(ctx, js_keypadText, 0);
    duk_put_prop_string(ctx, -2, "keypadText");
    duk_push_c_function(ctx, js_keypadRect, 0);
    duk_put_prop_string(ctx, -2, "keypadRect");
    duk_push_c_function(ctx, js_keypadDraw, 0);
    duk_put_prop_string(ctx, -2, "keypadDraw");
    duk_push_c_function(ctx, js_keypadClose, 0);
    duk_put_prop_string(ctx, -2, "keypadClose");

    // --- Topbar custom (API level 6): texto e chips da faixa ---
    duk_push_c_function(ctx, js_topbarText, 1);
    duk_put_prop_string(ctx, -2, "topbarText");
    duk_push_c_function(ctx, js_topbarButtons, 1);
    duk_put_prop_string(ctx, -2, "topbarButtons");
    duk_push_c_function(ctx, js_topbarPop, 0);
    duk_put_prop_string(ctx, -2, "topbarPop");

    // --- System nivel 3 (apps de sistema em JS) ---
    duk_push_c_function(ctx, js_setBrightness, 1);
    duk_put_prop_string(ctx, -2, "setBrightness");
    duk_push_c_function(ctx, js_getBrightness, 0);
    duk_put_prop_string(ctx, -2, "getBrightness");
    duk_push_c_function(ctx, js_backlightSupported, 0);
    duk_put_prop_string(ctx, -2, "backlightSupported");
    duk_push_c_function(ctx, js_openWifiSetup, 0);
    duk_put_prop_string(ctx, -2, "openWifiSetup");
    duk_push_c_function(ctx, js_exitApp, 0);
    duk_put_prop_string(ctx, -2, "exitApp");
    duk_push_c_function(ctx, js_setClip, 4);
    duk_put_prop_string(ctx, -2, "setClip");
    duk_push_c_function(ctx, js_clearClip, 0);
    duk_put_prop_string(ctx, -2, "clearClip");
    duk_push_c_function(ctx, js_present, 0);
    duk_put_prop_string(ctx, -2, "present");
    duk_push_c_function(ctx, js_isBuffered, 0);
    duk_put_prop_string(ctx, -2, "isBuffered");
    duk_push_c_function(ctx, js_wifiStatus, 0);
    duk_put_prop_string(ctx, -2, "wifiStatus");
    duk_push_c_function(ctx, js_md5, 1);
    duk_put_prop_string(ctx, -2, "md5");
    duk_push_c_function(ctx, js_setPin, 1);
    duk_put_prop_string(ctx, -2, "setPin");
    duk_push_c_function(ctx, js_verifyPin, 1);
    duk_put_prop_string(ctx, -2, "verifyPin");
    duk_push_c_function(ctx, js_pinClear, 0);
    duk_put_prop_string(ctx, -2, "pinClear");
    duk_push_c_function(ctx, js_pinState, 0);
    duk_put_prop_string(ctx, -2, "pinState");
    duk_push_c_function(ctx, js_webAuthInfo, 0);
    duk_put_prop_string(ctx, -2, "webAuthInfo");
    duk_push_c_function(ctx, js_webAuthSetPass, 1);
    duk_put_prop_string(ctx, -2, "webAuthSetPass");
    duk_push_c_function(ctx, js_rescanApps, 0);
    duk_put_prop_string(ctx, -2, "rescanApps");
    duk_push_c_function(ctx, js_factoryReset, 1);
    duk_put_prop_string(ctx, -2, "factoryReset");
    duk_push_c_function(ctx, js_otaCheck, 0);
    duk_put_prop_string(ctx, -2, "otaCheck");
    duk_push_c_function(ctx, js_otaStart, 2);
    duk_put_prop_string(ctx, -2, "otaStart");
    duk_push_c_function(ctx, js_setTimezone, 1);
    duk_put_prop_string(ctx, -2, "setTimezone");
    duk_push_c_function(ctx, js_setManualTime, 5);
    duk_put_prop_string(ctx, -2, "setManualTime");
    duk_push_c_function(ctx, js_set24hFormat, 1);
    duk_put_prop_string(ctx, -2, "set24hFormat");
    duk_push_c_function(ctx, js_get24hFormat, 0);
    duk_put_prop_string(ctx, -2, "get24hFormat");
    duk_push_c_function(ctx, js_setNtpEnabled, 1);
    duk_put_prop_string(ctx, -2, "setNtpEnabled");
    duk_push_c_function(ctx, js_getNtpEnabled, 0);
    duk_put_prop_string(ctx, -2, "getNtpEnabled");
    duk_push_c_function(ctx, js_webActive, 0);
    duk_put_prop_string(ctx, -2, "webActive");
    duk_push_c_function(ctx, js_webSetActive, 1);
    duk_put_prop_string(ctx, -2, "webSetActive");
    duk_push_c_function(ctx, js_theme, 0);
    duk_put_prop_string(ctx, -2, "theme");
    duk_push_c_function(ctx, js_drawIcon, 3);
    duk_put_prop_string(ctx, -2, "drawIcon");

    // Assign to global variable 'System'
    duk_put_prop_string(ctx, -2, "System");

    // --- Net Object (HTTP para apps, API level 2) ---
    duk_push_object(ctx); // Net
    duk_push_c_function(ctx, js_netGet, 1);
    duk_put_prop_string(ctx, -2, "get");
    duk_push_c_function(ctx, js_netGetJSON, 1);
    duk_put_prop_string(ctx, -2, "getJSON");
    duk_push_c_function(ctx, js_netPost, 3);
    duk_put_prop_string(ctx, -2, "post");
    duk_push_c_function(ctx, js_netDownload, 3);
    duk_put_prop_string(ctx, -2, "download");
    duk_push_c_function(ctx, js_netIsConnected, 0);
    duk_put_prop_string(ctx, -2, "isConnected");
    duk_push_c_function(ctx, js_wifiScan, 0);
    duk_put_prop_string(ctx, -2, "wifiScan");
    duk_push_c_function(ctx, js_wifiConnect, 2);
    duk_put_prop_string(ctx, -2, "wifiConnect");
    duk_push_c_function(ctx, js_wifiDisconnect, 0);
    duk_put_prop_string(ctx, -2, "wifiDisconnect");
    duk_put_prop_string(ctx, -2, "Net");

    // --- FS Object ---
    duk_push_object(ctx); // FS
    duk_push_c_function(ctx, js_readTextFile, 1);
    duk_put_prop_string(ctx, -2, "readTextFile");
    duk_push_c_function(ctx, js_writeTextFile, 2);
    duk_put_prop_string(ctx, -2, "writeTextFile");
    duk_push_c_function(ctx, js_appendTextFile, 2);
    duk_put_prop_string(ctx, -2, "appendTextFile");
    duk_push_c_function(ctx, js_deleteFile, 1);
    duk_put_prop_string(ctx, -2, "deleteFile");
    duk_push_c_function(ctx, js_renameFile, 2);
    duk_put_prop_string(ctx, -2, "renameFile");
    duk_push_c_function(ctx, js_fileExists, 1);
    duk_put_prop_string(ctx, -2, "exists");
    duk_push_c_function(ctx, js_listDir, 1);
    duk_put_prop_string(ctx, -2, "listDir");
    duk_push_c_function(ctx, js_mkdir, 1);
    duk_put_prop_string(ctx, -2, "mkdir");
    duk_push_c_function(ctx, js_rmdir, 1);
    duk_put_prop_string(ctx, -2, "rmdir");
    duk_push_c_function(ctx, js_isDirectory, 1);
    duk_put_prop_string(ctx, -2, "isDirectory");
    duk_push_c_function(ctx, js_isFile, 1);
    duk_put_prop_string(ctx, -2, "isFile");
    duk_push_c_function(ctx, js_getFileSize, 1);
    duk_put_prop_string(ctx, -2, "getFileSize");
    duk_push_c_function(ctx, js_getTotalSpace, 1);
    duk_put_prop_string(ctx, -2, "getTotalSpace");
    duk_push_c_function(ctx, js_getUsedSpace, 1);
    duk_put_prop_string(ctx, -2, "getUsedSpace");
    duk_push_c_function(ctx, js_getFreeSpace, 1);
    duk_put_prop_string(ctx, -2, "getFreeSpace");
    duk_push_c_function(ctx, js_getFileMD5, 1);
    duk_put_prop_string(ctx, -2, "getFileMD5");
    duk_push_c_function(ctx, js_mountSD, 0);
    duk_put_prop_string(ctx, -2, "mountSD");
    duk_push_c_function(ctx, js_unmountSD, 0);
    duk_put_prop_string(ctx, -2, "unmountSD");
    duk_push_c_function(ctx, js_copyFile, 2);
    duk_put_prop_string(ctx, -2, "copyFile");
    duk_push_c_function(ctx, js_copyDirectory, 2);
    duk_put_prop_string(ctx, -2, "copyDirectory");
    duk_push_c_function(ctx, js_removeDirectory, 1);
    duk_put_prop_string(ctx, -2, "removeDirectory");
    
    // Assign to global variable 'FS'
    duk_put_prop_string(ctx, -2, "FS");

    // --- Color Constants on global scope ---
    // Common TFT colors so JS apps don't need hex
    duk_push_uint(ctx, 0x0000); duk_put_prop_string(ctx, -2, "BLACK");
    duk_push_uint(ctx, 0xFFFF); duk_put_prop_string(ctx, -2, "WHITE");
    duk_push_uint(ctx, 0xF800); duk_put_prop_string(ctx, -2, "RED");
    duk_push_uint(ctx, 0x07E0); duk_put_prop_string(ctx, -2, "GREEN");
    duk_push_uint(ctx, 0x001F); duk_put_prop_string(ctx, -2, "BLUE");
    duk_push_uint(ctx, 0xFFE0); duk_put_prop_string(ctx, -2, "YELLOW");
    duk_push_uint(ctx, 0x07FF); duk_put_prop_string(ctx, -2, "CYAN");
    duk_push_uint(ctx, 0xF81F); duk_put_prop_string(ctx, -2, "MAGENTA");
    duk_push_uint(ctx, 0xFDA0); duk_put_prop_string(ctx, -2, "ORANGE");
    duk_push_uint(ctx, 0x7BEF); duk_put_prop_string(ctx, -2, "DARKGREY");

    duk_pop(ctx); // pop global object
}
