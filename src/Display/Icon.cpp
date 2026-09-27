#include "Icon.h"
#include "Theme.h"
#include "Layout.h"
#include <LittleFS.h>

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
    String path = String("/icons/") + name + ".bin";
    // fs:: qualificado: o TFT_eSPI define FS_NO_GLOBALS (SMOOTH_FONT), o que
    // esconde o "using fs::File" quando o FS.h entra primeiro pela cadeia do
    // display
    fs::File f = LittleFS.open(path, "r");
    if (!f || f.size() < SZ) {
        if (f) f.close();
        return nullptr;
    }
#ifdef BOARD_HAS_PSRAM
    uint16_t* buf = (uint16_t*)ps_malloc(SZ);
#else
    uint16_t* buf = (uint16_t*)malloc(SZ);
#endif
    if (!buf) {
        if (f) f.close();
        return nullptr;
    }
    if (f.read((uint8_t*)buf, SZ) != SZ) {
        free(buf);
        f.close();
        return nullptr;
    }
    f.close();
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
    // Paleta de acentos coerente com o tema
#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN
    static const uint32_t palette[] = {
        0x3B82F6, 0x8B5CF6, 0xEC4899, 0xF59E0B,
        0x10B981, 0x06B6D4, 0x6366F1, 0xEF4444,
    };
#else
    static const uint32_t palette[] = {
        (((0x3B & 0xF8) << 8) | ((0x82 & 0xFC) << 3) | (0xF6 >> 3)),
        (((0x8B & 0xF8) << 8) | ((0x5C & 0xFC) << 3) | (0xF6 >> 3)),
        (((0xEC & 0xF8) << 8) | ((0x48 & 0xFC) << 3) | (0x99 >> 3)),
        (((0xF5 & 0xF8) << 8) | ((0x9E & 0xFC) << 3) | (0x0B >> 3)),
        (((0x10 & 0xF8) << 8) | ((0xB9 & 0xFC) << 3) | (0x81 >> 3)),
        (((0x06 & 0xF8) << 8) | ((0xB6 & 0xFC) << 3) | (0xD4 >> 3)),
        (((0x63 & 0xF8) << 8) | ((0x6F & 0xFC) << 3) | (0xF1 >> 3)),
        (((0xEF & 0xF8) << 8) | ((0x44 & 0xFC) << 3) | (0x44 >> 3)),
    };
#endif
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
    tft->drawString(String(letter), x + SIZE / 2, y + SIZE / 2, UI::big ? 6 : 4);
}

void Icon::drawFallback(KryonDisplay* tft, int x, int y) {
    tft->fillRoundRect(x, y, SIZE, SIZE, 14, THEME_CARD);
    tft->drawRoundRect(x, y, SIZE, SIZE, 14, THEME_STROKE);
    tft->setTextColor(THEME_TEXT_DIM, THEME_CARD);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("?", x + SIZE / 2, y + SIZE / 2, UI::big ? 4 : 2);
}
