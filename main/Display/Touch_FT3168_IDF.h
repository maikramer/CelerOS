#ifndef CELEROS_TOUCH_FT3168_IDF_H
#define CELEROS_TOUCH_FT3168_IDF_H

// Touch FT3168 (FocalTech, familia FT5x06) sobre o driver i2c_master oficial
// do ESP-IDF.
//
// Mesmo motivo do Touch_GT911_IDF.h: a camada I2C LL do LovyanGFX falha
// silenciosamente no IDF 6.1. Alem disso o FT3168 do watch Waveshare AMOLED
// 2.06 precisa do reg 0xA5 (power mode = monitor) escrito no init — sem isso
// o chip hiberna e as leituras voltam vazias; o Touch_FT5x06 nativo nao faz.
//
// Porte fiel do peripherals/touch.rs do firmware Rust do watch
// (waveshare-watch-rs):
//   - reset fisico: TP_RESET low 10 ms, high 50 ms;
//   - reg 0xA5 = 0x01 (monitor mode);
//   - leitura: reg 0x02 = numero de dedos; regs 0x03..0x06 = X/Y do ponto 1
//     (12 bits: (h & 0x0F) << 8 | l).
// O chip entrega coordenadas ja em resolucao do painel — o firmware Rust usa
// o valor cru direto como pixel; aqui so limitamos aos bounds do vidro.

#include "lgfx/v1/Touch.hpp"
#include "driver/i2c_master.h"
#include "esp_log.h"

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  struct Touch_FT3168_IDF : public ITouch
  {
    static constexpr const uint8_t default_addr   = 0x38;
    static constexpr const uint8_t REG_FINGER_NUM = 0x02;
    static constexpr const uint8_t REG_XY1        = 0x03;  // x_h, x_l, y_h, y_l
    static constexpr const uint8_t REG_POWER_MODE = 0xA5;

    Touch_FT3168_IDF(void)
    {
      _cfg.i2c_addr = default_addr;
      _cfg.freq = 400000;
    }

    ~Touch_FT3168_IDF(void) override
    {
      deinit();
    }

    bool init(void) override
    {
      if (_inited) return true;
      deinit();

      // INT como entrada com pull-up ANTES do reset (igual ao Rust): em chips
      // FocalTech o nivel do INT durante o reset participa da selecao de
      // endereco — deixar flutuando pode botar o chip em outro endereco.
      if (_cfg.pin_int >= 0)
      {
        lgfx::pinMode((gpio_num_t)_cfg.pin_int, lgfx::pin_mode_t::input_pullup);
      }

      // Reset fisico igual ao Rust (TP_RESET = GPIO9 do watch): pulso curto
      // e descanso antes de falar com o chip.
      if (_cfg.pin_rst >= 0)
      {
        lgfx::pinMode((gpio_num_t)_cfg.pin_rst, lgfx::pin_mode_t::output);
        lgfx::gpio_hi((gpio_num_t)_cfg.pin_rst);
        lgfx::delay(10);
        lgfx::gpio_lo((gpio_num_t)_cfg.pin_rst);
        lgfx::delay(10);
        lgfx::gpio_hi((gpio_num_t)_cfg.pin_rst);
        lgfx::delay(100);
      }

      for (int retry = 4; retry; --retry)
      {
        if (!openBus()) { deinit(); return false; }

        // Monitor mode (reg 0xA5): write best-effort — o firmware Rust tambem
        // ignora o retorno desta escrita.
        writeReg(REG_POWER_MODE, 0x01);
        lgfx::delay(2);

        // Validacao por uma leitura REAL (reg 0x02, n. de dedos) — nunca pelo
        // readback do 0xA5: o modo monitor pode nao ser legivel de volta.
        uint8_t fingers = 0xFF;
        if (readReg(REG_FINGER_NUM, &fingers))
        {
          _inited = true;
          ESP_LOGI("celer.touch", "FT3168 init OK (0x%02X @ %u kHz, monitor, fingers=%u)",
                   (unsigned)_cfg.i2c_addr, (unsigned)(_cfg.freq / 1000), (unsigned)fingers);
          return true;
        }
        closeBus();
        lgfx::delay(20);
      }
      ESP_LOGE("celer.touch", "FT3168 nao respondeu (addr 0x%02X)", (unsigned)_cfg.i2c_addr);
      return false;
    }

    void wakeup(void) override { writeReg(REG_POWER_MODE, 0x01); }
    void sleep(void) override {}

    uint_fast8_t getTouchRaw(touch_point_t* tp, uint_fast8_t count) override
    {
      if (!_inited || tp == nullptr || count == 0) return 0;

      uint8_t fingers = 0;
      if (!readReg(REG_FINGER_NUM, &fingers)) return 0;
      if ((fingers & 0x0F) == 0) return 0;

      uint8_t xy[4] = {0, 0, 0, 0};
      if (!readRegBurst(REG_XY1, xy, 4)) return 0;

      // So o ponto 1 (o firmware Rust tambem le apenas ele).
      tp[0].id   = 0;
      tp[0].x    = (int16_t)(((xy[0] & 0x0F) << 8) | xy[1]);
      tp[0].y    = (int16_t)(((xy[2] & 0x0F) << 8) | xy[3]);
      tp[0].size = 1;

      if (tp[0].x < _cfg.x_min) tp[0].x = _cfg.x_min;
      if (tp[0].x > _cfg.x_max) tp[0].x = _cfg.x_max;
      if (tp[0].y < _cfg.y_min) tp[0].y = _cfg.y_min;
      if (tp[0].y > _cfg.y_max) tp[0].y = _cfg.y_max;
      return 1;
    }

  private:
    i2c_master_bus_handle_t _bus = nullptr;
    i2c_master_dev_handle_t _dev = nullptr;
    bool _inited = false;

    bool openBus(void)
    {
      i2c_master_bus_config_t bcfg = {};
      bcfg.i2c_port = _cfg.i2c_port;
      bcfg.sda_io_num = (gpio_num_t)_cfg.pin_sda;
      bcfg.scl_io_num = (gpio_num_t)_cfg.pin_scl;
      bcfg.clk_source = I2C_CLK_SRC_DEFAULT;
      bcfg.glitch_ignore_cnt = 7;
      bcfg.flags.enable_internal_pullup = true;
      if (i2c_new_master_bus(&bcfg, &_bus) != ESP_OK) {
        _bus = nullptr;
        return false;
      }

      i2c_device_config_t dcfg = {};
      dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
      dcfg.device_address = (uint16_t)_cfg.i2c_addr;
      uint32_t freq = _cfg.freq;
      if (freq == 0 || freq > 400000) freq = 400000;
      dcfg.scl_speed_hz = freq;
      if (i2c_master_bus_add_device(_bus, &dcfg, &_dev) != ESP_OK) {
        closeBus();
        return false;
      }
      return true;
    }

    void closeBus(void)
    {
      if (_dev) { i2c_master_bus_rm_device(_dev); _dev = nullptr; }
      if (_bus) { i2c_del_master_bus(_bus); _bus = nullptr; }
    }

    void deinit(void)
    {
      closeBus();
      _inited = false;
    }

    bool writeReg(uint8_t reg, uint8_t val)
    {
      uint8_t buf[2] = {reg, val};
      return i2c_master_transmit(_dev, buf, 2, 20) == ESP_OK;
    }

    bool readReg(uint8_t reg, uint8_t* val)
    {
      return i2c_master_transmit_receive(_dev, &reg, 1, val, 1, 20) == ESP_OK;
    }

    bool readRegBurst(uint8_t reg, uint8_t* data, size_t len)
    {
      return i2c_master_transmit_receive(_dev, &reg, 1, data, len, 20) == ESP_OK;
    }
};

//----------------------------------------------------------------------------
 }
}

#endif  // CELEROS_TOUCH_FT3168_IDF_H
