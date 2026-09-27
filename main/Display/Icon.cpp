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
    // unico (sem flicker). Fora dos limites cai no blit opaco (nao ocorre
    // nos layouts atuais).
    if (x < 0 || y < 0 || x + SIZE > tft->width() || y + SIZE > tft->height()) {
        tft->pushImage(x, y, SIZE, SIZE, px);
        return;
    }
    static uint16_t* dst = nullptr;  // scratch de um usuario: loop de UI unico
    if (dst == nullptr) {
        dst = (uint16_t*)heap_caps_malloc((size_t)SIZE * SIZE * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (dst == nullptr) dst = (uint16_t*)malloc((size_t)SIZE * SIZE * 2);
    }
    if (dst == nullptr) {  // sem scratch: melhor opaco que nao desenhar
        tft->pushImage(x, y, SIZE, SIZE, px);
        return;
    }

    tft->readRect(x, y, SIZE, SIZE, dst);
    constexpr int PX = SIZE * SIZE;
    for (int p = 0; p < PX; p++) {
        uint8_t a = (p & 1) ? (a4[p >> 1] & 0x0F) : (a4[p >> 1] >> 4);
        if (a == 0) continue;                    // transparente: mantem o fundo
        if (a == 15) { dst[p] = px[p]; continue; }  // opaco
        uint16_t s = px[p], d = dst[p];
        uint32_t sr = s >> 11, sg = (s >> 5) & 0x3F, sb = s & 0x1F;
        uint32_t dr = d >> 11, dg = (d >> 5) & 0x3F, db = d & 0x1F;
        uint32_t ia = 15 - a;
        dst[p] = (uint16_t)((((sr * a + dr * ia + 7) / 15) << 11) |
                            (((sg * a + dg * ia + 15) / 31) << 5) |
                            ((sb * a + db * ia + 7) / 15));
    }
    tft->pushImage(x, y, SIZE, SIZE, dst);
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
    static const uint32_t palette[] = {
        0x3B82F6, 0x8B5CF6, 0xEC4899, 0xF59E0B,
        0x10B981, 0x06B6D4, 0x6366F1, 0xEF4444,
    };
    uint32_t h = 0;
    for (const char* p = appName; p && *p; p++) h = h * 31 + (uint8_t)*p;
    return palette[h % 8];
}

void Icon::drawAppTile(lgfx::LGFXBase* tft, const char* appName, int x, int y) {
    if (!tft || !appName) return;
    tft->fillRoundRect(x, y, SIZE, SIZE, 14, appTileColor(appName));
    char letter = 'A';
    for (const char* p = appName; p && *p; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            letter = (c >= 'a' && c <= 'z') ? c - 32 : c;
            break;
        }
    }
    tft->setTextColor(THEME_TEXT, appTileColor(appName));
    tft->setTextDatum(MC_DATUM);
    tft->drawString(std::string(1, letter).c_str(), x + SIZE / 2, y + SIZE / 2, UI::big ? 6 : 4);
}

void Icon::drawFallback(lgfx::LGFXBase* tft, int x, int y) {
    tft->fillRoundRect(x, y, SIZE, SIZE, 14, THEME_CARD);
    tft->drawRoundRect(x, y, SIZE, SIZE, 14, THEME_STROKE);
    tft->setTextColor(THEME_TEXT_DIM, THEME_CARD);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("?", x + SIZE / 2, y + SIZE / 2, UI::big ? 4 : 2);
}
