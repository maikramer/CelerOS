#ifndef CELER_BOARDS_WAVESHARE_WATCH_QMI8658_H
#define CELER_BOARDS_WAVESHARE_WATCH_QMI8658_H

// IMU QMI8658 do watch (I2C0 addr 0x6B). Porte fiel do peripherals/imu.rs
// do firmware Rust (waveshare-watch-rs):
//   - init: WHO_AM_I (0x00) == 0x05, RESET (0x60) = 0xB0 + 15 ms;
//   - modo "idle WOM" (o usado fora de jogos): accel ±8g ODR 30 Hz
//     (CTRL2=0x23), LPF (CTRL5=0x01), motor AnyMotion com threshold nas
//     CAL1..CAL3 (16-bit LE, ~1/32 g por unidade: 0x20 dura, 0x10 media,
//     0x08 facil), CAL4=5, CTRL8=0x02, INT1 ligado (CTRL1=0x48), so accel
//     (CTRL7=0x01) — e a configuracao que o deep sleep do F5 reusa;
//   - leitura: 6 bytes a partir de 0x35 (AX_L), little-endian, escala
//     ±8 g -> 8/32768 g por LSB;
//   - INT (GPIO21) ativo-baixo; o nivel segura ate ler STATUS1 (0x2F).
// O servico le a 30 Hz por polling com a tela acesa; apagada/AOD, le em
// lotes da FIFO (stream de 32 amostras, protocolo CTRL9 do driver oficial da
// QST: REQ_FIFO + handshake de ACK). A INT so e usada no deep sleep com o
// ajuste "imu_wake" (Motion::prepareSleep).

#include "WatchI2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

namespace Qmi8658 {

static constexpr uint8_t kAddr     = 0x6B;
static constexpr uint8_t REG_WHO   = 0x00;  // = 0x05
static constexpr uint8_t REG_CTRL1 = 0x02;  // bit6 auto-inc LE; bit3 INT1 en
static constexpr uint8_t REG_CTRL2 = 0x03;  // accel FS/ODR
static constexpr uint8_t REG_CTRL3 = 0x04;  // gyro FS/ODR
static constexpr uint8_t REG_CTRL5 = 0x06;  // LPF
static constexpr uint8_t REG_CTRL7 = 0x08;  // sensores on/off
static constexpr uint8_t REG_CTRL8 = 0x09;  // bit1 = engine AnyMotion
static constexpr uint8_t REG_CTRL9 = 0x0A;  // comandos (protocolo CTRL9)
static constexpr uint8_t REG_CAL1_L = 0x0B;  // CAL1..CAL4 (16-bit LE)
static constexpr uint8_t REG_FIFO_WTM  = 0x13;  // watermark (amostras)
static constexpr uint8_t REG_FIFO_CTRL = 0x14;  // [3:2] tamanho, [1:0] modo
static constexpr uint8_t REG_FIFO_CNT  = 0x15;  // LSB; MSB em FIFO_STATUS[1:0]
static constexpr uint8_t REG_FIFO_DATA = 0x17;
static constexpr uint8_t REG_STATUSINT = 0x2D;  // bit7 = CmdDone do CTRL9
static constexpr uint8_t REG_STATUS1 = 0x2F;
static constexpr uint8_t REG_AX_L   = 0x35;
static constexpr uint8_t REG_RESET  = 0x60;

inline i2c_master_dev_handle_t dev() {
    static i2c_master_dev_handle_t s_dev = nullptr;
    static bool s_tried = false;
    return WatchI2c::cachedDevice(kAddr, &s_dev, &s_tried);
}

inline bool wr(uint8_t reg, uint8_t val) {
    return WatchI2c::writeReg8(dev(), reg, val);
}

inline bool wr16(uint8_t regLo, uint16_t v) {
    uint8_t buf[3] = {regLo, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
    return i2c_master_transmit(dev(), buf, 3, 20) == ESP_OK;
}

inline bool rd(uint8_t reg, uint8_t* out, size_t len) {
    return WatchI2c::readRegs(dev(), reg, out, len);
}

/// Reset + validacao (WHO_AM_I). Nao configura sensores.
inline bool init() {
    uint8_t who = 0;
    if (!rd(REG_WHO, &who, 1) || who != 0x05) {
        ESP_LOGE("celer.imu", "QMI8658 nao respondeu (who=0x%02X)", who);
        return false;
    }
    wr(REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(15));
    return true;
}

/// Modo idle: accel ±8g a 30 Hz + engine AnyMotion (calThr em 1/32 g:
/// 0x20 = poucos falsos positivos, 0x10 = medio, 0x08 = sensivel).
/// Gyro fica desligado (economia) — o modo full (jogos) e do F4.
inline bool idleAccel30Hz(uint16_t calThr) {
    bool ok = true;
    ok = ok && wr(REG_CTRL7, 0x00);            // tudo off p/ reconfigurar
    ok = ok && wr(REG_CTRL2, 0x23);            // ±8g, ODR 30.12 Hz
    ok = ok && wr(REG_CTRL5, 0x01);            // LPF
    ok = ok && wr16(REG_CAL1_L, calThr);       // threshold AnyMotion XYZ
    ok = ok && wr16(REG_CAL1_L + 2, calThr);
    ok = ok && wr16(REG_CAL1_L + 4, calThr);
    ok = ok && wr16(REG_CAL1_L + 6, 5);        // CAL4 = 5
    ok = ok && wr(REG_CTRL8, 0x02);            // liga o engine
    ok = ok && wr(REG_CTRL1, 0x48);            // auto-inc + INT1 enable
    ok = ok && wr(REG_CTRL7, 0x01);            // accel on
    return ok;
}

/// Le o accel em g (escala ±8g). false se o bus falhou.
inline bool readAccelG(float* x, float* y, float* z) {
    uint8_t b[6] = {0, 0, 0, 0, 0, 0};
    if (!rd(REG_AX_L, b, 6)) return false;
    constexpr float kScale = 8.0f / 32768.0f;
    int16_t rx = (int16_t)(b[0] | (b[1] << 8));
    int16_t ry = (int16_t)(b[2] | (b[3] << 8));
    int16_t rz = (int16_t)(b[4] | (b[5] << 8));
    *x = rx * kScale;
    *y = ry * kScale;
    *z = rz * kScale;
    return true;
}

// ---- FIFO (lotes com a tela apagada) ----------------------------------------
static constexpr uint8_t kFifoCtrl = (0x01 << 2) | 0x02;  // 32 amostras, modo stream
static constexpr int kFifoMax = 32;
static constexpr uint8_t CMD_ACK = 0x00, CMD_RST_FIFO = 0x04, CMD_REQ_FIFO = 0x05;

/// Comando CTRL9: escreve, espera CmdDone (STATUSINT bit7), responde ACK e
/// espera o bit baixar. false = chip nao confirmou.
inline bool ctrl9(uint8_t cmd) {
    if (!wr(REG_CTRL9, cmd)) return false;
    uint8_t st = 0;
    bool done = false;
    for (int i = 0; i < 20 && !done; i++) {
        if (rd(REG_STATUSINT, &st, 1) && (st & 0x80)) done = true;
        else vTaskDelay(pdMS_TO_TICKS(1));
    }
    wr(REG_CTRL9, CMD_ACK);
    for (int i = 0; i < 20; i++) {
        if (rd(REG_STATUSINT, &st, 1) && !(st & 0x80)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return done;
}

/// Liga a FIFO em stream (guarda as ultimas 32 amostras do accel) e esvazia.
inline bool fifoStart() {
    return wr(REG_FIFO_WTM, 8) && wr(REG_FIFO_CTRL, kFifoCtrl) && ctrl9(CMD_RST_FIFO);
}

/// FIFO em bypass (desligada).
inline void fifoStop() { wr(REG_FIFO_CTRL, 0x00); }

/// Esvazia a FIFO sem ler (amostras ja consumidas pela leitura direta).
inline bool fifoReset() { return ctrl9(CMD_RST_FIFO); }

/// Le ate max amostras do accel (em g, ordem de chegada). -1 = falha no bus
/// ou no comando; 0 = vazia. Contador do chip em palavras de 2 bytes; so
/// accel ligado = 6 bytes por amostra.
inline int fifoReadAccelG(float (*out)[3], int max) {
    uint8_t c[2] = {0, 0};
    if (!rd(REG_FIFO_CNT, c, 2)) return -1;
    int n = ((((int)c[1] & 0x03) << 8) | c[0]) * 2 / 6;
    if (n <= 0) return 0;
    if (n > max) n = max;
    if (n > kFifoMax) n = kFifoMax;
    if (!ctrl9(CMD_REQ_FIFO)) {
        wr(REG_FIFO_CTRL, kFifoCtrl);
        return -1;
    }
    uint8_t b[kFifoMax * 6];
    const bool ok = rd(REG_FIFO_DATA, b, (size_t)n * 6);
    wr(REG_FIFO_CTRL, kFifoCtrl);  // sai do FIFO_RD_MODE
    if (!ok) return -1;
    constexpr float kScale = 8.0f / 32768.0f;
    for (int i = 0; i < n; i++) {
        const uint8_t* p = b + i * 6;
        out[i][0] = (int16_t)(p[0] | (p[1] << 8)) * kScale;
        out[i][1] = (int16_t)(p[2] | (p[3] << 8)) * kScale;
        out[i][2] = (int16_t)(p[4] | (p[5] << 8)) * kScale;
    }
    return n;
}

/// Temperatura do die (regs 0x33/0x34, raw/256 °C) — leitura on-demand.
inline bool readTemp(float* c) {
    uint8_t b[2] = {0, 0};
    if (!rd(0x33, b, 2)) return false;
    *c = (int16_t)(b[0] | (b[1] << 8)) / 256.0f;
    return true;
}

/// Limpa a INT do AnyMotion (o nivel segura ate ler STATUS1).
inline void clearMotionIrq() {
    uint8_t st = 0;
    rd(REG_STATUS1, &st, 1);
}

/// Tudo off (pre-deep-sleep). A task do Motion para de polar (s_imuOk).
inline void powerDown() {
    wr(REG_CTRL8, 0x00);
    wr(REG_CTRL7, 0x00);
}

}  // namespace Qmi8658

#endif  // CELER_BOARDS_WAVESHARE_WATCH_QMI8658_H
