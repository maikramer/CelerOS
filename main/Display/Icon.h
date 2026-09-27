#ifndef KRYONOS_ICON_H
#define KRYONOS_ICON_H

#include "Boards/Board.h"

// ============================================================================
// Pacote de icones 64x64 RGB565 do KryonOS.
//
// Os .bin ficam em /icons/ na LittleFS (gerados por tools/make_icons.py a
// partir de artes do text2d/FLUX.2). Icon::draw() carrega cada icao sob
// demanda (PSRAM quando disponivel) e faz o blit com pushImage.
// ============================================================================

class Icon {
public:
    static constexpr int SIZE = 64;

    // Blit 64x64 do icone nomeado em (x, y). Icone ausente desenha tile neutro.
    static void draw(KryonDisplay* tft, const char* name, int x, int y);

    // Tile de app do usuario: quadrado arredondado na cor derivada do nome +
    // letra inicial. Desenhado em runtime (nao usa asset).
    static void drawAppTile(KryonDisplay* tft, const char* appName, int x, int y);

    // Cor de acento derivada do nome (formato nativo do alvo)
    static uint32_t appTileColor(const char* appName);

private:
    static constexpr int COUNT = 11;
    static const char* NAMES[COUNT];
    static uint16_t* cache[COUNT];
    static int index(const char* name);
    static uint16_t* load(const char* name);
    static void drawFallback(KryonDisplay* tft, int x, int y);
};

#endif  // KRYONOS_ICON_H
