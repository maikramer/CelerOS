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
#include "../Boards/Board.h"
#include <lgfx/v1/misc/DataWrapper.hpp>
#include "JsInternal.h"

// =====================================================
// Double Buffering
// =====================================================
//
// Sprites multiplos (API 12): pool de ate 4 (1 sem PSRAM — cada sprite
// come RAM interna no fallback 8-bit). createSprite devolve um id 1..4
// (0 = falhou) e o sprite novo vira o CORRENTE; draw*/pushSprite seguem
// o corrente (compat: apps antigos ignoram o retorno). useSprite(id)
// troca o alvo: 0 = quadro/display, 1..4 = sprite existente.

namespace {
CelerSprite* s_spritePool[4] = {};  // nullptr = livre
int spriteCap() { return Board::profile().hasPsram ? 4 : 1; }
}  // namespace

// Reset por app (JSBindings::init): app que saiu sem deleteSprite nao vaza
void JSBindings::deleteAllSprites() {
    for (CelerSprite*& s : s_spritePool) {
        if (s) {
            s->deleteSprite();
            delete s;
            s = nullptr;
        }
    }
    tftSprite = nullptr;  // corrente (ponteiro pertencia ao pool)
    useSprite = false;
}

duk_ret_t JSBindings::js_createSprite(duk_context *ctx) {
    if (!tftInstance) return 0;

    int w = duk_require_int(ctx, 0);
    int h = duk_require_int(ctx, 1);
    // Canvas virtual: o sprite e alocado no tamanho FISICO equivalente
    int pw = jsx(w);
    int ph = appSh(h);  // sprite = canvas do app: area abaixo da topbar

    int slot = -1;
    for (int i = 0; i < spriteCap(); i++) {
        if (s_spritePool[i] == nullptr) { slot = i; break; }
    }
    if (slot < 0) {
        // pool cheio: apps antigos esperam que createSprite recicle o
        // anterior — mantem a compat derrubando o sprite corrente
        if (tftSprite != nullptr) {
            for (int i = 0; i < 4; i++) {
                if (s_spritePool[i] == tftSprite) { slot = i; break; }
            }
        }
        if (slot < 0) { duk_push_int(ctx, 0); return 1; }
        tftSprite->deleteSprite();
        delete tftSprite;
        s_spritePool[slot] = nullptr;
        tftSprite = nullptr;
    }

    CelerSprite* spr = new CelerSprite(tftInstance);
    // Prefere PSRAM quando disponivel (sem PSRAM o LovyanGFX usa o heap)
    spr->setPsram(true);

    void* ptr = nullptr;

    // First try 16-bit color if we have plenty of contiguous RAM
    if (ESP.getMaxAllocHeap() > (uint32_t)(pw * ph * 2 + 10000)) {
        spr->setColorDepth(16);
        ptr = spr->createSprite(pw, ph);
    }

    // Fallback to 8-bit color if 16-bit failed or wasn't attempted
    if (!ptr) {
        spr->setColorDepth(8);
        ptr = spr->createSprite(pw, ph);
    }

    if (!ptr) {
        delete spr;
        duk_push_int(ctx, 0);
        return 1;
    }

    s_spritePool[slot] = spr;
    tftSprite = spr;   // novo sprite vira o corrente (compat)
    useSprite = false; // createSprite nao binda: quem desenha chama bind/use
    duk_push_int(ctx, slot + 1);
    return 1;
}

duk_ret_t JSBindings::js_deleteSprite(duk_context *ctx) {
    // Sem arg: apaga o CORRENTE (compat). Com id: apaga aquele.
    CelerSprite* victim = tftSprite;
    if (duk_is_number(ctx, 0)) {
        int id = duk_require_int(ctx, 0);
        if (id < 1 || id > 4) return 0;
        victim = s_spritePool[id - 1];
        if (victim == nullptr) return 0;
    }
    if (victim != nullptr) {
        for (CelerSprite*& s : s_spritePool) {
            if (s == victim) {
                victim->deleteSprite();
                delete victim;
                s = nullptr;
                break;
            }
        }
        if (tftSprite == victim) {
            tftSprite = nullptr;
            useSprite = false;
        }
    }
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

// API 12: troca o alvo do desenho. useSprite(0) volta ao quadro/display;
// useSprite(id) seleciona (e binda) o sprite id criado pelo createSprite.
duk_ret_t JSBindings::js_useSprite(duk_context *ctx) {
    int id = duk_require_int(ctx, 0);
    if (id == 0) {
        useSprite = false;
        duk_push_boolean(ctx, 1);
        return 1;
    }
    if (id < 1 || id > 4 || s_spritePool[id - 1] == nullptr) {
        useSprite = false;
        duk_push_boolean(ctx, 0);
        return 1;
    }
    tftSprite = s_spritePool[id - 1];
    useSprite = true;
    duk_push_boolean(ctx, 1);
    return 1;
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
bool imagePathOk(const char* path) {
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
    // datum = o do System.setTextDatum (API 12; TL por padrao, resetado a
    // cada app). Antes era forcado TL aqui e o setTextDatum nao fazia nada.
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

// API 12: datum de ancoragem do drawString no ALVO corrente (quadro,
// display ou sprite). 0=TL (default, comportamento anterior), 1=TC,
// 2=TR, 4=ML, 5=MC, 6=MR, 8=BL, 9=BC, 10=BR (mesmos valores do LovyanGFX).
// textWidth/fontHeight continuam medindo a string — centralizar na mao
// continua possivel; com datum o drawString ja posiciona.
duk_ret_t JSBindings::js_setTextDatum(duk_context *ctx) {
    if (!tftInstance) return 0;
    int d = duk_require_int(ctx, 0);
    if (d < 0 || d > 10) d = 0;
    gfx()->setTextDatum((textdatum_t)d);
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

