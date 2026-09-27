#ifndef KRYONOS_DISPLAY_H
#define KRYONOS_DISPLAY_H

#include <Arduino.h>
#include <string>

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

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN

#define KRYONOS_TOUCH_CAPACITIVE 1

#include "Touch_GT911_IDF.h"
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// ----------------------------------------------------------------------------
// Datums no valor numerico do LovyanGFX (diferem do TFT_eSPI a partir de ML).
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
    lgfx::Touch_GT911_IDF _touch;  // driver i2c_master oficial (LL do LovyanGFX falha no IDF 6.1)

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
    int16_t textWidth(const std::string& string, uint8_t font) {
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

#else  // !KRYONOS_BOARD_SMARTDISPLAY_4IN - Cheap Yellow Display (ESP32 + ILI9341 + XPT2046)

#include <lgfx/v1/panel/Panel_ILI9341.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#include <lgfx/v1/touch/Touch_XPT2046.hpp>

// ----------------------------------------------------------------------------
// Cores no padrao RGB565 (painel SPI 16-bit interpreta inteiros como RGB565).
// ----------------------------------------------------------------------------
#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xC618
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFDA0
#define TFT_GREENYELLOW 0xB7E0
#define TFT_PINK        0xFC9F

// ----------------------------------------------------------------------------
// Cheap Yellow Display (ESP32-2432S028R e afins): ILI9341 240x320 no VSPI
// (SCK=18, MOSI=23, MISO=19, CS=17, DC=16, RST=5), touch resistivo XPT2046
// compartilhando o barramento (CS=21) e backlight em GPIO22.
// Pinos identicos ao env esp32doit-devkit-v1 do platformio.ini original.
// ----------------------------------------------------------------------------
class KryonGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_SPI       _bus;
    lgfx::Panel_ILI9341 _panel;
    lgfx::Light_PWM     _light;
    lgfx::Touch_XPT2046 _touch;

    KryonGFX(void) {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI3_HOST;  // VSPI
            cfg.freq_write = 27000000;   // 27MHz (valor validado no env original)
            cfg.pin_sclk = 18;
            cfg.pin_mosi = 23;
            cfg.pin_miso = 19;
            cfg.pin_dc = 16;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 17;
            cfg.pin_rst = 5;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 22;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            // Faixa bruta de fabrica; a calibracao interativa (calibrateTouch)
            // refina por dispositivo e persiste em /local/touch_cal_p.bin
            cfg.x_min = 300;
            cfg.x_max = 3900;
            cfg.y_min = 200;
            cfg.y_max = 3700;
            cfg.pin_int = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host = SPI3_HOST;  // VSPI
            cfg.freq = 2500000;
            cfg.pin_sclk = 18;
            cfg.pin_mosi = 23;
            cfg.pin_miso = 19;
            cfg.pin_cs = 21;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }

    // --- Compatibilidade TFT_eSPI -------------------------------------------
    using lgfx::LovyanGFX::textWidth;
    using lgfx::LovyanGFX::drawString;

    int16_t textWidth(const char* string, uint8_t font) {
        return (int16_t)lgfx::LovyanGFX::textWidth(string, lgfx::fontdata[font]);
    }
    int16_t textWidth(const std::string& string, uint8_t font) {
        return (int16_t)lgfx::LovyanGFX::textWidth(string.c_str(), lgfx::fontdata[font]);
    }

    // Calibracao no formato proprio [x_min, x_max, y_min, y_max, 0],
    // persistida pelo TouchCalibrator em /local/touch_cal_p.bin.
    void setTouch(uint16_t* calData) {
        auto cfg = _touch.config();
        cfg.x_min = calData[0];
        cfg.x_max = calData[1];
        cfg.y_min = calData[2];
        cfg.y_max = calData[3];
        _touch.config(cfg);
    }

    // Calibracao interativa de 2 pontos (cantos opostos): le o raw do
    // XPT2046 em duas marcas e deriva a faixa de mapeamento.
    void calibrateTouch(uint16_t* calData, uint32_t color, uint32_t bg, uint8_t size) {
        (void)size;
        static constexpr int MARGIN_DIV = 8;
        static constexpr int SAMPLES = 8;

        int32_t w = width();
        int32_t h = height();
        lgfx::touch_point_t tp;

        fillScreen(bg);
        setTextDatum(MC_DATUM);
        setTextColor(color, bg);

        lgfx::touch_point_t pts[2];
        const int32_t pos[2][2] = {
            { w / MARGIN_DIV,     h / MARGIN_DIV     },
            { w - w / MARGIN_DIV, h - h / MARGIN_DIV },
        };

        for (int i = 0; i < 2; i++) {
            drawLine(pos[i][0] - 10, pos[i][1], pos[i][0] + 10, pos[i][1], color);
            drawLine(pos[i][0], pos[i][1] - 10, pos[i][0], pos[i][1] + 10, color);
            drawString("touch the crosshair", w / 2, h - 16, 2);

            // espera soltar
            do { delay(20); } while (getTouchRaw(&tp, 1));

            // acumula amostras
            int32_t sx = 0, sy = 0;
            int valid = 0;
            for (int s = 0; s < SAMPLES; ) {
                if (getTouchRaw(&tp, 1)) {
                    sx += tp.x;
                    sy += tp.y;
                    valid++;
                    s++;
                }
                delay(10);
            }
            pts[i].x = sx / valid;
            pts[i].y = sy / valid;
            delay(300);
        }

        calData[0] = (uint16_t)pts[0].x;  // x_min (top-left)
        calData[1] = (uint16_t)pts[1].x;  // x_max (bottom-right)
        calData[2] = (uint16_t)pts[0].y;  // y_min
        calData[3] = (uint16_t)pts[1].y;  // y_max
        calData[4] = 0;
    }
};

using KryonDisplay = KryonGFX;
using KryonSprite  = lgfx::LGFX_Sprite;

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
