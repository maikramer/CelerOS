#include "Icon.h"
#include "Theme.h"
#include "Layout.h"
#include <string>
#include <cstdio>
#include <cstdlib>
#include "esp_heap_caps.h"

// Registro fixo — mesmos ids do tools/icons.json
const char* Icon::NAMES[Icon::COUNT] = {
    "appstore", "installer", "settings", "help", "web",
    "time", "about", "update", "app", "wifi_on", "wifi_off",
};
uint16_t* Icon::cache[Icon::COUNT] = {nullptr};
uint8_t* Icon::alpha[Icon::COUNT] = {nullptr};

int Icon::index(const char* name) {
    for (int i = 0; i < COUNT; i++) {
        if (strcmp(name, NAMES[i]) == 0) return i;
    }
    return -1;
}

uint16_t* Icon::load(const char* name, uint8_t** alphaOut) {
    constexpr size_t PX = (size_t)SIZE * SIZE;
    constexpr size_t V1_SZ = PX * 2;           // RGB565 opaco
    constexpr size_t V2_SZ = PX * 2 + PX / 2;  // + mascara A4
    std::string path = std::string("/local/icons/") + name + ".bin";
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return nullptr;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < (long)V1_SZ) {
        fclose(f);
        return nullptr;
    }
    bool hasAlpha = size >= (long)V2_SZ;

    // PSRAM quando disponivel (framebuffer grande), senao heap interno
    uint16_t* buf = (uint16_t*)heap_caps_malloc(PX * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        buf = (uint16_t*)malloc(PX * 2);
    }
    uint8_t* a = nullptr;
    if (hasAlpha) {
        a = (uint8_t*)heap_caps_malloc(PX / 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (a == nullptr) a = (uint8_t*)malloc(PX / 2);
    }
    if (buf == nullptr || (hasAlpha && a == nullptr) ||
        fread(buf, 1, PX * 2, f) != PX * 2 ||
        (hasAlpha && fread(a, 1, PX / 2, f) != PX / 2)) {
        free(buf);
        free(a);
        fclose(f);
        return nullptr;
    }
    fclose(f);
    *alphaOut = a;
    return buf;
}

bool Icon::available(const char* name) {
    int i = index(name);
    if (i < 0) return false;
    if (!cache[i]) cache[i] = load(name, &alpha[i]);
    return cache[i] != nullptr;
}

void Icon::drawAlpha(lgfx::LGFXBase* tft, const uint16_t* px, const uint8_t* a4, int x, int y) {
    // Blend por software: le o fundo, compoe o alpha e devolve em um blit
    // unico (sem flicker). So a parte visivel e processada — o Canvas em
    // faixas desenha icones cortados na borda de cada faixa.
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + SIZE > tft->width() ? tft->width() : x + SIZE;
    int y1 = y + SIZE > tft->height() ? tft->height() : y + SIZE;
    if (x0 >= x1 || y0 >= y1) return;
    const int vw = x1 - x0, vh = y1 - y0;

    static uint16_t* dst = nullptr;  // scratch de um usuario: loop de UI unico
    if (dst == nullptr) {
        dst = (uint16_t*)heap_caps_malloc((size_t)SIZE * SIZE * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (dst == nullptr) dst = (uint16_t*)malloc((size_t)SIZE * SIZE * 2);
    }
    if (dst == nullptr) {  // sem scratch: melhor opaco que nao desenhar
        tft->pushImage(x, y, SIZE, SIZE, px);
        return;
    }

    tft->readRect(x0, y0, vw, vh, dst);
    for (int row = 0; row < vh; row++) {
        const int sp0 = (y0 - y + row) * SIZE + (x0 - x);
        uint16_t* d = dst + row * vw;
        for (int col = 0; col < vw; col++) {
            const int p = sp0 + col;
            uint32_t a = (p & 1) ? (a4[p >> 1] & 0x0F) : (a4[p >> 1] >> 4);
            if (a == 0) continue;                        // transparente: mantem o fundo
            if (a == 15) { d[col] = px[p]; continue; }   // opaco
            uint32_t s = px[p], b = d[col];
            uint32_t ia = 15 - a;
            // cada canal na propria escala (R/B 5 bits, G 6 bits); peso /15
            uint32_t r = ((s >> 11) * a + (b >> 11) * ia + 7) / 15;
            uint32_t g = (((s >> 5) & 0x3F) * a + ((b >> 5) & 0x3F) * ia + 7) / 15;
            uint32_t bl = ((s & 0x1F) * a + (b & 0x1F) * ia + 7) / 15;
            d[col] = (uint16_t)((r << 11) | (g << 5) | bl);
        }
    }
    tft->pushImage(x0, y0, vw, vh, dst);
}

void Icon::draw(lgfx::LGFXBase* tft, const char* name, int x, int y) {
    if (!tft) return;
    int i = index(name);
    if (i >= 0) {
        if (!cache[i]) cache[i] = load(name, &alpha[i]);
        if (cache[i]) {
            if (alpha[i]) drawAlpha(tft, cache[i], alpha[i], x, y);
            else tft->pushImage(x, y, SIZE, SIZE, cache[i]);
            return;
        }
    }
    drawFallback(tft, x, y);
}

uint32_t Icon::appTileColor(const char* appName) {
    // Paleta de acentos coerente com o tema (RGB888; o painel converte)
    // (tons ja quantizados para RGB565 — ver Theme.h)
    static const uint32_t palette[] = {
        0x3880F0, 0x885CF0, 0xE04890, 0xE89010,
        0x10A878, 0x08A8C8, 0x5860E8, 0xE04848,
    };
    uint32_t h = 0;
    for (const char* p = appName; p && *p; p++) h = h * 31 + (uint8_t)*p;
    return palette[h % 8];
}

uint32_t Icon::mix(uint32_t a, uint32_t b, int t256) {
    auto ch = [&](int sh) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        return (uint32_t)((ca * (256 - t256) + cb * t256) >> 8) << sh;
    };
    return ch(16) | ch(8) | ch(0);
}

void Icon::fillGradientRoundRect(lgfx::LGFXBase* tft, int x, int y, int w, int h, int r,
                                 uint32_t top, uint32_t bottom) {
    if (!tft || w <= 0 || h <= 0) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int i = 0; i < h; i++) {
        // recuo horizontal da linha nos cantos arredondados
        int inset = 0;
        int d = (i < r) ? r - i : (i >= h - r ? i - (h - r - 1) : 0);
        if (d > 0) {
            int e = r * r - d * d;
            int s = 0;
            while ((s + 1) * (s + 1) <= e) s++;  // isqrt (r <= ~20: barato)
            inset = r - s;
        }
        tft->drawFastHLine(x + inset, y + i, w - 2 * inset, mix(top, bottom, h > 1 ? i * 256 / (h - 1) : 0));
    }
}

void Icon::drawAppTile(lgfx::LGFXBase* tft, const char* appName, int x, int y) {
    if (!tft || !appName) return;
    // Tile com gradiente vertical sutil (topo mais claro) + inicial em negrito
    uint32_t base = appTileColor(appName);
    fillGradientRoundRect(tft, x, y, SIZE, SIZE, 16, mix(base, 0xFFFFFF, 48), mix(base, 0x000000, 40));
    char letter = 'A';
    for (const char* p = appName; p && *p; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            letter = (c >= 'a' && c <= 'z') ? c - 32 : c;
            break;
        }
    }
    char s[2] = {letter, 0};
    tft->setTextColor(0xFFFFFF);  // transparente sobre o gradiente
    tft->setTextDatum(MC_DATUM);
    tft->drawString(s, x + SIZE / 2, y + SIZE / 2 + 1, &lgfx::fonts::FreeSansBold18pt7b);
}

void Icon::drawFallback(lgfx::LGFXBase* tft, int x, int y) {
    tft->fillRoundRect(x, y, SIZE, SIZE, 16, THEME_CARD);
    tft->drawRoundRect(x, y, SIZE, SIZE, 16, THEME_STROKE);
    tft->setTextColor(THEME_TEXT_DIM);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("?", x + SIZE / 2, y + SIZE / 2, &lgfx::fonts::FreeSansBold18pt7b);
}
