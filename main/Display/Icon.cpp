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

int Icon::index(const char* name) {
    for (int i = 0; i < COUNT; i++) {
        if (strcmp(name, NAMES[i]) == 0) return i;
    }
    return -1;
}

uint16_t* Icon::load(const char* name) {
    constexpr size_t SZ = (size_t)SIZE * SIZE * 2;
    std::string path = std::string("/local/icons/") + name + ".bin";
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return nullptr;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < (long)SZ) {
        fclose(f);
        return nullptr;
    }

    // PSRAM quando disponivel (framebuffer grande), senao heap interno
    uint16_t* buf = (uint16_t*)heap_caps_malloc(SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        buf = (uint16_t*)malloc(SZ);
    }
    if (buf == nullptr) {
        fclose(f);
        return nullptr;
    }
    if (fread(buf, 1, SZ, f) != SZ) {
        free(buf);
        fclose(f);
        return nullptr;
    }
    fclose(f);
    return buf;
}

void Icon::draw(KryonDisplay* tft, const char* name, int x, int y) {
    if (!tft) return;
    int i = index(name);
    if (i >= 0) {
        if (!cache[i]) cache[i] = load(name);
        if (cache[i]) {
            tft->pushImage(x, y, SIZE, SIZE, cache[i]);
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

void Icon::drawAppTile(KryonDisplay* tft, const char* appName, int x, int y) {
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

void Icon::drawFallback(KryonDisplay* tft, int x, int y) {
    tft->fillRoundRect(x, y, SIZE, SIZE, 14, THEME_CARD);
    tft->drawRoundRect(x, y, SIZE, SIZE, 14, THEME_STROKE);
    tft->setTextColor(THEME_TEXT_DIM, THEME_CARD);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("?", x + SIZE / 2, y + SIZE / 2, UI::big ? 4 : 2);
}
