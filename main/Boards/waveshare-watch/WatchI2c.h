#ifndef CELER_BOARDS_WAVESHARE_WATCH_I2C_H
#define CELER_BOARDS_WAVESHARE_WATCH_I2C_H

// Bus I2C0 compartilhado do watch (SDA=15, SCL=14).
//
// Ciclo de vida: o AXP2101 abre o bus no boot so para ligar os trilhos do
// display e o fecha; o driver do touch (Touch_FT3168_IDF) abre em seguida e
// MANTEM aberto para sempre. Todo acesso em runtime (RTC PCF85063, bateria
// do AXP2101, IMU QMI8658) entra nesse mesmo bus: pega o handle existente
// com i2c_master_get_bus_handle() e adiciona o proprio device — o driver
// novo do IDF serializa as transacoes por device.

#include "driver/i2c_master.h"
#include "esp_log.h"
#include <stdint.h>

namespace WatchI2c {

/// Adiciona um device ao bus I2C0 ja aberto pelo touch. Retorna false (e
/// loga uma vez) se o bus ainda nao existir ou o device nao couber.
inline bool addDevice(uint8_t addr, i2c_master_dev_handle_t* out) {
    i2c_master_bus_handle_t bus = nullptr;
    if (i2c_master_get_bus_handle(I2C_NUM_0, &bus) != ESP_OK || bus == nullptr) {
        static bool logged = false;
        if (!logged) {
            ESP_LOGE("celer.i2c", "bus I2C0 ainda nao existe (touch nao iniciou?)");
            logged = true;
        }
        return false;
    }
    i2c_device_config_t dcfg = {};
    dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dcfg.device_address = addr;
    dcfg.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &dcfg, out) != ESP_OK) {
        *out = nullptr;
        return false;
    }
    return true;
}

/// Handle em cache por chip: no primeiro uso adiciona o device ao bus;
/// falha nao tenta de novo (bus pode nem existir ainda no boot). Cada
/// driver (RTC, PMU, IMU, codecs) guarda os proprios statics.
inline i2c_master_dev_handle_t cachedDevice(uint8_t addr, i2c_master_dev_handle_t* cache,
                                            bool* tried) {
    if (*cache == nullptr && !*tried) {
        *tried = true;
        addDevice(addr, cache);
    }
    return *cache;
}

/// Escrita do registrador de 8 bits.
inline bool writeReg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, 20) == ESP_OK;
}

/// Leitura a partir do registrador (auto-incremento do chip).
inline bool readRegs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* out, size_t len) {
    return i2c_master_transmit_receive(dev, &reg, 1, out, len, 20) == ESP_OK;
}

}  // namespace WatchI2c

#endif  // CELER_BOARDS_WAVESHARE_WATCH_I2C_H
