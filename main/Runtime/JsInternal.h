#pragma once

// Estado e helpers compartilhados entre os modulos do runtime JS
// (JSBindings.cpp = nucleo: topbar/quadro/init; Js*.cpp = um dominio cada).
#include "sdkconfig.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include "JSBindings.h"
#include "../Display/Layout.h"
#include "../Launcher/LauncherUI.h"
#include "../Display/FrameSprite.h"
#include <lgfx/v1/misc/DataWrapper.hpp>

extern CelerDisplay* s_jsTft;
extern bool s_topbarFixed;
// Canvas nativo (API 28): o app abriu mao do canvas virtual 240x320 e desenha
// em pixels FISICOS do vidro (480x480 na 4848). Todos os conversores abaixo
// viram identidade; o touch e screenWidth/Height passam a fisicos. Exigido
// app sem topbar fixa (System.setNativeCanvas recusa e devolve false).
extern bool s_nativeCanvas;
extern FrameSprite* s_frame;
// Pedido de push do quadro INTEIRO: o vidro foi sujado por fora do quadro
// (teclado do prompt). Desenho normal no quadro dispensa: a caixa suja do
// FrameSprite ja diz o que mudou.
extern bool s_frameDirty;

// Capabilities do app corrente (F4) e packageName (FS.appData). Os guards
// vivem no init da fachada; js_appData (JsFs.cpp) le s_appPkg.
extern uint32_t s_perms;
extern std::string s_appPkg;
// Pasta do app em execucao (dirname do main.js; vazia p/ .js avulso):
// base do require() de modulos (JsModules.cpp). Alimentada no runFile.
extern std::string s_appDir;
// Deps compartilhadas do app corrente (deps.json da pasta, versoes
// RESOLVIDAS no install): require() cai p/ /local/modules/<nome>/<versao>/
// quando o modulo nao esta na pasta do app. Tambem alimentada no runFile.
void jsLoadAppDeps(const char* appDir);
inline bool perm(uint32_t bit) { return (s_perms & bit) != 0; }

// Saida limpa de app (F2): erro MARCADO com a propriedade celerExit — o
// kernel identifica a marcacao (ou a string "OS_EXIT" exata, compat com
// apps que a lancam direto). Antes qualquer erro cujo texto continha
// "OS_EXIT" fechava o app silenciosamente.
// Saida pedida e "grudenta": um app com try/catch generico no loop engolia
// o erro e o X/exit remoto nunca fechava. Com s_appExitPending, todo ponto
// de espera seguinte (delay/getTouch/keypadPoll) relanca. Zerado no init.
extern bool s_appExitPending;

[[noreturn]] inline void throwAppExit(duk_context* ctx) {
    s_appExitPending = true;
    duk_push_error_object(ctx, DUK_ERR_ERROR, "app exit");
    duk_push_boolean(ctx, 1);
    duk_put_prop_string(ctx, -2, "celerExit");
    (void)duk_throw(ctx);  // longjmp: nunca retorna de verdade
    while (true) { }       // so para calmar o -Wreturn-type
}

// Encerramento remoto do app (shell "exit" / dev loop do SDK): checado nos
// pontos de espera do runtime (delay/getTouch/keypadPoll). Reusa a saida
// limpa do X da topbar — o kernel volta ao launcher normalmente.
inline void checkRemoteAppExit(duk_context* ctx) {
    if (s_appExitPending || LauncherUI::consumeAppExitRequest()) throwAppExit(ctx);
}

// Handoff de buffer malloc para o heap JS: duk_push_lstring LANCA (longjmp)
// se faltar RAM para a string, e o free do dono nunca rodaria — o buffer
// vazaria ate o reboot (pior caso: o WAV do Mic.stop, centenas de KB). O
// push roda sob duk_safe_call; o dono e sempre liberado e o erro, repassado
// ao script. Devolve 1 (string no stack).
struct JsPushBuf { const char* p; size_t len; };
inline duk_ret_t jsPushBufSafe(duk_context* ctx, void* u) {
    JsPushBuf* b = (JsPushBuf*)u;
    duk_push_lstring(ctx, b->p, b->len);
    return 1;
}
inline int jsPushOwnedString(duk_context* ctx, char* p, size_t len) {
    JsPushBuf b{p, len};
    duk_int_t rc = duk_safe_call(ctx, jsPushBufSafe, &b, 0, 1);
    free(p);
    if (rc != DUK_EXEC_SUCCESS) duk_throw(ctx);
    return 1;
}

bool pollAppChrome(bool& touched, uint16_t& x, uint16_t& y);
bool imagePathOk(const char* path);
void keypadCloseSession();

// Desenho do SISTEMA no alvo do app (faixa, teclado acoplado): salva o
// estado de texto (cor/datum/tamanho/fonte) e o recorte do app, desenha
// limpo e restaura na saida do escopo — nada vaza em nenhum sentido.
struct GfxStateGuard {
    lgfx::LGFXBase& g;
    lgfx::TextStyle style;
    const lgfx::IFont* font;
    int32_t cx, cy, cw, ch;
    explicit GfxStateGuard(lgfx::LGFXBase& target) : g(target), style(target.getTextStyle()), font(target.getFont()) {
        g.getClipRect(&cx, &cy, &cw, &ch);
        g.clearClipRect();
        g.setTextSize(1);
    }
    ~GfxStateGuard() {
        g.setTextStyle(style);
        g.setFont(font);
        g.setClipRect(cx, cy, cw, ch);
    }
    GfxStateGuard(const GfxStateGuard&) = delete;
    GfxStateGuard& operator=(const GfxStateGuard&) = delete;
};

inline uint32_t jsc(uint32_t c) {
    // Cores JS sao RGB565. No LovyanGFX o TIPO decide o formato: uint32_t e
    // lido como RGB888 — passar o 565 cru (como antes, "painel 16-bit")
    // trocava as cores. Expande sempre para 888; o LGFX converte ao nativo.
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}
// Fonte numerica do JS (drawString/textWidth/fontHeight): so 1..8 existem
// no fontdata[] do LovyanGFX (9 = nullptr, >9/negativo = alem do fim do
// array, e o CelerFont(uint8_t) truncava 300 em 44) — drawString(s, x, y,
// 9) derrubava o aparelho. Fora da faixa vira a fonte padrao (2).
inline const lgfx::IFont* jsFont(int f) {
    if (f < 1 || f > 8) f = 2;
    return CelerFont(UI::font(f));
}
inline int jsx(int v) { return s_nativeCanvas ? v : UI::sx(v); }
inline int appSh(int v) {
    if (s_nativeCanvas) return v;
    return s_topbarFixed ? (v * (UI::H - UI::topbarH()) / 320) : v * UI::H / 320;
}
inline float appScaleY() {
    if (s_nativeCanvas) return 1.0f;
    return (float)(s_topbarFixed ? (UI::H - UI::topbarH()) : UI::H) / 320.0f;
}
// Raio "uniforme": MINIMO das duas escalas. O circulo desenhado no vidro tem
// um unico raio fisico, mas o app raciocina num bbox virtual (cx±r, cy±r) —
// com o minimo ele fica CONTIDO nesse bbox nos dois eixos. A media antiga
// estourava o eixo menor em escala nao-inteira (4848: sx 2 / sy 1,5 → r4
// virava 7 fisicos e passava 1px da celula), e apps que apagam circulo com
// fillRect do retangulo virtual deixavam a ultima linha viva (artefato).
inline int jsu(int v) {
    int a = UI::sx(v), b = appSh(v);
    return a < b ? a : b;
}
inline int jsH(int v) { return appSh(v); }
inline int jsy(int v) { return JSBindings::mapY(v); }

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

