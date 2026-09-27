#ifndef KRYONOS_LEGACY_PALETTE_H
#define KRYONOS_LEGACY_PALETTE_H

// ============================================================================
// Paleta das telas antigas (pre-Kui) remapeada para o tema.
//
// As telas legadas foram escritas com as cores cruas do TFT_eSPI (preto puro,
// branco puro, verde/azul/vermelho saturados) — visual destoante do launcher.
// Incluir ESTE header por ULTIMO num .cpp de tela antiga troca os TFT_* por
// tons do tema so naquela unidade de compilacao; apps JS e o kernel seguem
// com a paleta padrao do Display.h.
//
// Semantica preservada: verde era "destaque/primario" nas telas antigas
// (titulos, botoes de acao) e vira o acento do tema; texto sobre ele
// (TFT_BLACK) vira o fundo escuro, com bom contraste.
// ============================================================================

#include "Display.h"
#include "Theme.h"

#undef TFT_BLACK
#undef TFT_WHITE
#undef TFT_GREEN
#undef TFT_RED
#undef TFT_DARKGREY
#undef TFT_LIGHTGREY
#undef TFT_BLUE
#undef TFT_ORANGE
#undef TFT_YELLOW
#undef TFT_CYAN
#undef TFT_DARKCYAN
#undef TFT_PURPLE
#undef TFT_NAVY
#undef TFT_DARKGREEN
#undef TFT_MAROON
#undef TFT_MAGENTA

#define TFT_BLACK     THEME_BG
#define TFT_WHITE     THEME_TEXT
#define TFT_GREEN     THEME_ACCENT
#define TFT_RED       THEME_ERR
#define TFT_DARKGREY  THEME_RAISED
#define TFT_LIGHTGREY THEME_TEXT_DIM
#define TFT_BLUE      THEME_ACCENT_D
#define TFT_ORANGE    THEME_WARN
#define TFT_YELLOW    0xF8D050
#define TFT_CYAN      THEME_ACCENT
#define TFT_DARKCYAN  THEME_ACCENT_D
#define TFT_PURPLE    0x8C5CF8
#define TFT_NAVY      THEME_CARD
#define TFT_DARKGREEN 0x146C3C
#define TFT_MAROON    0x702028
#define TFT_MAGENTA   0xE84898

#endif  // KRYONOS_LEGACY_PALETTE_H
