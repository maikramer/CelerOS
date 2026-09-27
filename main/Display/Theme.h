#ifndef KRYONOS_THEME_H
#define KRYONOS_THEME_H

// ============================================================================
// Paleta visual do KryonOS (tema escuro moderno).
// Valores RGB888 — o LovyanGFX converte para o formato nativo do painel
// (RGB565 ou superior) automaticamente em qualquer placa.
// ============================================================================

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

#endif  // KRYONOS_THEME_H
