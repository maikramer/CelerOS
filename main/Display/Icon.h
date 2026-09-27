#ifndef KRYONOS_ICON_H
#define KRYONOS_ICON_H

#include "Boards/Board.h"
#include <cstdio>

// ============================================================================
// Pacote de icones 64x64 do KryonOS.
//
// Os assets ficam em /local/icons/ na LittleFS (gerados por tools/make_icons.py
// a partir de artes do text2d/FLUX.2). Icon::draw() carrega cada icone sob
// demanda (PSRAM quando disponivel).
//
// Formatos aceitos (a assinatura do arquivo decide):
//   PNG 64x64 RGBA:    decodificado no load pelo pngle do LovyanGFX direto
//                      para os buffers do cache (~2,5x menor que o .bin;
//                      alpha de 8 bits vira a mascara A4). Sem transparencia
//                      -> blit opaco.
//   .bin v2 (10240 B): RGB565 LE [64*64*2] + mascara alpha 4-bit [64*64/2]
//                      (2 pixels por byte, nibble alto = pixel da esquerda;
//                      0 = transparente, 15 = opaco). draw() faz o blend por
//                      software sobre o conteudo do display (readRect).
//   .bin v1 (8192 B):  RGB565 LE opaco; blit direto com pushImage.
// Independente do formato em disco, o cache e sempre RGB565+A4 — o caminho de
// draw/blend nao conhece PNG. Em ambos os casos o nome comum resolve para
// /local/icons/<nome>.png e, se nao existir, <nome>.bin (data antiga segue
// funcionando).
// ============================================================================

class Icon {
public:
    static constexpr int SIZE = 64;

    // Blit 64x64 do icone nomeado em (x, y), compondo o alpha sobre o fundo
    // atual (v2). Icone ausente desenha tile neutro.
    static void draw(lgfx::LGFXBase* tft, const char* name, int x, int y);

    // O icone existe (e foi carregado em cache)? Sem desenhar fallback.
    static bool available(const char* name);

    // Icone de pacote de app: caminho absoluto do PNG ou .bin (ex.:
    // /local/apps/Foo/icon.png), formatos PNG/v1/v2. draw/available
    // aceitam caminho absoluto no lugar do nome. Cache proprio —
    // invalidateFileIcons() e chamado no rescan do launcher porque o app
    // pode ter sido reinstalado com outra arte.
    static void drawFile(lgfx::LGFXBase* tft, const char* path, int x, int y);
    static bool availableFile(const char* path);
    static void invalidateFileIcons();

    // Tile de app do usuario: quadrado arredondado na cor derivada do nome +
    // letra inicial. Desenhado em runtime (nao usa asset).
    static void drawAppTile(lgfx::LGFXBase* tft, const char* appName, int x, int y);

    // Cor de acento derivada do nome (RGB888)
    static uint32_t appTileColor(const char* appName);

    // Mistura RGB888: t256 = 0 -> a, 256 -> b
    static uint32_t mix(uint32_t a, uint32_t b, int t256);

    // Retangulo arredondado com gradiente vertical (top -> bottom), RGB888
    static void fillGradientRoundRect(lgfx::LGFXBase* tft, int x, int y, int w, int h, int r,
                                      uint32_t top, uint32_t bottom);

private:
    static constexpr int COUNT = 11;
    static const char* NAMES[COUNT];
    static uint16_t* cache[COUNT];   // pixels RGB565
    static uint8_t* alpha[COUNT];    // mascara A4 (v2) ou nullptr (v1)
    static int index(const char* name);
    static uint16_t* load(const char* name, uint8_t** alphaOut);
    static uint16_t* loadBin(FILE* f, uint8_t** alphaOut);
    static uint16_t* loadPng(FILE* f, uint8_t** alphaOut);
    static void drawAlpha(lgfx::LGFXBase* tft, const uint16_t* px, const uint8_t* a4, int x, int y);
    static void drawFallback(lgfx::LGFXBase* tft, int x, int y);
};

#endif  // KRYONOS_ICON_H
