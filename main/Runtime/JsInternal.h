#pragma once

// Estado e helpers compartilhados entre os modulos do runtime JS
// (JSBindings.cpp = nucleo: topbar/quadro/init; Js*.cpp = um dominio cada).
#include "sdkconfig.h"
#include <cstdio>
#include <string>
#include "JSBindings.h"
#include "../Display/Layout.h"
#include <lgfx/v1/misc/DataWrapper.hpp>

extern CelerDisplay* s_jsTft;
extern bool s_topbarFixed;
extern CelerSprite* s_frame;
extern bool s_frameDirty;

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
inline int jsu(int v) { return (UI::sx(v) + appSh(v)) / 2; }  // uniforme (raios)
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

