#ifndef CELER_BOARDS_CYD_DISPLAY_H
#define CELER_BOARDS_CYD_DISPLAY_H

// ----------------------------------------------------------------------------
// CYD variante VSPI (NAO TESTADA em hardware): ILI9341 240x320 no VSPI
// (SCK=18, MOSI=23, MISO=19, CS=17, DC=16, RST=5), touch resistivo XPT2046
// compartilhando o barramento (CS=21) e backlight em GPIO22.
//
// Este era o pinout do target "cyd" original, herdado do env
// esp32doit-devkit-v1 do platformio.ini do fork — na placa CLASSICA
// (ESP32-2432S028R witnessmenow) esses pinos nao ligam a nada (tela branca).
// A partir de 2026-09 o target "cyd" e a variante classica (HSPI + touch
// dedicado); esta fica como "cyd-vspi" para quem tiver a placa com este
// pinout. NAO FOI TESTADA: se for a sua placa e nao desenhar, abra issue
// com o pinout.
// ----------------------------------------------------------------------------

#include "../../Display/Display.h"

#include <lgfx/v1/panel/Panel_ILI9341.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#include <lgfx/v1/touch/Touch_XPT2046.hpp>

class BoardDisplay : public CelerDisplayBase {
public:
    lgfx::Bus_SPI       _bus;
    lgfx::Panel_ILI9341 _panel;
    lgfx::Light_PWM     _light;
    lgfx::Touch_XPT2046 _touch;

    BoardDisplay(void) {
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

    // Calibracao no formato proprio [x_min, x_max, y_min, y_max, 0],
    // persistida pelo TouchCalibrator em /local/touch_cal_p.bin.
    void setTouch(uint16_t* calData) override {
        auto cfg = _touch.config();
        cfg.x_min = calData[0];
        cfg.x_max = calData[1];
        cfg.y_min = calData[2];
        cfg.y_max = calData[3];
        _touch.config(cfg);
    }

    // Calibracao interativa de 2 pontos (cantos opostos): le o raw do
    // XPT2046 em duas marcas e deriva a faixa de mapeamento.
    void calibrateTouch(uint16_t* calData, uint32_t color, uint32_t bg, uint8_t size) override {
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
            {w / MARGIN_DIV, h / MARGIN_DIV},
            {w - w / MARGIN_DIV, h - h / MARGIN_DIV},
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
            for (int s = 0; s < SAMPLES;) {
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

#endif  // CELER_BOARDS_CYD_DISPLAY_H
