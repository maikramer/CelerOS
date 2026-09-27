#ifndef KRYONOS_THEME_H
#define KRYONOS_THEME_H

// ============================================================================
// Paleta visual do KryonOS (tema escuro moderno).
// Valores RGB888 — no alvo LovyanGFX inteiros sao interpretados como RGB888;
// no alvo TFT_eSPI (RGB565) a macro abaixo reduz para 16 bits.
// ============================================================================

#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN

#define THEME_BG        0x0D1117  // fundo geral
#define THEME_CARD      0x161D27  // cards / listas
#define THEME_STROKE    0x2A3441  // contornos suaves
#define THEME_ACCENT    0x22D3EE  // ciano de destaque
#define THEME_ACCENT_D  0x0E7490  // ciano escuro (barras)
#define THEME_TEXT      0xF1F5F9  // texto principal
#define THEME_TEXT_DIM  0x8B98A9  // texto secundario
#define THEME_OK        0x22C55E
#define THEME_WARN      0xF59E0B
#define THEME_ERR       0xEF4444

#else  // TFT_eSPI: converte RGB888 -> RGB565 em tempo de compilacao

#define THEME_BG        (((0x0D & 0xF8) << 8) | ((0x11 & 0xFC) << 3) | (0x17 >> 3))
#define THEME_CARD      (((0x16 & 0xF8) << 8) | ((0x1D & 0xFC) << 3) | (0x27 >> 3))
#define THEME_STROKE    (((0x2A & 0xF8) << 8) | ((0x34 & 0xFC) << 3) | (0x41 >> 3))
#define THEME_ACCENT    (((0x22 & 0xF8) << 8) | ((0xD3 & 0xFC) << 3) | (0xEE >> 3))
#define THEME_ACCENT_D  (((0x0E & 0xF8) << 8) | ((0x74 & 0xFC) << 3) | (0x90 >> 3))
#define THEME_TEXT      (((0xF1 & 0xF8) << 8) | ((0xF5 & 0xFC) << 3) | (0xF9 >> 3))
#define THEME_TEXT_DIM  (((0x8B & 0xF8) << 8) | ((0x98 & 0xFC) << 3) | (0xA9 >> 3))
#define THEME_OK        (((0x22 & 0xF8) << 8) | ((0xC5 & 0xFC) << 3) | (0x5E >> 3))
#define THEME_WARN      (((0xF5 & 0xF8) << 8) | ((0x9E & 0xFC) << 3) | (0x0B >> 3))
#define THEME_ERR       (((0xEF & 0xF8) << 8) | ((0x44 & 0xFC) << 3) | (0x44 >> 3))

#endif

#endif  // KRYONOS_THEME_H
