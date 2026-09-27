#ifndef KRYONOS_DISPLAY_H
#define KRYONOS_DISPLAY_H

#include <Arduino.h>
#include <string>

// ============================================================================
// Camada comum de display do KryonOS (sem #ifdef de placa).
//
// Toda a configuracao fisica (painel/bus/touch/pinos) vive em
// main/Boards/<placa>/BoardDisplay.h, selecionada pelo CMake via include path:
// quem inclui "Boards/Board.h" recebe a classe BoardDisplay da placa do build.
//
// Tudo que e comum a todas as placas vive AQUI: datums, paleta TFT_* e a base
// KryonDisplayBase com os shims de compatibilidade TFT_eSPI.
//
// CORES: todo o codigo usa valores RGB888 (0xRRGGBB) — o LovyanGFX converte
// automaticamente para o formato nativo do painel (RGB565 ou o que for). As
// paletas por placa e as conversoes manuais foram extintas.
// ============================================================================

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Datums no valor numerico do LovyanGFX (diferem do TFT_eSPI a partir de ML).
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 4
#define MC_DATUM 5
#define MR_DATUM 6
#define BL_DATUM 8
#define BC_DATUM 9
#define BR_DATUM 10

// Paleta no padrao RGB888 (converte para o formato nativo em qualquer painel).
#define TFT_BLACK       0x000000
#define TFT_NAVY        0x000080
#define TFT_DARKGREEN   0x006400
#define TFT_DARKCYAN    0x008B8B
#define TFT_MAROON      0x800000
#define TFT_PURPLE      0x780078
#define TFT_OLIVE       0x808000
#define TFT_LIGHTGREY   0xC0C0C0
#define TFT_DARKGREY    0x606060
#define TFT_BLUE        0x0000FF
#define TFT_GREEN       0x00FF00
#define TFT_CYAN        0x00FFFF
#define TFT_RED         0xFF0000
#define TFT_MAGENTA     0xFF00FF
#define TFT_YELLOW      0xFFE000
#define TFT_WHITE       0xFFFFFF
#define TFT_ORANGE      0xFFD000
#define TFT_GREENYELLOW 0xADFF2F
#define TFT_PINK        0xFF99FF

// Base comum dos displays por placa: shims TFT_eSPI + hooks de calibracao.
// Placas com touch resistivo sobrescrevem setTouch/calibrateTouch.
class KryonDisplayBase : public lgfx::LGFX_Device {
public:
    using lgfx::LovyanGFX::textWidth;
    using lgfx::LovyanGFX::drawString;

    // textWidth(str, font) com fonte numerica (usado pelo marquee/labels)
    int16_t textWidth(const char* string, uint8_t font) {
        return (int16_t)lgfx::LovyanGFX::textWidth(string, lgfx::fontdata[font]);
    }
    int16_t textWidth(const std::string& string, uint8_t font) {
        return (int16_t)lgfx::LovyanGFX::textWidth(string.c_str(), lgfx::fontdata[font]);
    }

    virtual void setTouch(uint16_t* calData) { (void)calData; }
    virtual void calibrateTouch(uint16_t* calData, uint32_t color, uint32_t bg, uint8_t size) {
        (void)calData; (void)color; (void)bg; (void)size;
    }
};

// Converte canais 0-255 para cor RGB888 (formato unico do codigo).
inline uint32_t KryonColorRGB(int r, int g, int b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

using KryonSprite = lgfx::LGFX_Sprite;

#endif  // KRYONOS_DISPLAY_H
