#ifndef KRYONOS_TOUCH_GT911_IDF_H
#define KRYONOS_TOUCH_GT911_IDF_H

// Touch GT911 sobre o driver i2c_master oficial do ESP-IDF.
//
// Motivo: a camada I2C register-level (LL) do LovyanGFX falha silenciosamente
// no IDF 6.1 neste alvo (transacoes sem ACK/timeout sem dado — probe com o
// driver oficial confirma o GT911 respondendo em 0x5D enquanto o getTouch do
// LovyanGFX retorna 0). Este driver implementa a interface ITouch do LovyanGFX
// usando o driver novo do IDF, mantendo o painel/display intacto.
//
// Protocolo Goodix GT911 (registros):
//   0x8140..0x8143 : product id ("911", "9147", ...)
//   0x814E         : status (bit7 = buffer pronto, bits0..3 = numero de pontos)
//   0x814F + i*8   : ponto i — [track][x_lo][x_hi][y_lo][y_hi][sz_lo][sz_hi][rsv]
//   escrita de 0 em 0x814E limpa a flag de dados.

#include "lgfx/v1/Touch.hpp"
#include "driver/i2c_master.h"
#include "esp_log.h"

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  struct Touch_GT911_IDF : public ITouch
  {
    Touch_GT911_IDF(void)
    {
      _cfg.freq = 400000;
      _cfg.i2c_addr = 0x5D;
    }

    ~Touch_GT911_IDF(void) override
    {
      deinit();
    }

    bool init(void) override
    {
      deinit();

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
      if (freq == 0 || freq > 400000) freq = 400000;  // GT911: max 400 kHz
      dcfg.scl_speed_hz = freq;
      if (i2c_master_bus_add_device(_bus, &dcfg, &_dev) != ESP_OK) {
        deinit();
        return false;
      }

      // Valida a presenca do controlador lendo o product id
      uint8_t reg[2] = {0x81, 0x40};
      uint8_t id[4] = {0, 0, 0, 0};
      if (!writeRead(reg, 2, id, 4)) {
        ESP_LOGE("kryon.touch", "GT911 id read falhou (addr 0x%02X)", (unsigned)_cfg.i2c_addr);
        deinit();
        return false;
      }
      ESP_LOGI("kryon.touch", "GT911 init OK, product id=%c%c%c%c", id[0], id[1], id[2], id[3]);
      return true;
    }

    void wakeup(void) override {}
    void sleep(void) override {}

    uint_fast8_t getTouchRaw(touch_point_t* tp, uint_fast8_t count) override
    {
      if (_dev == nullptr || tp == nullptr || count == 0) return 0;

      uint8_t reg[2] = {0x81, 0x4E};
      uint8_t status = 0;
      if (!writeRead(reg, 2, &status, 1)) return 0;

      uint_fast8_t points = status & 0x0F;
      if (points == 0) {
        // Buffer vazio: garante flag zerada para o proximo evento
        clearStatus();
        return 0;
      }
      if (points > 5) points = 5;
      if (points > count) points = count;

      uint8_t preg[2] = {0x81, 0x4F};
      uint8_t data[5 * 8];
      if (!writeRead(preg, 2, data, points * 8)) return 0;

      for (uint_fast8_t i = 0; i < points; i++) {
        uint8_t* p = &data[i * 8];
        tp[i].id = p[0];
        int16_t x = (int16_t)(p[1] | (p[2] << 8));
        int16_t y = (int16_t)(p[3] | (p[4] << 8));
        tp[i].size = (uint16_t)(p[5] | (p[6] << 8));

        if (x < _cfg.x_min) x = _cfg.x_min;
        if (x > _cfg.x_max) x = _cfg.x_max;
        if (y < _cfg.y_min) y = _cfg.y_min;
        if (y > _cfg.y_max) y = _cfg.y_max;
        tp[i].x = x;
        tp[i].y = y;
      }

      clearStatus();
      return points;
    }

  private:
    i2c_master_bus_handle_t _bus = nullptr;
    i2c_master_dev_handle_t _dev = nullptr;

    bool writeRead(const uint8_t* w, size_t wl, uint8_t* r, size_t rl)
    {
      return i2c_master_transmit_receive(_dev, w, wl, r, rl, 20) == ESP_OK;
    }

    void clearStatus(void)
    {
      uint8_t clr[3] = {0x81, 0x4E, 0x00};
      i2c_master_transmit(_dev, clr, 3, 20);
    }

    void deinit(void)
    {
      if (_dev) {
        i2c_master_bus_rm_device(_dev);
        _dev = nullptr;
      }
      if (_bus) {
        i2c_del_master_bus(_bus);
        _bus = nullptr;
      }
    }
  };

//----------------------------------------------------------------------------
 }
}

#endif // KRYONOS_TOUCH_GT911_IDF_H
