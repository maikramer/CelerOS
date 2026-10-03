#ifndef CELER_BOARDS_WAVESHARE_WATCH_ES7210_H
#define CELER_BOARDS_WAVESHARE_WATCH_ES7210_H

// ADC de microfone ES7210 do watch (I2C0 addr 0x40). O ES8311 da placa e so
// o DAC (playback); o mic dual entrega o dado pelo ASDOUT GPIO42 vindo DESTE
// chip — sem esta inicializacao o I2S1 RX so recebe zeros (bancada
// 2026-10-02: dois dias de "mic validado" eram silencio de quarto).
//
// Sequencia fiel do es7210_config_codec() do esp-bsp (components/es7210),
// congelada para 16 kHz com MCLK 4.096 MHz do pino (linha {4096000, 16000}
// da tabela de coeficientes: osr=0x20, adc_div=0x01, doubler=1, dll=1,
// lrck 0x0100) — os clocks vem do proprio canal I2S1 do mic (mclk=GPIO16).
//
// ATENCAO aos bits de power: REG4B/4C/47-4A sao PDN ativos (1 = desligado,
// default 0xFF = tudo off). O 0x0F do componente standalone do esp-bsp so
// serve para mic DIGITAL: ele deixa o MICBIAS (bit 3) derrubado, e os mics
// analogicos do watch ficam sem polarizacao — a voz vaza por acoplamento a
// ~-23 dBFS sobre um piso de ruido alto e o ASR "ouve sem entender"
// (bancada 2026-10-03). Com bias devidamente ligado (0x00), 30 dB de PGA e
// volume digital a 0 dB, a fala chega saudavel ao conversor.
//
// Hooks BoardProfile::micCodecWake/micCodecSleep: o BoardIO chama em volta
// das capturas (mesma disciplina do ES8311 com os beeps).

#include "WatchI2c.h"
#include "esp_log.h"
#include <stdint.h>

namespace Es7210 {

static constexpr uint8_t kAddr = 0x40;  // ES7210_CODEC_DEFAULT_ADDR

inline i2c_master_dev_handle_t dev() {
    static i2c_master_dev_handle_t s_dev = nullptr;
    static bool s_tried = false;
    if (!s_dev && !s_tried) {
        s_tried = true;
        WatchI2c::addDevice(kAddr, &s_dev);
    }
    return s_dev;
}

inline bool wr(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev(), buf, 2, 20) == ESP_OK;
}

inline uint8_t rd(uint8_t reg) {
    uint8_t v = 0;
    i2c_master_transmit_receive(dev(), &reg, 1, &v, 1, 20);
    return v;
}

/// Power-on completo: 16 kHz, I2S 16-bit, mic1-4 a 30 dB, bias 2,87 V.
/// (ADC1 sai no slot ESQUERDO do frame I2S; ADC2 no direito.)
inline bool init() {
    if (dev() == nullptr) return false;
    bool ok = true;
    ok = ok && wr(0x00, 0xFF);            // reset
    ok = ok && wr(0x00, 0x32);            // limpa reset
    ok = ok && wr(0x09, 0x30);            // tempo de power-up
    ok = ok && wr(0x0A, 0x30);
    ok = ok && wr(0x23, 0x2A);            // HPF dos ADC1-4 (corte de DC)
    ok = ok && wr(0x22, 0x0A);
    ok = ok && wr(0x21, 0x2A);
    ok = ok && wr(0x20, 0x0A);
    ok = ok && wr(0x11, 0x60);            // SDP: I2S, 16-bit
    ok = ok && wr(0x12, 0x00);            // sem TDM
    ok = ok && wr(0x16, 0x00);            // ALC off (ganho deterministico)
    ok = ok && wr(0x40, 0xC3);            // analog on + VMID
    ok = ok && wr(0x41, 0x70);            // bias dos mic1/2 = 2,87 V
    ok = ok && wr(0x42, 0x70);            // bias dos mic3/4
    ok = ok && wr(0x43, 0x1A);            // ganho mic1 30 dB (0x0A | 0x10)
    ok = ok && wr(0x44, 0x1A);            // mic2
    ok = ok && wr(0x45, 0x1A);            // mic3
    ok = ok && wr(0x46, 0x1A);            // mic4
    ok = ok && wr(0x47, 0x08);            // mic1 on
    ok = ok && wr(0x48, 0x08);            // mic2
    ok = ok && wr(0x49, 0x08);            // mic3
    ok = ok && wr(0x4A, 0x08);            // mic4
    // 16 kHz com MCLK 4.096 MHz (linha da tabela do esp-bsp)
    ok = ok && wr(0x07, 0x20);            // osr
    ok = ok && wr(0x02, 0xC1);            // adc_div=0x01 | doubler<<6 | dll<<7
    ok = ok && wr(0x04, 0x01);            // lrck_div h
    ok = ok && wr(0x05, 0x00);            // lrck_div l
    ok = ok && wr(0x06, 0x04);            // DLL power down
    // Bits PDN ativos: 0x00 liga TUDO do par (MICBIAS + PGA + ADC + mod +
    // VREF). O 0x0F do esp-bsp standalone derruba o MICBIAS — os mics
    // analogicos deste watch somem (bancada 2026-10-03, ver cabecalho).
    ok = ok && wr(0x4B, 0x00);            // mic1/2: bias + ADC + PGA on
    ok = ok && wr(0x4C, 0x00);            // mic3/4: idem
    ok = ok && wr(0x00, 0x71);            // enable
    ok = ok && wr(0x00, 0x41);
    ok = ok && wr(0x1B, 0xBF);            // volume digital ADC1 = 0 dB
    ok = ok && wr(0x1C, 0xBF);            // ADC2
    if (!ok) ESP_LOGE("celer.audio", "ES7210 init falhou");
    return ok;
}

/// Derruba de verdade o analog e os mics (a proxima captura faz init()).
/// Espelha o es7210_stop() do esp_codec_dev: PDN ativos, 0xFF = tudo off.
inline void shutdown() {
    wr(0x47, 0xFF);                       // mic1 off
    wr(0x48, 0xFF);                       // mic2
    wr(0x49, 0xFF);                       // mic3
    wr(0x4A, 0xFF);                       // mic4
    wr(0x4B, 0xFF);                       // bias/ADC/PGA do par 1/2 off
    wr(0x4C, 0xFF);                       // par 3/4
    wr(0x40, 0xC0);                       // analog off
    wr(0x01, 0x7F);                       // clocks off
    wr(0x06, 0x07);                       // power down
}

}  // namespace Es7210

#endif  // CELER_BOARDS_WAVESHARE_WATCH_ES7210_H
