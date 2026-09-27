#ifndef KRYONOS_ICON_H
#define KRYONOS_ICON_H

#include "Boards/Board.h"

// ============================================================================
// Pacote de icones 64x64 do KryonOS.
//
// Os .bin ficam em /local/icons/ na LittleFS (gerados por tools/make_icons.py
// a partir de artes do text2d/FLUX.2). Icon::draw() carrega cada icone sob
// demanda (PSRAM quando disponivel).
//
// Formato do .bin:
//   v2 (10240 bytes): RGB565 LE [64*64*2] + mascara alpha 4-bit [64*64/2]
//                     (2 pixels por byte, nibble alto = pixel da esquerda;
//                     0 = transparente, 15 = opaco). draw() faz o blend por
//                     software sobre o conteudo do display (readRect).
//   v1 (8192 bytes):  RGB565 LE opaco; blit direto com pushImage.
// O tamanho do arquivo decide a versao — data antiga segue funcionando.
// ============================================================================

class Icon {
public:
    static constexpr int SIZE = 64;

    // Blit 64x64 do icone nomeado em (x, y), compondo o alpha sobre o fundo
    // atual (v2). Icone ausente desenha tile neutro.
    static void draw(lgfx::LGFXBase* tft, const char* name, int x, int y);

    // O icone existe (e foi carregado em cache)? Sem desenhar fallback.
    static bool available(const char* name);

    // Tile de app do usuario: quadrado arredondado na cor derivada do nome +
    // letra inicial. Desenhado em runtime (nao usa asset).
    static void drawAppTile(lgfx::LGFXBase* tft, const char* appName, int x, int y);

    // Cor de acento derivada do nome (formato nativo do alvo)
    static uint32_t appTileColor(const char* appName);

private:
    static constexpr int COUNT = 11;
    static const char* NAMES[COUNT];
    static uint16_t* cache[COUNT];   // pixels RGB565
    static uint8_t* alpha[COUNT];    // mascara A4 (v2) ou nullptr (v1)
    static int index(const char* name);
    static uint16_t* load(const char* name, uint8_t** alphaOut);
    static void drawAlpha(lgfx::LGFXBase* tft, const uint16_t* px, const uint8_t* a4, int x, int y);
    static void drawFallback(lgfx::LGFXBase* tft, int x, int y);
};

#endif  // KRYONOS_ICON_H
