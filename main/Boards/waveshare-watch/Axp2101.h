#ifndef CELER_BOARDS_WAVESHARE_WATCH_AXP2101_H
#define CELER_BOARDS_WAVESHARE_WATCH_AXP2101_H

// AXP2101 (PMU do watch, I2C0 addr 0x34) — o minimo para o vidro acender.
//
// Sem isso a tela fica preta mesmo com o CO5300 inicializado: o AMOLED e o
// touch sao alimentados por trilhos do PMU que sobem desligados. Porte fiel
// do peripherals/power.rs do firmware Rust (waveshare-watch-rs):
//   DCDC1 = 3,3 V  (reg 0x82 = 18; bit0 do reg 0x80 liga)
//   ALDO1 = 3,3 V  (reg 0x92 = 28; bit0 do reg 0x90 liga)
//
// A bateria (0x34/0x35 mV 14 bits, 0xA4 percent) e lida em runtime pelo bus
// compartilhado do touch — ver WatchI2c.h — atraves do hook
// BoardProfile::readBatteryMv. Percent/VBUS/estado de carga ficam para uma
// API JS dedicada.

#include "WatchI2c.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <stdint.h>

namespace Axp2101 {

static constexpr uint8_t kAddr         = 0x34;
static constexpr uint8_t REG_DC_ONOFF  = 0x80;  // on/off dos DCDC + DVM
static constexpr uint8_t REG_DC_VOL0   = 0x82;  // tensao DCDC1
static constexpr uint8_t REG_LDO_ONOFF = 0x90;  // on/off ALDO1..4
static constexpr uint8_t REG_LDO_VOL0  = 0x92;  // tensao ALDO1
static constexpr uint8_t REG_VBAT_H    = 0x34;  // bateria mV, 14 bits (H|L)
static constexpr uint8_t REG_VBAT_L    = 0x35;

/// Liga os trilhos do display (DCDC1 + ALDO1 em 3,3 V).
/// Chamar ANTES do init do painel. Retorna false se o PMU nao responder.
static bool enableDisplayRails(gpio_num_t sda, gpio_num_t scl) {
    i2c_master_bus_handle_t bus = nullptr;
    i2c_master_dev_handle_t dev = nullptr;

    i2c_master_bus_config_t bcfg = {};
    bcfg.i2c_port = I2C_NUM_0;
    bcfg.sda_io_num = sda;
    bcfg.scl_io_num = scl;
    bcfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bcfg.glitch_ignore_cnt = 7;
    bcfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bcfg, &bus) != ESP_OK) return false;

    i2c_device_config_t dcfg = {};
    dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dcfg.device_address = kAddr;
    dcfg.scl_speed_hz = 400000;
    bool ok = i2c_master_bus_add_device(bus, &dcfg, &dev) == ESP_OK;

    uint8_t v = 0;
    auto wr = [&](uint8_t reg, uint8_t val) -> bool {
        uint8_t buf[2] = {reg, val};
        return ok && i2c_master_transmit(dev, buf, 2, 20) == ESP_OK;
    };
    auto rd = [&](uint8_t reg) -> bool {
        return ok && i2c_master_transmit_receive(dev, &reg, 1, &v, 1, 20) == ESP_OK;
    };

    // DCDC1 = 3,3 V: (3300 - 1500) / 100 = 18
    ok = ok && wr(REG_DC_VOL0, 18) && rd(REG_DC_ONOFF) && wr(REG_DC_ONOFF, v | 0x01);
    // ALDO1 = 3,3 V: (3300 - 500) / 100 = 28
    ok = ok && wr(REG_LDO_VOL0, 28) && rd(REG_LDO_ONOFF) && wr(REG_LDO_ONOFF, v | 0x01);

    if (dev) i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);

    if (ok) {
        ESP_LOGI("celer.power", "AXP2101: DCDC1+ALDO1 em 3,3V (trilhos do display)");
    } else {
        ESP_LOGE("celer.power", "AXP2101 nao respondeu — tela deve ficar preta");
    }
    return ok;
}

/// Tensao da celula em mV, pelo bus I2C0 compartilhado (dono: touch).
/// Implementa o hook BoardProfile::readBatteryMv do watch — o BoardIO chama
/// antes do caminho ADC. Formato do power.rs: ((H << 8) | L) & 0x3FFF.
static bool readBatteryMv(int* mv) {
    static i2c_master_dev_handle_t s_dev = nullptr;
    static bool s_tried = false;
    if (!s_dev && !s_tried) {
        s_tried = true;
        WatchI2c::addDevice(kAddr, &s_dev);
    }
    if (!s_dev || mv == nullptr) return false;
    uint8_t reg = REG_VBAT_H;
    uint8_t b[2] = {0, 0};
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, 20) != ESP_OK) return false;
    int v = ((b[0] << 8) | b[1]) & 0x3FFF;
    if (v < 2500 || v > 5000) return false;  // fora da faixa de Li-ion: nao e leitura util
    *mv = v;
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        ESP_LOGI("celer.power", "bateria: %d mV", v);
    }
    return true;
}

}  // namespace Axp2101

#endif  // CELER_BOARDS_WAVESHARE_WATCH_AXP2101_H
