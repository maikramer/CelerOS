#ifndef CELER_BOARDS_WAVESHARE_WATCH_PCF85063_H
#define CELER_BOARDS_WAVESHARE_WATCH_PCF85063_H

// RTC PCF85063A do watch (I2C0, addr 0x51). Porte fiel do peripherals/rtc.rs
// do firmware Rust (waveshare-watch-rs):
//   - registradores 0x04..0x0A em BCD: seg (bit7 = OS, oscilador parado),
//     min, hora (24h), dia, weekday (0=domingo, cru), mes, ano (00..99);
//   - init limpa STOP (bit5) e 12_24 (bit2) do CTRL1 (0x00) so se preciso;
//   - escrita para o oscilador (CTRL1 |= 0x20), grava BCD e relanca.
// A hora gravada e LOCAL (igual Rust): o TimeManager le com mktime() sob o
// TZ ja carregado e grava com localtime_r(). Validade: OS==0 e ano >= 2020.

#include "WatchI2c.h"
#include <time.h>

namespace Pcf85063 {

static constexpr uint8_t kAddr     = 0x51;
static constexpr uint8_t REG_CTRL1 = 0x00;
static constexpr uint8_t REG_TIME  = 0x04;  // seg..ano (7 bytes)

inline i2c_master_dev_handle_t dev() {
    static i2c_master_dev_handle_t s_dev = nullptr;
    static bool s_tried = false;
    if (!s_dev && !s_tried) {
        s_tried = true;
        if (WatchI2c::addDevice(kAddr, &s_dev)) {
            // CTRL1: limpa STOP (bit5) e 12_24 (bit2) se estiverem setados —
            // so escreve quando muda (evita resetar o contador a toa).
            uint8_t ctrl = 0;
            uint8_t reg = REG_CTRL1;
            if (i2c_master_transmit_receive(s_dev, &reg, 1, &ctrl, 1, 20) == ESP_OK
                && (ctrl & 0x24) != 0) {
                uint8_t buf[2] = {REG_CTRL1, (uint8_t)(ctrl & ~0x24)};
                i2c_master_transmit(s_dev, buf, 2, 20);
            }
        }
    }
    return s_dev;
}

inline int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
inline uint8_t toBcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/// Le a hora do RTC para `out`. False se o chip nao respondeu, o oscilador
/// estiver parado (OS) ou a data for anterior a 2020 (bateria nova/corta).
inline bool read(struct tm& out) {
    i2c_master_dev_handle_t d = dev();
    if (!d) return false;
    uint8_t reg = REG_TIME;
    uint8_t b[7] = {0, 0, 0, 0, 0, 0, 0};
    if (i2c_master_transmit_receive(d, &reg, 1, b, 7, 20) != ESP_OK) return false;
    if (b[0] & 0x80) return false;  // OS: oscilador parado, hora nao presta

    out = {};
    out.tm_sec  = bcd(b[0] & 0x7F);
    out.tm_min  = bcd(b[1] & 0x7F);
    out.tm_hour = bcd(b[2] & 0x3F);
    out.tm_mday = bcd(b[3] & 0x3F);
    out.tm_wday = b[4] & 0x07;
    out.tm_mon  = bcd(b[5] & 0x1F) - 1;
    out.tm_year = bcd(b[6]) + 100;         // 00..99 -> 2000..2099 (- 1900)
    out.tm_isdst = -1;
    int year = out.tm_year + 1900;
    if (year < 2020 || year > 2099) return false;
    return true;
}

/// Grava a hora local `t` no RTC (para e relanca o oscilador em volta da
/// escrita, igual ao rtc.rs).
inline bool write(const struct tm& t) {
    i2c_master_dev_handle_t d = dev();
    if (!d) return false;

    uint8_t reg = REG_CTRL1;
    uint8_t ctrl = 0;
    if (i2c_master_transmit_receive(d, &reg, 1, &ctrl, 1, 20) != ESP_OK) return false;

    // para o oscilador
    uint8_t stop[2] = {REG_CTRL1, (uint8_t)(ctrl | 0x20)};
    if (i2c_master_transmit(d, stop, 2, 20) != ESP_OK) return false;

    uint8_t buf[8] = {
        REG_TIME,
        toBcd(t.tm_sec),                       // bit7 = 0: OS limpo
        toBcd(t.tm_min),
        toBcd(t.tm_hour),
        toBcd(t.tm_mday),
        (uint8_t)(t.tm_wday & 0x07),           // weekday cru (nao BCD)
        toBcd(t.tm_mon + 1),
        toBcd((t.tm_year + 1900) % 100),
    };
    bool ok = i2c_master_transmit(d, buf, 8, 20) == ESP_OK;

    // relanca o oscilador
    uint8_t run[2] = {REG_CTRL1, (uint8_t)(ctrl & ~0x20)};
    ok = i2c_master_transmit(d, run, 2, 20) == ESP_OK && ok;
    return ok;
}

}  // namespace Pcf85063

#endif  // CELER_BOARDS_WAVESHARE_WATCH_PCF85063_H
