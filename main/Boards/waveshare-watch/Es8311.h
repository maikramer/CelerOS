#ifndef CELER_BOARDS_WAVESHARE_WATCH_ES8311_H
#define CELER_BOARDS_WAVESHARE_WATCH_ES8311_H

// Codec ES8311 do watch (I2C0 addr 0x18) + amp PA no GPIO46: DAC/playback
// (o MIC dual e o ES7210 — ver Es7210.h). Porte fiel do
// peripherals/audio.rs do firmware Rust (waveshare-watch-rs):
//
//   - init a 16 kHz com MCLK 4,096 MHz (256x fs) vindo do pino (I2S0):
//     o 0x00=0x80 (power-on) APOS o reset e CRITICO;
//   - shutdown() derruba analog/DAC de verdade (~20 mA mais barato que
//     mute); unmute() religa p/ o beep;
//   - o PA so fica alto durante a transmissao (ruido de pop fora).
//
// Hooks BoardProfile::audioCodecWake/audioCodecSleep — o BoardIO::toneI2s
// chama em volta do tom quando o perfil os fornece.

#include "WatchI2c.h"
#include "esp_log.h"
#include <stdint.h>

namespace Es8311 {

static constexpr uint8_t kAddr = 0x18;

inline i2c_master_dev_handle_t dev() {
    static i2c_master_dev_handle_t s_dev = nullptr;
    static bool s_tried = false;
    return WatchI2c::cachedDevice(kAddr, &s_dev, &s_tried);
}

inline bool wr(uint8_t reg, uint8_t val) {
    return WatchI2c::writeReg8(dev(), reg, val);
}

inline uint8_t rd(uint8_t reg) {
    uint8_t v = 0;
    WatchI2c::readRegs(dev(), reg, &v, 1);
    return v;
}

/// Power-on completo a 16 kHz (MCLK 4,096 MHz do pino, BCLK 512 kHz).
inline bool init() {
    if (dev() == nullptr) return false;
    bool ok = true;
    ok = ok && wr(0x00, 0x1F);                       // reset
    ok = ok && wr(0x00, 0x00);                       // limpa reset
    ok = ok && wr(0x00, 0x80);                       // power-on (CRITICO)
    ok = ok && wr(0x01, 0x3F);                       // clocks on, MCLK do pino
    ok = ok && wr(0x02, (uint8_t)((rd(0x02) & 0x07) | (1 << 5)));  // pre_div=2, 1x
    ok = ok && wr(0x03, 0x10);                       // fs_mode, adc_osr
    ok = ok && wr(0x04, 0x10);                       // dac_osr
    ok = ok && wr(0x05, 0x00);                       // adc/dac_div = 1
    ok = ok && wr(0x06, (uint8_t)((rd(0x06) & 0xE0) | 3));  // bclk_div = 4
    ok = ok && wr(0x07, (uint8_t)(rd(0x07) & 0xC0)); // lrck_h = 0
    ok = ok && wr(0x08, 0xFF);                       // lrck_l
    ok = ok && wr(0x09, 0x0C);                       // DAC SDP: I2S 16-bit
    ok = ok && wr(0x0A, 0x0C);                       // ADC SDP: I2S 16-bit
    ok = ok && wr(0x0D, 0x01);                       // analog on
    ok = ok && wr(0x0E, 0x02);                       // PGA + ADC modulator
    ok = ok && wr(0x12, 0x00);                       // DAC on
    ok = ok && wr(0x13, 0x10);                       // HP drive on
    ok = ok && wr(0x1C, 0x6A);                       // ADC EQ bypass
    ok = ok && wr(0x37, 0x08);                       // DAC EQ bypass
    ok = ok && wr(0x32, 0xD9);                       // volume 85%
    if (!ok) ESP_LOGE("celer.audio", "ES8311 init falhou");
    return ok;
}

/// Desliga de verdade (nao e mute): economiza ~20 mA.
inline void shutdown() {
    wr(0x32, 0x00);   // volume 0
    wr(0x13, 0x00);   // HP off
    wr(0x12, 0x20);   // PDN_DAC
    wr(0x0E, 0xFF);   // ADC/PGA off
    wr(0x0D, 0xFC);   // analog off
}

/// Religado rapido para tocar (apos um init() previo).
inline bool unmute() {
    if (dev() == nullptr) return false;
    bool ok = true;
    ok = ok && wr(0x0D, 0x01);
    ok = ok && wr(0x0E, 0x02);
    ok = ok && wr(0x12, 0x00);
    ok = ok && wr(0x13, 0x10);
    ok = ok && wr(0x32, 0xD0);
    return ok;
}

/// Unmute com volume 0..100 (reg 0x32; 100% = 0xFF — mesma conta do
/// firmware Rust: 85% -> 0xD9).
inline bool unmuteVolume(int pct) {
    if (!unmute()) return false;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int r = pct * 256 / 100 - 1;
    if (r < 0) r = 0;
    return wr(0x32, (uint8_t)r);
}

}  // namespace Es8311

#endif  // CELER_BOARDS_WAVESHARE_WATCH_ES8311_H
