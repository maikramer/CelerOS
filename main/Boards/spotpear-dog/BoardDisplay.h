#ifndef CELER_BOARDS_SPOTPEAR_DOG_DISPLAY_H
#define CELER_BOARDS_SPOTPEAR_DOG_DISPLAY_H

// ----------------------------------------------------------------------------
// Cao robotico SpotBear/ZZPET (ESP32-S3-Ai-Dog, SKU zzpet-s3): OLED 1.3"
// SH1106 128x64 no I2C0 (SDA=41, SCL=42, addr 0x3C). Pinout extraido via
// JTAG e validado com firmware de bring-up (repo maikramer/zzpet-s3-dog).
//
// ATENCAO barramento: esta placa NAO tem pull-ups externos no I2C — a
// 100 kHz tudo limpo, a 400 kHz as transacoes corrompem e o OLED embaralha
// pixels (descoberto na validacao de hardware; NAO subir a frequencia sem
// acrescentar pull-ups de 4k7 na placa).
//
// Orientacao: o vidro do OLED e montado girado dentro da cabeca do cachorro.
// O painel 1-bit do LovyanGFX tem framebuffer proprio e faz a rotacao por
// conta propria: setRotation/offset_rotation cobrem a transformacao que o
// firmware do fornecedor chamava de SWAP_XY+MIRROR_X+MIRROR_Y. O SH1106
// tem RAM de 132 colunas: offset_x = 2 para o vidro de 128.
//
// Sem touch (o "touch" do cachorro e um pad capacitivo avulso no GPIO10,
// lido pelo BoardIO — nao e um touchscreen) e sem backlight PWM (o painel
// implementa setBrightness como contraste via comando 0x81).
// ----------------------------------------------------------------------------

#include <cstdio>
#include "esp_log.h"
#include "../../Display/Display.h"

#include <lgfx/v1/panel/Panel_SSD1306.hpp>  // Panel_SH110x mora aqui
#include <lgfx/v1/platforms/esp32/Bus_I2C.hpp>

class BoardDisplay : public CelerDisplayBase {
public:
    lgfx::Bus_I2C     _bus;
    lgfx::Panel_SH110x _panel;

    BoardDisplay(void) {
        {
            auto cfg = _bus.config();
            cfg.freq_write = 100000;  // SEM pull-up externo: 400kHz corrompe
            cfg.freq_read = 100000;
            cfg.pin_scl = 42;
            cfg.pin_sda = 41;
            cfg.i2c_port = 0;
            cfg.i2c_addr = 0x3C;
            cfg.prefix_len = 1;
            cfg.prefix_cmd = 0x00;
            cfg.prefix_data = 0x40;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.panel_width = 128;
            cfg.panel_height = 64;
            cfg.memory_width = 128;
            cfg.memory_height = 64;
            cfg.offset_x = 2;          // RAM de 132 colunas do SH1106
            cfg.offset_y = 0;
            cfg.offset_rotation = 1;   // vidro girado na cabeca (bring-up)
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

#endif  // CELER_BOARDS_SPOTPEAR_DOG_DISPLAY_H
