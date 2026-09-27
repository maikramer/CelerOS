#ifndef CELEROS_THEME_H
#define CELEROS_THEME_H

// ============================================================================
// Paleta visual do CelerOS (tema escuro azul-ardosia).
//
// Valores RGB888, mas ESCOLHIDOS JA QUANTIZADOS para RGB565 (R/B multiplos
// de 8, G multiplo de 4): o painel mostra exatamente a cor declarada. Cinzas
// escuros arbitrarios viram verde-petroleo no 565 (o verde tem 1 bit a mais),
// por isso os neutros aqui puxam levemente para o azul.
//
// SUFIXO u OBRIGATORIO: o LovyanGFX decide o formato da cor pelo TIPO —
// uint32_t = RGB888, mas int32_t/int16_t = RGB565 (so os 16 bits baixos).
// Um literal 0xRRGGBB sem sufixo e int e sairia com a cor errada no vidro.
// ============================================================================

#define THEME_BG        0x080C18u  // fundo geral
#define THEME_CARD      0x121A2Cu  // cards / listas / superficies
#define THEME_RAISED    0x1C2640u  // superficie elevada / estado pressionado
#define THEME_STROKE    0x2C3850u  // contornos suaves
#define THEME_ACCENT    0x38BCF8u  // azul-ceu de destaque
#define THEME_ACCENT_D  0x0C4870u  // destaque escuro (selecao, barras)
#define THEME_ON_ACCENT 0x081020u  // texto sobre THEME_ACCENT
#define THEME_TEXT      0xF0F4F8u  // texto principal
#define THEME_TEXT_DIM  0x8894A8u  // texto secundario
#define THEME_OK        0x20C864u
#define THEME_WARN      0xF8A010u
#define THEME_ERR       0xF04848u

#endif  // CELEROS_THEME_H
