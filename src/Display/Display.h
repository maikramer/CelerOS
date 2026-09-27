#ifndef KRYONOS_DISPLAY_H
#define KRYONOS_DISPLAY_H

#include <Arduino.h>

// ============================================================================
// Camada de abstracao de display do KryonOS.
//
// Todos os modulos de UI usam os tipos KryonDisplay / KryonSprite e a API
// herdada do KryonDisplay (drawString com fonte numerada, textWidth, getTouch,
// datums *_DATUM, cores TFT_*). A implementacao e escolhida por placa:
//
//   - Placa classica (ESP32 + ILI9341 SPI): alias direto para KryonDisplay.
//   - Guition ESP32-S3-4848S040 "SmartDisplay ESP32-S3 4.0 inch"
//     (480x480 ST7701 RGB + touch GT911): LovyanGFX + shims de compat.
//     Nesse alvo as cores TFT_* sao RGB888 (inteiros sao interpretados como
//     RGB888 pelo LovyanGFX) e o touch e capacitivo (nao requer calibracao).
// ============================================================================

#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN

#define KRYONOS_TOUCH_CAPACITIVE 1

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// ----------------------------------------------------------------------------
// Datums no valor numerico do LovyanGFX (diferem do KryonDisplay a partir de ML).
// ----------------------------------------------------------------------------
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 4
#define MC_DATUM 5
#define MR_DATUM 6
#define BL_DATUM 8
#define BC_DATUM 9
#define BR_DATUM 10

// ----------------------------------------------------------------------------
// Cores no padrao RGB888 (o LovyanGFX interpreta inteiros como RGB888).
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// Display da Guition ESP32-S3-4848S040 (SmartDisplay ESP32-S3 4.0").
//
// Painel 4" IPS 480x480, ST7701 em RGB paralelo 16-bit; init do painel via
// SPI 3-wire (CS=39, SCK=48, MOSI=47); backlight em GPIO38 (PWM); touch
// capacitivo GT911 (I2C SDA=19, SCL=45, addr 0x5D).
// Ordem dos pin_data = D0..D15 = B0..B4, G0..G5, R0..R4 (little-endian).
// ----------------------------------------------------------------------------
// O init do fabricante (embutido na lib) seleciona RGB666 (0x3A=0x60), mas o
// framebuffer do LCD_CAM e 16-bit (RGB565) — com 0x60 as cores saem corrompidas.
// Este painel reaproveita o init do fabricante e corrige o formato de cor.
class KryonPanel : public lgfx::Panel_ST7701_guition_esp32_4848S040 {
public:
    bool init(bool use_reset) override {
        if (!lgfx::Panel_ST7701_guition_esp32_4848S040::init(use_reset)) return false;
        static constexpr const uint8_t rgb565_patch[] = { 0x3A, 1, 0x50, 0xFF, 0xFF };
        command_list(rgb565_patch);
        return true;
    }
};

class KryonGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB     _bus;
    KryonPanel        _panel;
    lgfx::Light_PWM   _light;
    lgfx::Touch_GT911 _touch;

    KryonGFX(void) {
        {
            auto cfg = _panel.config();
            cfg.memory_width  = 480;
            cfg.memory_height = 480;
            cfg.panel_width   = 480;
            cfg.panel_height  = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel.config(cfg);
        }
        {
            auto cfg = _panel.config_detail();
            cfg.pin_cs   = 39;  // SPI 3-wire do init do painel (bit-bang)
            cfg.pin_sclk = 48;
            cfg.pin_mosi = 47;
            cfg.use_psram = 1;  // framebuffer na PSRAM
            _panel.config_detail(cfg);
        }
        {
            auto cfg = _bus.config();
            cfg.panel = &_panel;
            // D0..D15 = B0..B4, G0..G5, R0..R4 (little-endian do LCD_CAM)
            cfg.pin_d0  = 4;   // B0
            cfg.pin_d1  = 5;   // B1
            cfg.pin_d2  = 6;   // B2
            cfg.pin_d3  = 7;   // B3
            cfg.pin_d4  = 15;  // B4
            cfg.pin_d5  = 8;   // G0
            cfg.pin_d6  = 20;  // G1
            cfg.pin_d7  = 3;   // G2
            cfg.pin_d8  = 46;  // G3
            cfg.pin_d9  = 9;   // G4
            cfg.pin_d10 = 10;  // G5
            cfg.pin_d11 = 11;  // R0
            cfg.pin_d12 = 12;  // R1
            cfg.pin_d13 = 13;  // R2
            cfg.pin_d14 = 14;  // R3
            cfg.pin_d15 = 0;   // R4
            cfg.pin_henable = 18;  // DE
            cfg.pin_vsync   = 17;
            cfg.pin_hsync   = 16;
            cfg.pin_pclk    = 21;
            cfg.freq_write  = 12000000;  // 12 MHz (mais alto causa flicker neste painel)
            cfg.hsync_polarity    = 1;
            cfg.hsync_front_porch = 10;
            cfg.hsync_pulse_width = 8;
            cfg.hsync_back_porch  = 50;
            cfg.vsync_polarity    = 1;
            cfg.vsync_front_porch = 10;
            cfg.vsync_pulse_width = 8;
            cfg.vsync_back_porch  = 20;
            cfg.pclk_active_neg = 0;
            cfg.pclk_idle_high  = 0;
            _bus.config(cfg);
        }
        _panel.setBus(&_bus);

        {
            auto cfg = _light.config();
            cfg.pin_bl = 38;
            cfg.invert = false;
            _light.config(cfg);
        }
        _panel.light(&_light);

        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = 479;
            cfg.y_min = 0;
            cfg.y_max = 479;
            cfg.pin_int = -1;   // INT nao conectado na placa
            cfg.pin_rst = -1;   // RST nao conectado na placa
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.pin_sda = 19;
            cfg.pin_scl = 45;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x5D;  // (0x5D ou 0x14)
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }

    // --- Compatibilidade TFT_eSPI -------------------------------------------
    using lgfx::LovyanGFX::textWidth;
    using lgfx::LovyanGFX::drawString;

    // textWidth(str, font) com fonte numerada (usado pelo marquee do HelpCenter)
    int16_t textWidth(const char* string, uint8_t font) {
        return (int16_t)lgfx::LovyanGFX::textWidth(string, lgfx::fontdata[font]);
    }
    int16_t textWidth(const String& string, uint8_t font) {
        return (int16_t)lgfx::LovyanGFX::textWidth(string.c_str(), lgfx::fontdata[font]);
    }

    // Touch capacitivo: calibracao do XPT2046 nao se aplica
    void setTouch(uint16_t* calData) { (void)calData; }
    void calibrateTouch(uint16_t* calData, uint32_t color, uint32_t bg, uint8_t size) {
        (void)calData; (void)color; (void)bg; (void)size;
    }
};

using KryonDisplay = KryonGFX;
using KryonSprite  = lgfx::LGFX_Sprite;

#else  // !KRYONOS_BOARD_SMARTDISPLAY_4IN — alvo classico ESP32 + ILI9341

#include <TFT_eSPI.h>

using KryonDisplay = TFT_eSPI;
using KryonSprite  = TFT_eSprite;

#endif  // KRYONOS_BOARD_SMARTDISPLAY_4IN

// Converte RGB888 (canais 0-255) para o formato de cor nativo do alvo.
// TFT_eSPI usa RGB565; LovyanGFX interpreta inteiros como RGB888.
inline uint32_t KryonColorRGB(int r, int g, int b) {
#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
#else
    return ((uint32_t)(r & 0xF8) << 8) | ((uint32_t)(g & 0xFC) << 3) | ((uint32_t)b >> 3);
#endif
}

#endif  // KRYONOS_DISPLAY_H
