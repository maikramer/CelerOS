// Lado main da sentinela ULP do watch. O programa (ulp/ulp_main.c) e
// carregado na RTC slow mem a cada deep sleep; a mailbox dele sobrevive ao
// sono e e lida aqui no boot seguinte. Comentarios do contrato: WatchUlp.h.

#include "WatchUlp.h"
#include "ulp/UlpWatch.h"
#include "Qmi8658.h"

#include "../../Utils/CelerSettings.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "ulp_riscv.h"

// Header gerado pelo build do ULP (ulp_embed_binary no CMakeLists): declara
// os globals do programa com prefixo ulp_ (ulp_mb_wake, ulp_cfg_enable...)
// e os limites do binario embutido.
extern "C" {
#include "ulp_watch.h"
extern const uint8_t ulp_watch_bin_start[] asm("_binary_ulp_watch_bin_start");
extern const uint8_t ulp_watch_bin_end[] asm("_binary_ulp_watch_bin_end");
}

namespace {

// Ciclo adaptativo da sentinela: rapido com o pulso mexendo (resposta da
// tecla PWR <=100 ms), lento depois de kCalmMs sem INT do IMU (mesa,
// noite: ~2,5x menos ciclos de ULP + I2C bit-banged). O PEK fica latchado
// no AXP2101, entao o lento so atrasa o wake (<=250 ms), nao perde toque.
constexpr uint32_t kFastUs = 100000;
constexpr uint32_t kSlowUs = 250000;
constexpr uint32_t kCalmMs = 60000;
constexpr uint32_t kRaiseWinMs = 2000;     // 2 eventos em ~2 s = gesto
constexpr uint32_t kBatEveryMs = 300000;   // VBAT a cada ~5 min
constexpr uint32_t kVbusEveryMs = 1000;    // cabo plugado acorda em ~1 s
constexpr uint32_t kBatLoMv = 3300;        // aviso (borda, uma vez)
constexpr uint32_t kBatCritMv = 3150;      // sobrevivencia: IMU off no ULP
constexpr uint16_t kRaiseThr = 0x20;       // AnyMotion firme (gesto deliberado)

// Periodo em ciclos do RTC slow, como o ulp_set_wakeup_period o grava (a
// calibracao do clock so existe no main): o ULP so copia o valor pronto.
uint32_t periodCycles(uint32_t us) {
    ulp_set_wakeup_period(0, us);
    return REG_GET_FIELD(RTC_CNTL_ULP_CP_TIMER_1_REG, RTC_CNTL_ULP_CP_TIMER_SLP_CYCLE);
}

}  // namespace

namespace WatchUlp {

int arm() {
    esp_err_t err = ulp_riscv_load_binary(ulp_watch_bin_start,
                                          (size_t)(ulp_watch_bin_end - ulp_watch_bin_start));
    if (err != ESP_OK) {
        ESP_LOGE("celer.ulp", "binario nao carregou (0x%x): sentinela desligada", err);
        return 0;
    }

    // Config da mailbox ANTES do run(): defaults do programa sao
    // sobrescritos e o ciclo de clear do .data ja passou no load.
    uint32_t enable = ULP_EN_PEK | ULP_EN_BAT | ULP_EN_VBUS;
    const bool raiseOn = CelerSettings::get("raise_wake", "1") == "1";
    if (raiseOn) {
        enable |= ULP_EN_RAISE;
        // O sleepPrep desligou o IMU (ou o deixou no modo cru de imu_wake):
        // com o ULP no comando ele volta em AnyMotion firme — a INT1 e
        // servida e filtrada pelo coprocessador, nao pelo EXT1.
        Qmi8658::idleAccel30Hz(kRaiseThr);
        Qmi8658::clearMotionIrq();
    }
    ulp_cfg_enable = enable;
    ulp_cfg_bat_lo_mv = kBatLoMv;
    ulp_cfg_bat_crit_mv = kBatCritMv;
    ulp_cfg_raise_win_ms = kRaiseWinMs;
    ulp_cfg_bat_every_ms = kBatEveryMs;
    ulp_cfg_vbus_every_ms = kVbusEveryMs;
    ulp_cfg_calm_ms = kCalmMs;
    ulp_cfg_fast_ms = kFastUs / 1000;
    ulp_cfg_slow_ms = kSlowUs / 1000;
    ulp_cfg_slow_cyc = periodCycles(kSlowUs);
    ulp_cfg_fast_cyc = periodCycles(kFastUs);  // por ultimo: o timer parte no rapido

    if (ulp_riscv_run() != ESP_OK) {
        ESP_LOGE("celer.ulp", "ULP nao subiu: sentinela desligada");
        return 0;
    }
    // RTC perifericos ligados durante o sono: bit-bang nas RTC GPIO exige o
    // dominio RTC_PERIPH alimentado (custa dezenas de uA; sem isso o bus morre).
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ulp_wakeup();
    ESP_LOGI("celer.ulp", "sentinela armada (PEK+VBUS%s, VBAT %d/%d mV, ciclo %d/%d ms)",
             raiseOn ? "+raise filtrado" : "", (int)kBatLoMv, (int)kBatCritMv,
             (int)(kFastUs / 1000), (int)(kSlowUs / 1000));
    return 1 | (raiseOn ? 2 : 0);
}

int wake() {
    static bool s_consumed = false;
    if (s_consumed) return ULP_WAKE_NONE;
    s_consumed = true;
    // IDF 6.1: causas em bitmask (a API singular esp_sleep_get_wakeup_cause
    // esta deprecada). Fora de boot pos-deep-sleep devolve 0.
    if ((esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_ULP)) == 0) return ULP_WAKE_NONE;
    if (ulp_mb_magic != ULP_MB_MAGIC) return ULP_WAKE_NONE;
    const int r = (int)ulp_mb_wake;
    ulp_mb_wake = ULP_WAKE_NONE;  // nao deixa o motivo vazar para o proximo boot
    ESP_LOGI("celer.ulp",
             "acordou pelo ULP: motivo %d (%d ciclos, %d lentos, flags 0x%x, VBAT %d mV, %d erros I2C)",
             r, (int)ulp_mb_run, (int)ulp_mb_slow, (unsigned)ulp_mb_flags, (int)ulp_mb_bat_mv,
             (int)ulp_mb_i2c_err);
    return r;
}

}  // namespace WatchUlp
