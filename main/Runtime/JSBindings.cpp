#include "JSBindings.h"
#include "../Display/Layout.h"
#include "../FileSystem/FileSystem.h"
#include "../UI/Keyboard.h"
#include "../WebManager/WebManager.h"
#include "../Kernel/TimeManager.h"
#include "../Utils/StrUtils.h"
#include "HttpClient.h"
#include "SystemInfo.h"

// ---------------------------------------------------------------------------
// Camada de compatibilidade JS: canvas virtual 240x320 + cores RGB565.
//
// Os apps da loja sao escritos para o alvo classico (CYD): cores literais
// RGB565 (0xF800, 0x18E3...) e geometria 240x320. Em paineis maiores o
// runtime "mente" para o app: reporta 240x320, escala coordenadas/tamanhos
// para a tela fisica (UI::sx/sy) e converte cores RGB565 -> formato nativo.
// No alvo classico os fatores sao 1:1 e a camada e transparente.
// ---------------------------------------------------------------------------
static KryonDisplay* s_jsTft = nullptr;  // setado no JSBindings::init

static inline uint32_t jsc(uint32_t c) {
    // Painel 16-bit (RGB565 nativo): cor passa direto; caso contrario
    // expande RGB565 -> RGB888 (determinado pelo display em runtime)
    if (s_jsTft == nullptr || s_jsTft->getColorDepth() == 16) {
        return c;
    }
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return ((((r << 3) | (r >> 2)) & 0xFF) << 16)
         | ((((g << 2) | (g >> 4)) & 0xFF) << 8)
         | (((b << 3) | (b >> 2)) & 0xFF);
}
static inline int jsx(int v) { return UI::sx(v); }
static inline int jsy(int v) { return UI::sy(v); }
static inline int jsu(int v) { return (UI::sx(v) + UI::sy(v)) / 2; }  // uniforme (raios)

KryonDisplay* JSBindings::tftInstance = nullptr;
KryonSprite* JSBindings::tftSprite = nullptr;
bool JSBindings::useSprite = false;

void JSBindings::fatalErrorHandler(void *udata, const char *msg) {
    (void) udata;
    Serial.print("*** FATAL ERROR: ");
    Serial.println(msg ? msg : "no message");
    
    if (msg && strstr(msg, "alloc")) {
        Serial.println("out of memory");
        if (tftInstance) {
            if (useSprite && tftSprite) tftSprite->fillScreen(TFT_RED); else tftInstance->fillScreen(TFT_RED);
            if (useSprite && tftSprite) tftSprite->setTextColor(TFT_WHITE, TFT_RED); else tftInstance->setTextColor(TFT_WHITE, TFT_RED);
            if (useSprite && tftSprite) tftSprite->drawString("OUT OF MEMORY", 10, 10, 4); else tftInstance->drawString("OUT OF MEMORY", 10, 10, 4);
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
    int ph = jsy(h);

    if (tftSprite) {
        tftSprite->deleteSprite();
        delete tftSprite;
        tftSprite = nullptr;
    }

    tftSprite = new KryonSprite(tftInstance);
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
    tftSprite->pushSprite(jsx(x), jsy(y));
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
    if (useSprite && tftSprite) tftSprite->drawFastVLine(jsx(x), jsy(y), jsy(h), jsc(color));
    else tftInstance->drawFastVLine(jsx(x), jsy(y), jsy(h), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawFastHLine(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    uint32_t color = duk_require_uint(ctx, 3);
    if (useSprite && tftSprite) tftSprite->drawFastHLine(jsx(x), jsy(y), jsx(w), jsc(color));
    else tftInstance->drawFastHLine(jsx(x), jsy(y), jsx(w), jsc(color));
    return 0;
}

// =====================================================
// Display Bindings - Drawing Primitives
// =====================================================

duk_ret_t JSBindings::js_fillScreen(duk_context *ctx) {
    if (!tftInstance) return 0;
    uint32_t color = duk_require_uint(ctx, 0);
    if (useSprite && tftSprite) tftSprite->fillScreen(jsc(color)); else tftInstance->fillScreen(jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_fillRect(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    int h = duk_require_int(ctx, 3);
    uint32_t color = duk_require_uint(ctx, 4);
    if (useSprite && tftSprite) tftSprite->fillRect(jsx(x), jsy(y), jsx(w), jsy(h), jsc(color)); else tftInstance->fillRect(jsx(x), jsy(y), jsx(w), jsy(h), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawRect(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    int h = duk_require_int(ctx, 3);
    uint32_t color = duk_require_uint(ctx, 4);
    if (useSprite && tftSprite) tftSprite->drawRect(jsx(x), jsy(y), jsx(w), jsy(h), jsc(color)); else tftInstance->drawRect(jsx(x), jsy(y), jsx(w), jsy(h), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawLine(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x0 = duk_require_int(ctx, 0);
    int y0 = duk_require_int(ctx, 1);
    int x1 = duk_require_int(ctx, 2);
    int y1 = duk_require_int(ctx, 3);
    uint32_t color = duk_require_uint(ctx, 4);
    if (useSprite && tftSprite) tftSprite->drawLine(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsc(color)); else tftInstance->drawLine(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawPixel(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    uint32_t color = duk_require_uint(ctx, 2);
    if (useSprite && tftSprite) tftSprite->drawPixel(jsx(x), jsy(y), jsc(color)); else tftInstance->drawPixel(jsx(x), jsy(y), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_drawCircle(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int r = duk_require_int(ctx, 2);
    uint32_t color = duk_require_uint(ctx, 3);
    if (useSprite && tftSprite) tftSprite->drawCircle(jsx(x), jsy(y), jsu(r), jsc(color)); else tftInstance->drawCircle(jsx(x), jsy(y), jsu(r), jsc(color));
    return 0;
}

duk_ret_t JSBindings::js_fillCircle(duk_context *ctx) {
    if (!tftInstance) return 0;
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int r = duk_require_int(ctx, 2);
    uint32_t color = duk_require_uint(ctx, 3);
    if (useSprite && tftSprite) tftSprite->fillCircle(jsx(x), jsy(y), jsu(r), jsc(color)); else tftInstance->fillCircle(jsx(x), jsy(y), jsu(r), jsc(color));
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
    if (useSprite && tftSprite) tftSprite->drawTriangle(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsx(x2), jsy(y2), jsc(color)); else tftInstance->drawTriangle(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsx(x2), jsy(y2), jsc(color));
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
    if (useSprite && tftSprite) tftSprite->fillTriangle(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsx(x2), jsy(y2), jsc(color)); else tftInstance->fillTriangle(jsx(x0), jsy(y0), jsx(x1), jsy(y1), jsx(x2), jsy(y2), jsc(color));
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
    if (useSprite && tftSprite) tftSprite->drawRoundRect(jsx(x), jsy(y), jsx(w), jsy(h), jsu(r), jsc(color)); else tftInstance->drawRoundRect(jsx(x), jsy(y), jsx(w), jsy(h), jsu(r), jsc(color));
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
    if (useSprite && tftSprite) tftSprite->fillRoundRect(jsx(x), jsy(y), jsx(w), jsy(h), jsu(r), jsc(color)); else tftInstance->fillRoundRect(jsx(x), jsy(y), jsx(w), jsy(h), jsu(r), jsc(color));
    return 0;
}

// Helper functions for BMP parsing
static uint16_t read16(FILE *f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = fgetc(f); // LSB
  ((uint8_t *)&result)[1] = fgetc(f); // MSB
  return result;
}

static uint32_t read32(FILE *f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = fgetc(f); // LSB
  ((uint8_t *)&result)[1] = fgetc(f);
  ((uint8_t *)&result)[2] = fgetc(f);
  ((uint8_t *)&result)[3] = fgetc(f); // MSB
  return result;
}

duk_ret_t JSBindings::js_drawBMP(duk_context *ctx) {
    if (!tftInstance) return 0;
    const char *path = duk_require_string(ctx, 0);
    int x = duk_require_int(ctx, 1);
    int y = duk_require_int(ctx, 2);

    // /sd e /local sao pontos de montagem reais no VFS: abre o caminho original
    if (strncmp(path, "/sd", 3) != 0 && strncmp(path, "/local", 6) != 0) {
        duk_push_boolean(ctx, 0);
        return 1;
    }

    FILE *bmpFS = fopen(path, "rb");
    if (!bmpFS) { Serial.printf("BMP ERR: Could not open file %s\n", path); duk_push_boolean(ctx, 0); return 1; }

    uint16_t sig = read16(bmpFS);
    if (sig != 0x4D42) { // "BM" signature
        Serial.printf("BMP ERR: Invalid signature: 0x%04X\n", sig);
        fclose(bmpFS);
        duk_push_boolean(ctx, 0);
        return 1;
    }

    read32(bmpFS); // File size
    read32(bmpFS); // Creator bytes
    uint32_t imageOffset = read32(bmpFS); // Pixel data offset
    read32(bmpFS); // DIB header size
    int32_t bmpWidth = read32(bmpFS);
    int32_t bmpHeight = read32(bmpFS);
    
    uint16_t planes = read16(bmpFS);
    if (planes != 1) { // Planes must be 1
        Serial.printf("BMP ERR: Invalid planes: %d\n", planes);
        fclose(bmpFS);
        duk_push_boolean(ctx, 0);
        return 1;
    }
    
    uint16_t bmpDepth = read16(bmpFS);
    if (bmpDepth != 16 && bmpDepth != 24 && bmpDepth != 32) { // 16, 24, 32-bit BMPs supported
        Serial.printf("BMP ERR: Unsupported depth: %d\n", bmpDepth);
        fclose(bmpFS);
        duk_push_boolean(ctx, 0);
        return 1;
    }

    uint32_t comp = read32(bmpFS);
    if (comp != 0 && comp != 3) { // 0=BI_RGB, 3=BI_BITFIELDS
        Serial.printf("BMP ERR: Unsupported compression: %lu\n", comp);
        fclose(bmpFS);
        duk_push_boolean(ctx, 0);
        return 1;
    }

    // Determine row size and padding
    bool flip = true;
    if (bmpHeight < 0) {
        bmpHeight = -bmpHeight;
        flip = false;
    }

    uint32_t bytesPerPixel = bmpDepth / 8;
    uint32_t rowSize = (bmpWidth * bytesPerPixel + 3) & ~3;
    uint8_t sdbuffer[4 * 64]; // Read buffer (max 4 bytes per pixel * 64 pixels)
    uint16_t tftbuffer[64];   // Convert to 16-bit 565 colors

    fseek(bmpFS, imageOffset, SEEK_SET);

    // Draw row by row
    for (int row = 0; row < bmpHeight; row++) {
        int drawY = flip ? (jsy(y) + (int)((bmpHeight - 1 - row) * (float)UI::H / 320.0f)) : (jsy(y) + (int)(row * (float)UI::H / 320.0f));
        
        // Skip drawing if out of bounds
        if (drawY < 0 || drawY >= tftInstance->height()) {
            fseek(bmpFS, (long)rowSize, SEEK_CUR);
            continue;
        }

        uint32_t pixelsRead = 0;
        while (pixelsRead < bmpWidth) {
            uint32_t pixelsToRead = bmpWidth - pixelsRead;
            if (pixelsToRead > 64) pixelsToRead = 64;
            fread(sdbuffer, 1, pixelsToRead * bytesPerPixel, bmpFS);
            
            for (uint32_t i = 0; i < pixelsToRead; i++) {
                if (bmpDepth == 24) {
                    uint8_t b = sdbuffer[i*3];
                    uint8_t g = sdbuffer[i*3+1];
                    uint8_t r = sdbuffer[i*3+2];
                    tftbuffer[i] = tftInstance->color565(r, g, b);
                } else if (bmpDepth == 32) {
                    uint8_t b = sdbuffer[i*4];
                    uint8_t g = sdbuffer[i*4+1];
                    uint8_t r = sdbuffer[i*4+2];
                    tftbuffer[i] = tftInstance->color565(r, g, b);
                } else if (bmpDepth == 16) {
                    uint8_t b1 = sdbuffer[i*2];
                    uint8_t b2 = sdbuffer[i*2+1];
                    tftbuffer[i] = (b2 << 8) | b1;
                }
            }

            int drawX = jsx(x) + (int)(pixelsRead * (float)UI::W / 240.0f);
            tftInstance->pushImage(drawX, drawY, (int)(pixelsToRead * (float)UI::W / 240.0f), 1, tftbuffer);
            
            pixelsRead += pixelsToRead;
        }

        // Skip padding
        uint32_t padding = rowSize - (bmpWidth * bytesPerPixel);
        if (padding > 0) {
            uint8_t padBuffer[4];
            fread(padBuffer, 1, padding, bmpFS);
        }
    }

    fclose(bmpFS);
    duk_push_boolean(ctx, 1);
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
    tftInstance->setTextDatum(TL_DATUM);
    if (useSprite && tftSprite) tftSprite->drawString(str, jsx(x), jsy(y), KryonFont(UI::font(font))); else tftInstance->drawString(str, jsx(x), jsy(y), UI::font(font));
    return 0;
}

duk_ret_t JSBindings::js_setTextColor(duk_context *ctx) {
    if (!tftInstance) return 0;
    uint32_t fg = duk_require_uint(ctx, 0);
    // Optional background color (defaults to foreground = transparent)
    if (duk_is_number(ctx, 1)) {
        uint32_t bg = duk_require_uint(ctx, 1);
        if (useSprite && tftSprite) tftSprite->setTextColor(jsc(fg), jsc(bg)); else tftInstance->setTextColor(jsc(fg), jsc(bg));
    } else {
        if (useSprite && tftSprite) tftSprite->setTextColor(jsc(fg)); else tftInstance->setTextColor(jsc(fg));
    }
    return 0;
}

duk_ret_t JSBindings::js_setTextSize(duk_context *ctx) {
    if (!tftInstance) return 0;
    int size = duk_require_int(ctx, 0);
    if (useSprite && tftSprite) tftSprite->setTextSize(size); else tftInstance->setTextSize(size);
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
    uint16_t tx, ty;
    bool touched = false;
    if (tftInstance) {
        touched = tftInstance->getTouch(&tx, &ty);
        
        // Hidden OS Exit Button (Top Right Corner)
        if (touched && UI::hitExit(tx, ty)) {
            duk_error(ctx, DUK_ERR_ERROR, "OS_EXIT");
            return 0; // Unreachable, but good practice
        }
    }
    
    // Coordenadas no espaco de projeto 240x320 (hit-zones dos apps batem)
    int jx = tftInstance ? ((int)tx * 240 / tftInstance->width()) : 0;
    int jy = tftInstance ? ((int)ty * 320 / tftInstance->height()) : 0;
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
    if (ms > 0 && ms < 30000) { // Safety cap at 30 seconds
        delay(ms);
    }
    // Perform manual garbage collection while the system is theoretically "idle"
    // This aggressively prevents memory fragmentation and 'alloc failed' errors on ESP32
    duk_gc(ctx, 0);
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
    Serial.println(msg);
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
    duk_push_string(ctx, KRYONOS_VERSION);
    return 1;
}

duk_ret_t JSBindings::js_getAPILevel(duk_context *ctx) {
    duk_push_int(ctx, KRYONOS_API_LEVEL);
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
    std::string files[30];
    int count = FileSystem::listDir(path, files, 30);
    
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
    duk_push_uint(ctx, FileSystem::getTotalSpace(drive));
    return 1;
}

duk_ret_t JSBindings::js_getUsedSpace(duk_context *ctx) {
    const char *drive = duk_require_string(ctx, 0);
    duk_push_uint(ctx, FileSystem::getUsedSpace(drive));
    return 1;
}

duk_ret_t JSBindings::js_getFreeSpace(duk_context *ctx) {
    const char *drive = duk_require_string(ctx, 0);
    duk_push_uint(ctx, FileSystem::getFreeSpace(drive));
    return 1;
}

duk_ret_t JSBindings::js_getFileMD5(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_string(ctx, FileSystem::getFileMD5(path).c_str());
    return 1;
}

duk_ret_t JSBindings::js_mountSD(duk_context *ctx) {
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

    std::string result = kui::getString(initialText, promptMsg);
    
    duk_push_string(ctx, result.c_str());
    
    return 1;
}

// =====================================================
// Init - Register ALL bindings
// =====================================================

void JSBindings::init(duk_context *ctx, KryonDisplay *tft) {
    s_jsTft = tft;
    tftInstance = tft;

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

    // --- Text ---
    duk_push_c_function(ctx, js_drawString, 4);
    duk_put_prop_string(ctx, -2, "drawString");
    duk_push_c_function(ctx, js_setTextColor, 2);
    duk_put_prop_string(ctx, -2, "setTextColor");
    duk_push_c_function(ctx, js_setTextSize, 1);
    duk_put_prop_string(ctx, -2, "setTextSize");

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
    duk_push_c_function(ctx, js_netIsConnected, 0);
    duk_put_prop_string(ctx, -2, "isConnected");
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
    
    // Assign to global variable 'FS'
    duk_put_prop_string(ctx, -2, "FS");

    // --- Color Constants on global scope ---
    // Common TFT colors so JS apps don't need hex
    duk_push_uint(ctx, 0x0000);  // RGB565 (convencao JS)   duk_put_prop_string(ctx, -2, "BLACK");
    duk_push_uint(ctx, 0xFFFF);  // RGB565 (convencao JS)   duk_put_prop_string(ctx, -2, "WHITE");
    duk_push_uint(ctx, 0xF800);  // RGB565 (convencao JS)     duk_put_prop_string(ctx, -2, "RED");
    duk_push_uint(ctx, 0x07E0);  // RGB565 (convencao JS)   duk_put_prop_string(ctx, -2, "GREEN");
    duk_push_uint(ctx, 0x001F);  // RGB565 (convencao JS)    duk_put_prop_string(ctx, -2, "BLUE");
    duk_push_uint(ctx, 0xFFE0);  // RGB565 (convencao JS)  duk_put_prop_string(ctx, -2, "YELLOW");
    duk_push_uint(ctx, 0x07FF);  // RGB565 (convencao JS)    duk_put_prop_string(ctx, -2, "CYAN");
    duk_push_uint(ctx, 0xF81F);  // RGB565 (convencao JS) duk_put_prop_string(ctx, -2, "MAGENTA");
    duk_push_uint(ctx, 0xFDA0);  // RGB565 (convencao JS)  duk_put_prop_string(ctx, -2, "ORANGE");
    duk_push_uint(ctx, 0x7BEF);  // RGB565 (convencao JS)duk_put_prop_string(ctx, -2, "DARKGREY");

    duk_pop(ctx); // pop global object
}
