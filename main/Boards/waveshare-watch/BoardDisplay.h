#ifndef CELER_BOARDS_WAVESHARE_WATCH_DISPLAY_H
#define CELER_BOARDS_WAVESHARE_WATCH_DISPLAY_H

// ----------------------------------------------------------------------------
// Waveshare ESP32-S3-Touch-AMOLED-2.06 (smartwatch, ESP32-S3R8)
//
// AMOLED 410x502 CO5300 em QSPI (SDIO0..3 = GPIO4..7, SCLK=11, CS=12, RST=8);
// o painel visivel vive num GRAM maior com offset X=22. Touch FT3168 no I2C0
// (SDA=15, SCL=14, addr 0x38, RST=9, INT=38) — driver i2c_master oficial em
// main/Display/Touch_FT3168_IDF.h (o LL do LovyanGFX falha no IDF 6.1 e o
// FT3168 precisa do reg 0xA5 no init). Brilho = WRDISBV do painel (DCS 0x51),
// sem Light_PWM: profile.backlightPwm habilita o Backlight:: do CelerOS via
// setBrightness do LGFX. PMU AXP2101 (0x34, mesmo I2C0) liga DCDC1+ALDO1
// antes do init do painel — ver Axp2101.h/Board.cpp.
//
// Pinout e sequencias portados do firmware Rust do watch (waveshare-watch-rs,
// src/board.rs + src/drivers/co5300.rs).
// ----------------------------------------------------------------------------

#include "../../Display/Display.h"

#include "../../Display/Touch_FT3168_IDF.h"
#include <lgfx/v1/panel/Panel_CO5300.hpp>

// Painel com o reset lento do firmware Rust (low 200 ms — o painel nao sobe
// com pulso curto) e o patch pos-init que a lista do LGFX (do T-Watch-Ultra,
// mesmo controlador) nao tem: 0x58=0x00 (contraste off) e 0x36=0x00 (MADCTL
// portrait, RGB). Padrao do CelerPanel do smartdisplay.
class CelerPanel : public lgfx::Panel_CO5300 {
public:
    bool init(bool use_reset) override {
        if (_cfg.pin_rst >= 0) {
            lgfx::pinMode((gpio_num_t)_cfg.pin_rst, lgfx::pin_mode_t::output);
            lgfx::gpio_hi((gpio_num_t)_cfg.pin_rst);
            lgfx::delay(10);
            lgfx::gpio_lo((gpio_num_t)_cfg.pin_rst);
            lgfx::delay(200);   // reset longo (co5300.rs: RST_DELAY_MS)
            lgfx::gpio_hi((gpio_num_t)_cfg.pin_rst);
            lgfx::delay(200);
            use_reset = false;  // reset ja feito a mao
        }
        if (!lgfx::Panel_CO5300::init(use_reset)) return false;
        static constexpr const uint8_t rust_patch[] = {
            0x58, 1, 0x00,  // contrast enhancement off
            0x36, 1, 0x00,  // MADCTL: portrait, RGB
            0xFF, 0xFF,
        };
        command_list(rust_patch);
        return true;
    }
};

class BoardDisplay : public CelerDisplayBase {
public:
    lgfx::Bus_SPI          _bus;
    CelerPanel             _panel;
    lgfx::Touch_FT3168_IDF _touch;

    BoardDisplay(void) {
        {
            // QSPI: io0..3 = SDIO0..3 do painel. Registradores via cmd 0x02 +
            // endereco 24-bit (single lane), pixels via 0x32 (quad) — o mesmo
            // protocolo do qspi_bus.rs, que roda estavel a 80 MHz.
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 80000000;
            cfg.freq_read   = 16000000;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 11;
            cfg.pin_io0  = 4;   // SDIO0 ("MOSI" quad — o Rust chama de sio0)
            cfg.pin_io1  = 5;   // SDIO1 (tem que ser io1, nunca miso: flutua)
            cfg.pin_io2  = 6;   // SDIO2
            cfg.pin_io3  = 7;   // SDIO3
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = 12;
            cfg.pin_rst  = 8;
            // Largura LOGICA 412 (nao 410): o framebuffer do Panel_AMOLED
            // aloca stride arredondado para multiplo de 4, mas o display()
            // le com stride = panel_width cru — com 410 (%4=2) cada linha
            // desloca 2 px e o vidro sai cisalhado ("italico"). Com 412 os
            // dois strides batem; as 2 colunas extra caem no GRAM alem do
            // vidro visivel (offset 22 -> CASET 22..433, inofensivo).
            cfg.panel_width   = 412;
            cfg.panel_height  = 502;
            cfg.memory_width  = 412;
            cfg.memory_height = 502;
            cfg.offset_x = 22;         // LCD_COL_OFFSET do Rust (vidro 410)
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 1;
            cfg.readable = false;      // QSPI sem path de leitura
            cfg.invert   = false;      // INVOFF (padrao do painel, igual Rust)
            _panel.config(cfg);
        }
        {
            auto cfg = _touch.config();
            // Bounds = painel (identidade): medido com toques SUSTENTADOS
            // topo/meio/fundo (driver grava raw em /local/touch_range.txt)
            // — o FT3168 entrega x 0..~410, y 0..~500 nativo. Sessoes
            // anteriores "comprimidas" eram leitura enviesada, nao o chip.
            cfg.x_min = 0;
            cfg.x_max = 411;
            cfg.y_min = 0;
            cfg.y_max = 501;
            cfg.pin_int = 38;          // LOW = dedo na tela: borda acorda a leitura I2C
            cfg.pin_rst = 9;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.pin_sda = 15;
            cfg.pin_scl = 14;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x38;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }

    // Sono REAL do painel (ScreenPower): o wrapper do framebuffer faz
    // setSleep no-op (Panel_FrameBufferBase), entao falamos com o painel
    // de verdade — SLPIN (0x10) dorme, SLPOUT (0x11 + 150 ms) acorda com a
    // GRAM intacta (o quadro volta como estava).
    void panelSleep() {
        _panel.setBrightness(0);
        _panel.setSleep(true);
    }
    void panelWakeup() {
        _panel.setSleep(false);   // SLPOUT; o brilho volta com Backlight::undim
    }

    // Framebuffer do Panel_AMOLED (PSRAM, ~402 KB): o CO5300 so aceita
    // janelas de escrita alinhadas a par e nao tem readback — com o FB a UI
    // desenha livre, o flush cuida do alinhamento e o readRect (screencap /
    // espelho /screen) volta a funcionar. Padrao do exemplo T-Display-S3.
    bool enableFrameBuffer(bool auto_display = true) {
        if (_panel.initPanelFb()) {
            auto* fb = _panel.getPanelFb();
            if (fb) {
                fb->setBus(&_bus);
                fb->setAutoDisplay(auto_display);
                setPanel(fb);
                // BUG DO WRAPPER: o ctor dele chama setTouch() ANTES do
                // config(412x502) chegar — a afina do touch fica calculada
                // para um painel default (~240) e o touch sai comprimido
                // (raw*240/412). Recalibra DEPOIS, com o cfg ja certo.
                fb->setTouch(&_touch);
                return true;
            }
        }
        return false;
    }

    // Touch capacitivo: calibracao do XPT2046 nao se aplica (base = no-op)
};

#endif  // CELER_BOARDS_WAVESHARE_WATCH_DISPLAY_H
