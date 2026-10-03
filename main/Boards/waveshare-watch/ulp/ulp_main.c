// Sentinela ULP-RISC-V do watch durante o deep sleep. Roda no
// coprocessador a cada ciclo do timer (~100 ms) e vigia:
//   - PEK do AXP2101 (tecla PWR): sem IRQ em GPIO no watch, sem este
//     programa ela NAO acorda o relogio do deep sleep;
//   - INT1 do QMI8658 (GPIO21, ativo-baixo): servida por I2C (ler STATUS1
//     limpa o nivel) e filtrada por janela — dois eventos em ~2 s sao um
//     gesto de levantar o pulso; esbarramento isolado nao acorda;
//   - VBAT do AXP2101: leitura periodica, celula fraca acorda o main.
// O bus I2C0 (SDA=15, SCL=14) e bit-banged em open-drain com pull-up de
// RTC: o controlador I2C de hardware do ULP so fala GPIO 0..3.
//
// A mailbox (globals abaixo) vive na RTC slow mem e sobrevive ao deep
// sleep; o WatchUlp.cpp a le no boot seguinte pelos simbolos ulp_* do
// header gerado ulp_watch.h. Globals "static" ficam privados do programa.
// Cada arm() recarrega o binario: .data volta aos defaults e o WatchUlp
// sobrescreve a config antes do run().

#include <stdint.h>
#include <stdbool.h>
#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
#include "ulp_riscv_gpio.h"
#include "UlpWatch.h"

// ---- mailbox (RTC slow) ----------------------------------------------------
uint32_t mb_magic = 0;    // ULP_MB_MAGIC a partir do primeiro ciclo
uint32_t mb_run = 0;      // ciclos executados (diagnostico)
uint32_t mb_wake = 0;     // ULP_WAKE_* — motivo do ultimo wake pedido
uint32_t mb_bat_mv = 0;   // ultima VBAT valida lida
uint32_t mb_i2c_err = 0;  // transacoes falhas acumuladas

// ---- config (defaults; o WatchUlp.cpp sobrescreve antes do run) -----------
uint32_t cfg_enable = ULP_EN_PEK | ULP_EN_RAISE | ULP_EN_BAT;
uint32_t cfg_bat_lo_mv = 3300;
uint32_t cfg_raise_win = 20;    // janela do filtro do raise, em ciclos
uint32_t cfg_bat_every = 3000;  // leitura de VBAT a cada N ciclos

// ---- estado interno (persiste entre ciclos; re-init a cada arm) -----------
static uint32_t s_raise_last = 0;
static bool s_raise_have = false;
static bool s_bus_dead = false;

// ---- I2C bit-bang (open-drain: escrever 1 solta a linha) ------------------
static inline void scl_lo(void) { ulp_riscv_gpio_output_level(ULP_PIN_SCL, 0); }
static inline void scl_hi(void) { ulp_riscv_gpio_output_level(ULP_PIN_SCL, 1); }
static inline void sda_lo(void) { ulp_riscv_gpio_output_level(ULP_PIN_SDA, 0); }
static inline void sda_hi(void) { ulp_riscv_gpio_output_level(ULP_PIN_SDA, 1); }
static inline bool sda_in(void) { return ulp_riscv_gpio_get_level(ULP_PIN_SDA) != 0; }

static void pins_init(void) {
    ulp_riscv_gpio_init(ULP_PIN_SCL);
    ulp_riscv_gpio_init(ULP_PIN_SDA);
    ulp_riscv_gpio_pulldown_disable(ULP_PIN_SCL);
    ulp_riscv_gpio_pulldown_disable(ULP_PIN_SDA);
    ulp_riscv_gpio_pullup(ULP_PIN_SCL);
    ulp_riscv_gpio_pullup(ULP_PIN_SDA);
    ulp_riscv_gpio_input_enable(ULP_PIN_SCL);
    ulp_riscv_gpio_input_enable(ULP_PIN_SDA);
    ulp_riscv_gpio_output_enable(ULP_PIN_SCL);
    ulp_riscv_gpio_output_enable(ULP_PIN_SDA);
    ulp_riscv_gpio_set_output_mode(ULP_PIN_SCL, RTCIO_MODE_OUTPUT_OD);
    ulp_riscv_gpio_set_output_mode(ULP_PIN_SDA, RTCIO_MODE_OUTPUT_OD);

    ulp_riscv_gpio_init(ULP_PIN_IMU_INT);
    ulp_riscv_gpio_input_enable(ULP_PIN_IMU_INT);
    ulp_riscv_gpio_pulldown_disable(ULP_PIN_IMU_INT);
    ulp_riscv_gpio_pullup(ULP_PIN_IMU_INT);
}

// Linhas livres (altas)? Se um slave prendeu a SDA, 9 pulsos de SCL soltam.
static bool bus_ready(void) {
    scl_hi();
    sda_hi();
    ulp_riscv_delay_us(2 * ULP_I2C_HALF_US);
    if (sda_in()) return true;
    for (int i = 0; i < 9; i++) {
        scl_lo();
        ulp_riscv_delay_us(ULP_I2C_HALF_US);
        scl_hi();
        ulp_riscv_delay_us(ULP_I2C_HALF_US);
    }
    sda_hi();
    ulp_riscv_delay_us(2 * ULP_I2C_HALF_US);
    return sda_in();
}

static void i2c_start(void) {  // ambas altas -> SDA cai com SCL alto
    sda_lo();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    scl_lo();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
}

static void i2c_restart(void) {  // com SCL baixo: solta SDA, SCL sobe, SDA cai
    sda_hi();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    scl_hi();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    sda_lo();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    scl_lo();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
}

static void i2c_stop(void) {  // com SCL baixo: SDA baixa, SCL sobe, SDA solta
    sda_lo();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    scl_hi();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    sda_hi();
    ulp_riscv_delay_us(2 * ULP_I2C_HALF_US);
}

// Escreve 8 bits + clock do ACK; true = slave puxou a SDA baixa.
static bool i2c_write_byte(uint8_t v) {
    for (int i = 7; i >= 0; i--) {
        if ((v >> i) & 1) sda_hi(); else sda_lo();
        ulp_riscv_delay_us(ULP_I2C_HALF_US);
        scl_hi();
        ulp_riscv_delay_us(ULP_I2C_HALF_US);
        scl_lo();
    }
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    sda_hi();  // 9o clock: solta p/ o slave responder
    scl_hi();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    bool nack = sda_in();
    scl_lo();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    return !nack;
}

static uint8_t i2c_read_byte(bool ack) {
    uint8_t v = 0;
    sda_hi();  // slave conduz
    for (int i = 7; i >= 0; i--) {
        scl_hi();
        ulp_riscv_delay_us(ULP_I2C_HALF_US);
        v = (uint8_t)((v << 1) | (sda_in() ? 1 : 0));
        scl_lo();
        ulp_riscv_delay_us(ULP_I2C_HALF_US);
    }
    if (ack) sda_lo(); else sda_hi();  // ACK na ultima? NACK encerra a leitura
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    scl_hi();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    scl_lo();
    ulp_riscv_delay_us(ULP_I2C_HALF_US);
    sda_hi();
    return v;
}

// Le n bytes de um registrador de 8 bits (write reg + repeated start + read).
static bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t* out, int n) {
    if (!bus_ready()) return false;
    i2c_start();
    if (!i2c_write_byte((uint8_t)(addr << 1))) {
        i2c_stop();
        return false;
    }
    if (!i2c_write_byte(reg)) {
        i2c_stop();
        return false;
    }
    i2c_restart();
    if (!i2c_write_byte((uint8_t)((addr << 1) | 1))) {
        i2c_stop();
        return false;
    }
    for (int i = 0; i < n; i++) out[i] = i2c_read_byte(i != n - 1);
    i2c_stop();
    return true;
}

int main(void) {
    if (mb_magic != ULP_MB_MAGIC) {  // primeiro ciclo desde o arm()
        mb_magic = ULP_MB_MAGIC;
        mb_run = 0;
        mb_wake = ULP_WAKE_NONE;
        mb_bat_mv = 0;
        mb_i2c_err = 0;
        s_raise_last = 0;
        s_raise_have = false;
        s_bus_dead = false;
        pins_init();
    }
    mb_run++;
    if (s_bus_dead) return 0;

    // PEK: bit4 do IRQ_ST0 e borda de descida da tecla. Acorda na hora — o
    // boot le o motivo na mailbox e acende a tela.
    uint8_t st0 = 0;
    if (!i2c_read_reg(ULP_AXP_ADDR, ULP_AXP_IRQ_ST0, &st0, 1)) {
        // Sem ACK (bus sem pull-up suficiente, slave preso): ~10 s de falhas
        // seguidas e a sentinela desiste do I2C — BOOT/timer seguem valendo.
        if (++mb_i2c_err > 100) s_bus_dead = true;
        return 0;
    }
    if ((cfg_enable & ULP_EN_PEK) && (st0 & ULP_AXP_PEK_PRESS)) {
        mb_wake = ULP_WAKE_PEK;
        ulp_riscv_wakeup_main_processor();
        return 0;
    }

    // Raise: nivel baixo na INT1 (segura ate ler STATUS1). Servir limpa e
    // rearma o engine; cada evento e 1 hit e 2 hits na janela sao o gesto.
    if ((cfg_enable & ULP_EN_RAISE) && ulp_riscv_gpio_get_level(ULP_PIN_IMU_INT) == 0) {
        uint8_t st1 = 0;
        i2c_read_reg(ULP_QMI_ADDR, ULP_QMI_STATUS1, &st1, 1);
        if (s_raise_have && (mb_run - s_raise_last) <= cfg_raise_win) {
            mb_wake = ULP_WAKE_RAISE;
            ulp_riscv_wakeup_main_processor();
            return 0;
        }
        s_raise_last = mb_run;
        s_raise_have = true;
    }

    // Bateria: leitura periodica; celula abaixo do limiar acorda o main.
    if ((cfg_enable & ULP_EN_BAT) && (mb_run % cfg_bat_every) == 0) {
        uint8_t b[2] = {0, 0};
        if (i2c_read_reg(ULP_AXP_ADDR, ULP_AXP_VBAT_H, b, 2)) {
            uint32_t mv = (((uint32_t)b[0] << 8) | b[1]) & 0x3FFF;
            if (mv >= 2500 && mv <= 5000) {  // faixa util de Li-ion 1S
                mb_bat_mv = mv;
                if (mv < cfg_bat_lo_mv) {
                    mb_wake = ULP_WAKE_LOWBAT;
                    ulp_riscv_wakeup_main_processor();
                    return 0;
                }
            }
        }
    }
    return 0;  // halt automatico; main() roda de novo no proximo ciclo
}
