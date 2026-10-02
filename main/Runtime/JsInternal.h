#pragma once

// Estado e helpers compartilhados entre os modulos do runtime JS
// (JSBindings.cpp = nucleo: topbar/quadro/init; Js*.cpp = um dominio cada).
#include "sdkconfig.h"
#include <cstdio>
#include <string>
#include "JSBindings.h"
#include "../Display/Layout.h"
#include "../Launcher/LauncherUI.h"
#include <lgfx/v1/misc/DataWrapper.hpp>

extern CelerDisplay* s_jsTft;
extern bool s_topbarFixed;
extern CelerSprite* s_frame;
extern bool s_frameDirty;

// Capabilities do app corrente (F4) e packageName (FS.appData). Os guards
// vivem no init da fachada; js_appData (JsFs.cpp) le s_appPkg.
extern uint32_t s_perms;
extern std::string s_appPkg;
inline bool perm(uint32_t bit) { return (s_perms & bit) != 0; }

// Saida limpa de app (F2): erro MARCADO com a propriedade celerExit — o
// kernel identifica a marcacao (ou a string "OS_EXIT" exata, compat com
// apps que a lancam direto). Antes qualquer erro cujo texto continha
// "OS_EXIT" fechava o app silenciosamente.
[[noreturn]] inline void throwAppExit(duk_context* ctx) {
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
    if (LauncherUI::consumeAppExitRequest()) throwAppExit(ctx);
}

bool pollAppChrome(bool& touched, uint16_t& x, uint16_t& y);
bool imagePathOk(const char* path);
void keypadCloseSession();

inline uint32_t jsc(uint32_t c) {
    // Cores JS sao RGB565. No LovyanGFX o TIPO decide o formato: uint32_t e
    // lido como RGB888 — passar o 565 cru (como antes, "painel 16-bit")
    // trocava as cores. Expande sempre para 888; o LGFX converte ao nativo.
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}
inline int jsx(int v) { return UI::sx(v); }
inline int appSh(int v) {
    return s_topbarFixed ? (v * (UI::H - UI::topbarH()) / 320) : v * UI::H / 320;
}
inline float appScaleY() {
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

