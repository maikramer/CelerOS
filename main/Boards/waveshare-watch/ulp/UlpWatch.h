#ifndef CELER_BOARDS_WAVESHARE_WATCH_ULP_ULPWATCH_H
#define CELER_BOARDS_WAVESHARE_WATCH_ULP_ULPWATCH_H

// Constantes compartilhadas entre o programa ULP (ulp/ulp_main.c, rodando no
// coprocessador durante o deep sleep) e o lado main (WatchUlp.cpp). A
// "mailbox" sao os globals do programa ULP: eles vivem na RTC slow mem,
// sobrevivem ao deep sleep e aparecem no header gerado ulp_watch.h com
// prefixo ulp_ (ulp_mb_wake, ulp_cfg_enable...).

// Bus I2C0 do watch (ambos RTC GPIO: o controlador I2C de hardware do ULP
// so fala GPIO 0..3, logo o acesso e bit-bang) e INT1 do IMU.
#define ULP_PIN_SCL 14
#define ULP_PIN_SDA 15
#define ULP_PIN_IMU_INT 21  // INT1 do QMI8658, ativo-baixo

// Enderecos/registradores usados pela sentinela (espelham Axp2101.h/Qmi8658.h,
// que nao podem ser incluidos no build do ULP — dependem do driver do IDF).
#define ULP_AXP_ADDR 0x34
#define ULP_AXP_STATUS1 0x00  // bit5 = VBUS good (mesmo bit do readChargeState)
#define ULP_AXP_VBUS_GOOD 0x20
#define ULP_AXP_IRQ_ST0 0x48  // status das IRQs (leitura limpa os bits)
#define ULP_AXP_PEK_PRESS 0x10  // bit4: borda de descida da tecla PWR
#define ULP_AXP_VBAT_H 0x34
#define ULP_AXP_VBAT_L 0x35
#define ULP_QMI_ADDR 0x6B
#define ULP_QMI_STATUS1 0x2F  // ler limpa a INT do AnyMotion
#define ULP_QMI_CTRL7 0x08    // 0 = accel/gyro desligados (modo sobrevivencia)

// Meia-bit do I2C bit-banged (~60 kHz): os pull-ups de RTC sao fracos e a
// capacitancia do bus compartilhado (touch+PMU+RTC+IMU+codec) e alta.
#define ULP_I2C_HALF_US 8

// Motivos de wake escritos na mailbox (ulp_mb_wake; 0 = nenhum).
#define ULP_WAKE_NONE 0
#define ULP_WAKE_PEK 1     // tecla PWR apertada (poll do AXP2101)
#define ULP_WAKE_RAISE 2   // gesto de levantar o pulso (filtro de janela)
#define ULP_WAKE_LOWBAT 3  // tensao da celula cruzou o limiar (borda, 1x)
#define ULP_WAKE_VBUS 4    // cabo USB plugado durante o sono

// Bits de ulp_cfg_enable.
#define ULP_EN_PEK 1u
#define ULP_EN_RAISE 2u
#define ULP_EN_BAT 4u
#define ULP_EN_VBUS 8u

// Bits de ulp_mb_flags (diagnostico lido no boot).
#define ULP_FLAG_SLOW 1u      // ciclo lento ativo (pulso parado)
#define ULP_FLAG_SURVIVE 2u   // celula critica: IMU desligado, so PEK/VBUS
#define ULP_FLAG_BUS_DEAD 4u  // I2C desistiu (sem ACK por ~10 s)

// Validade da mailbox no boot pos-deep-sleep ("ULP1").
#define ULP_MB_MAGIC 0x55504C31u

#endif  // CELER_BOARDS_WAVESHARE_WATCH_ULP_ULPWATCH_H
