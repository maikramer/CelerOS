#ifndef KRYONOS_THEME_H
#define KRYONOS_THEME_H

// ============================================================================
// Paleta visual do KryonOS (tema escuro azul-ardosia).
//
// Valores RGB888, mas ESCOLHIDOS JA QUANTIZADOS para RGB565 (R/B multiplos
// de 8, G multiplo de 4): o painel mostra exatamente a cor declarada. Cinzas
// escuros arbitrarios viram verde-petroleo no 565 (o verde tem 1 bit a mais),
// por isso os neutros aqui puxam levemente para o azul.
// ============================================================================

#define THEME_BG        0x080C18  // fundo geral
#define THEME_CARD      0x121A2C  // cards / listas / superficies
#define THEME_RAISED    0x1C2640  // superficie elevada / estado pressionado
#define THEME_STROKE    0x2C3850  // contornos suaves
#define THEME_ACCENT    0x38BCF8  // azul-ceu de destaque
#define THEME_ACCENT_D  0x0C4870  // destaque escuro (selecao, barras)
#define THEME_ON_ACCENT 0x081020  // texto sobre THEME_ACCENT
#define THEME_TEXT      0xF0F4F8  // texto principal
#define THEME_TEXT_DIM  0x8894A8  // texto secundario
#define THEME_OK        0x20C864
#define THEME_WARN      0xF8A010
#define THEME_ERR       0xF04848

#endif  // KRYONOS_THEME_H
