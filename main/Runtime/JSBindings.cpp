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
#include "JsInternal.h"
#include "../Utils/CelerSettings.h"
#include "../Boards/Board.h"
#include "driver/ledc.h"

// ---------------------------------------------------------------------------
// Camada de compatibilidade JS: canvas virtual 240x320 + cores RGB565.
//
// Os apps da loja sao escritos para o alvo classico (CYD): cores literais
// RGB565 (0xF800, 0x18E3...) e geometria 240x320. Em paineis maiores o
// runtime "mente" para o app: reporta 240x320, escala coordenadas/tamanhos
// para a tela fisica (UI::sx/sy) e converte cores RGB565 -> formato nativo.
// No alvo classico os fatores sao 1:1 e a camada e transparente.
// ---------------------------------------------------------------------------
CelerDisplay* s_jsTft = nullptr;  // setado no JSBindings::init
uint32_t s_perms = celer::PERM_ALL;   // capabilities do app corrente (F4)
std::string s_appPkg;                 // packageName do app corrente (F4)


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
bool s_topbarFixed = true;    // setado por app pelo launcher (init)
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
// Escala vertical fracionaria (drawBMP/drawPNG escalam por float)
// jsy (alvo-dependent: quadro/display x sprite do app) vive depois das
// declaracoes de tftSprite/useSprite, logo abaixo.
// jsy e mapa de COORDENADA (soma o offset da faixa no modo fixo); jsH e a
// escala pura de TAMANHO vertical — altura/largura-vertical nunca podem
// receber o offset (uma linha de 3px viraria uma faixa de ~31px)

// Wrapper stdio para o drawPngFile do LGFX (a especializacao DataWrapperT<FILE>
// do upstream so ativa com macros do newlib que nao estao definidas no IDF)

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
CelerSprite* s_frame = nullptr;
bool s_frameDirty = false;

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
bool pollAppChrome(bool& touched, uint16_t& x, uint16_t& y) {
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
// Init - Register ALL bindings
// =====================================================

// Registro por tabela: {nome JS, funcao C, nargs} -> propriedade do objeto
// no topo da pilha. Menos codigo que um push+put por binding.
struct JsFn {
    const char* name;
    duk_c_function fn;
    duk_idx_t nargs;
};

template <size_t N>
static void putFns(duk_context* ctx, const JsFn (&fns)[N]) {
    for (const JsFn& f : fns) {
        duk_push_c_function(ctx, f.fn, f.nargs);
        duk_put_prop_string(ctx, -2, f.name);
    }
}

void JSBindings::init(duk_context *ctx, CelerDisplay *tft, const char* appTitle,
                      bool topbarFixed, const char* appPkg, uint32_t perms) {
    s_jsTft = tft;
    tftInstance = tft;
    s_appTitle = appTitle ? appTitle : "";
    s_topbarFixed = topbarFixed;
    s_appPkg = appPkg ? appPkg : "";
    s_perms = perms;
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

#if CONFIG_CELEROS_JS_GPIO
    // System.gpio sub-object — so com a capability "gpio" (F4)
    if (perm(celer::PERM_GPIO))
    duk_push_object(ctx);
    static const JsFn kFns1[] = {
        {"pinMode", js_pinMode, 2},
        {"digitalWrite", js_digitalWrite, 2},
        {"digitalRead", js_digitalRead, 1},
        {"analogRead", js_analogRead, 1},
        {"analogWrite", js_analogWrite, 2},
        {"pulseIn", js_pulseIn, 3},
    };
    putFns(ctx, kFns1);
    
    // GPIO Constants
    duk_push_int(ctx, OUTPUT); duk_put_prop_string(ctx, -2, "OUTPUT");
    duk_push_int(ctx, INPUT); duk_put_prop_string(ctx, -2, "INPUT");
    duk_push_int(ctx, INPUT_PULLUP); duk_put_prop_string(ctx, -2, "INPUT_PULLUP");
    duk_push_int(ctx, HIGH); duk_put_prop_string(ctx, -2, "HIGH");
    duk_push_int(ctx, LOW); duk_put_prop_string(ctx, -2, "LOW");
    
    if (perm(celer::PERM_GPIO))
    duk_put_prop_string(ctx, -2, "gpio");
#endif

    // --- Drawing Primitives ---

    static const JsFn kFns2[] = {
        {"createSprite", js_createSprite, 2},
        {"deleteSprite", js_deleteSprite, 0},
        {"pushSprite", js_pushSprite, 2},
        {"bindSprite", js_bindSprite, 1},
        {"drawFastVLine", js_drawFastVLine, 4},
        {"drawFastHLine", js_drawFastHLine, 4},
    };
    putFns(ctx, kFns2);

    static const JsFn kFns3[] = {
        {"fillScreen", js_fillScreen, 1},
        {"fillRect", js_fillRect, 5},
        {"drawRect", js_drawRect, 5},
        {"drawLine", js_drawLine, 5},
        {"drawPixel", js_drawPixel, 3},
        {"drawCircle", js_drawCircle, 4},
        {"fillCircle", js_fillCircle, 4},
        {"drawTriangle", js_drawTriangle, 7},
        {"fillTriangle", js_fillTriangle, 7},
        {"drawRoundRect", js_drawRoundRect, 6},
        {"fillRoundRect", js_fillRoundRect, 6},
    };
    putFns(ctx, kFns3);
    
    static const JsFn kFns4[] = {
        {"drawBMP", js_drawBMP, 3},
        {"drawPNG", js_drawPNG, 3},
    };
    putFns(ctx, kFns4);

    // --- Text ---
    static const JsFn kFns5[] = {
        {"drawString", js_drawString, 4},
        {"setTextColor", js_setTextColor, 2},
        {"setTextSize", js_setTextSize, 1},
        {"textWidth", js_textWidth, 2},
        {"fontHeight", js_fontHeight, 1},
    };
    putFns(ctx, kFns5);

    // --- Utility ---
    static const JsFn kFns6[] = {
        {"color", js_color, 3},
        {"screenWidth", js_screenWidth, 0},
        {"screenHeight", js_screenHeight, 0},
    };
    putFns(ctx, kFns6);

    // --- Touch Input ---
    static const JsFn kFns7[] = {
        {"getTouch", js_getTouch, 0},
        {"millis", js_millis, 0},
        {"micros", js_micros, 0},
        {"delay", js_delay, 1},
        {"delayMicroseconds", js_delayMicroseconds, 1},
        {"print", js_print, 1},
        {"getTemperature", js_getTemperature, 0},
        {"hasTemperatureSensor", js_hasTemperatureSensor, 0},
        {"getInfo", js_getInfo, 0},
    };
    putFns(ctx, kFns7);
    
    static const JsFn kFns8[] = {
        {"getTime", js_getTime, 0},
        {"getSeconds", js_getSeconds, 0},
        {"getDate", js_getDate, 0},
        {"getYear", js_getYear, 0},
        {"getMonth", js_getMonth, 0},
        {"getDay", js_getDay, 0},
        {"getTimezone", js_getTimezone, 0},
    };
    putFns(ctx, kFns8);

    static const JsFn kFns9[] = {
        {"getOSVersion", js_getOSVersion, 0},
    };
    putFns(ctx, kFns9);

    static const JsFn kFns10[] = {
        {"getAPILevel", js_getAPILevel, 0},
    };
    putFns(ctx, kFns10);

    static const JsFn kFns11[] = {
        {"getIPAddress", js_getIPAddress, 0},
    };
    putFns(ctx, kFns11);

    static const JsFn kFns12[] = {
        {"isWiFiActive", js_isWiFiActive, 0},
    };
    putFns(ctx, kFns12);

    // --- Keyboard ---
    static const JsFn kFns13[] = {
        {"prompt", js_prompt, 2},
    };
    putFns(ctx, kFns13);

    // --- Keyboard acoplado (API level 5): sessao nao-bloqueante p/ apps ---
    static const JsFn kFns14[] = {
        {"keypadOpen", js_keypadOpen, 1},
        {"keypadPoll", js_keypadPoll, 0},
        {"keypadText", js_keypadText, 0},
        {"keypadRect", js_keypadRect, 0},
        {"keypadDraw", js_keypadDraw, 0},
        {"keypadClose", js_keypadClose, 0},
    };
    putFns(ctx, kFns14);

    // --- Topbar custom (API level 6): texto e chips da faixa ---
    static const JsFn kFns15[] = {
        {"topbarText", js_topbarText, 1},
        {"topbarButtons", js_topbarButtons, 1},
        {"topbarPop", js_topbarPop, 0},
    };
    putFns(ctx, kFns15);

    // --- System nivel 3 (apps de sistema em JS) ---
    static const JsFn kFns16[] = {
        {"setBrightness", js_setBrightness, 1},
        {"getBrightness", js_getBrightness, 0},
        {"backlightSupported", js_backlightSupported, 0},
        {"exitApp", js_exitApp, 0},
        {"setClip", js_setClip, 4},
        {"clearClip", js_clearClip, 0},
        {"present", js_present, 0},
        {"isBuffered", js_isBuffered, 0},
        {"wifiStatus", js_wifiStatus, 0},
        {"md5", js_md5, 1},
        {"setPin", js_setPin, 1},
        {"verifyPin", js_verifyPin, 1},
        {"pinClear", js_pinClear, 0},
        {"pinState", js_pinState, 0},
        {"rescanApps", js_rescanApps, 0},
        {"setting", js_setting, 2},
        {"toast", js_toast, 1},
        {"beep", js_beep, 2},
        {"setTimezone", js_setTimezone, 1},
        {"setManualTime", js_setManualTime, 5},
        {"set24hFormat", js_set24hFormat, 1},
        {"get24hFormat", js_get24hFormat, 0},
        {"setNtpEnabled", js_setNtpEnabled, 1},
        {"getNtpEnabled", js_getNtpEnabled, 0},
        {"theme", js_theme, 0},
        {"drawIcon", js_drawIcon, 3},
    };
    putFns(ctx, kFns16);

    // Chamadas que comprometem o aparelho: capability "system" (F4)
    static const JsFn kFnsSysDanger[] = {
        {"restart", js_restart, 0},
        {"openWifiSetup", js_openWifiSetup, 0},
        {"factoryReset", js_factoryReset, 1},
        {"otaCheck", js_otaCheck, 0},
        {"otaStart", js_otaStart, 2},
        {"webActive", js_webActive, 0},
        {"webSetActive", js_webSetActive, 1},
        {"webAuthInfo", js_webAuthInfo, 0},
        {"webAuthSetPass", js_webAuthSetPass, 1},
    };
    if (perm(celer::PERM_SYSTEM)) putFns(ctx, kFnsSysDanger);

    // Assign to global variable 'System'
    duk_put_prop_string(ctx, -2, "System");

    // --- Net Object (HTTP para apps, API level 2) — capability "net" (F4) ---
    if (perm(celer::PERM_NET)) {
    duk_push_object(ctx); // Net
    static const JsFn kFns17[] = {
        {"get", js_netGet, 1},
        {"getJSON", js_netGetJSON, 1},
        {"post", js_netPost, 3},
        {"download", js_netDownload, 3},
        {"beginGet", js_netBeginGet, 1},
        {"pollGet", js_netPollGet, 1},
        {"cancelGet", js_netCancelGet, 1},
        {"isConnected", js_netIsConnected, 0},
        {"wifiScan", js_wifiScan, 0},
    };
    putFns(ctx, kFns17);
    // wifiConnect/wifiDisconnect gravam credenciais: exigem "system" tambem
    static const JsFn kFnsNetSys[] = {
        {"wifiConnect", js_wifiConnect, 2},
        {"wifiDisconnect", js_wifiDisconnect, 0},
    };
    if (perm(celer::PERM_SYSTEM)) putFns(ctx, kFnsNetSys);
    duk_put_prop_string(ctx, -2, "Net");
    }

    // --- FS Object — capability "fs" (F4) ---
    if (perm(celer::PERM_FS)) {
    duk_push_object(ctx); // FS
    static const JsFn kFns18[] = {
        {"readTextFile", js_readTextFile, 1},
        {"writeTextFile", js_writeTextFile, 2},
        {"appendTextFile", js_appendTextFile, 2},
        {"deleteFile", js_deleteFile, 1},
        {"renameFile", js_renameFile, 2},
        {"exists", js_fileExists, 1},
        {"listDir", js_listDir, 1},
        {"mkdir", js_mkdir, 1},
        {"rmdir", js_rmdir, 1},
        {"isDirectory", js_isDirectory, 1},
        {"isFile", js_isFile, 1},
        {"getFileSize", js_getFileSize, 1},
        {"getTotalSpace", js_getTotalSpace, 1},
        {"getUsedSpace", js_getUsedSpace, 1},
        {"getFreeSpace", js_getFreeSpace, 1},
        {"getFileMD5", js_getFileMD5, 1},
        {"mountSD", js_mountSD, 0},
        {"unmountSD", js_unmountSD, 0},
        {"copyFile", js_copyFile, 2},
        {"copyDirectory", js_copyDirectory, 2},
        {"removeDirectory", js_removeDirectory, 1},
        {"appData", js_appData, 0},
    };
    putFns(ctx, kFns18);

    // Assign to global variable 'FS'
    duk_put_prop_string(ctx, -2, "FS");
    }

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
