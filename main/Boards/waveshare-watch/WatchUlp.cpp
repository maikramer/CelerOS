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

// Ciclo da sentinela: 100 ms e o compromisso entre resposta da tecla PWR
// (<=100 ms ate o wake) e o tempo de ULP ativo por segundo (~1 ms de I2C
// bit-banged por ciclo = ~1% de duty no coprocessador).
constexpr uint32_t kPeriodUs = 100000;
constexpr uint32_t kRaiseWinCycles = 20;    // 2 eventos em ~2 s = gesto
constexpr uint32_t kBatEveryCycles = 3000;  // VBAT a cada ~5 min
constexpr uint16_t kRaiseThr = 0x20;        // AnyMotion firme (gesto deliberado)

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
    uint32_t enable = ULP_EN_PEK | ULP_EN_BAT;
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
    ulp_cfg_bat_lo_mv = 3300;
    ulp_cfg_raise_win = kRaiseWinCycles;
    ulp_cfg_bat_every = kBatEveryCycles;

    ulp_set_wakeup_period(0, kPeriodUs);
    if (ulp_riscv_run() != ESP_OK) {
        ESP_LOGE("celer.ulp", "ULP nao subiu: sentinela desligada");
        return 0;
    }
    // RTC perifericos ligados durante o sono: bit-bang nas RTC GPIO exige o
    // dominio RTC_PERIPH alimentado (custa dezenas de uA; sem isso o bus morre).
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ulp_wakeup();
    ESP_LOGI("celer.ulp", "sentinela armada (PEK%s, VBAT < %d mV, ciclo %d ms)",
             raiseOn ? "+raise filtrado" : "", (int)ulp_cfg_bat_lo_mv, (int)(kPeriodUs / 1000));
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
    ESP_LOGI("celer.ulp", "acordou pelo ULP: motivo %d (%d ciclos, VBAT %d mV, %d erros I2C)",
             r, (int)ulp_mb_run, (int)ulp_mb_bat_mv, (int)ulp_mb_i2c_err);
    return r;
}

}  // namespace WatchUlp
