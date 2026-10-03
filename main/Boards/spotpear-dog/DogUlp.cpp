// Lado main do watchdog de bateria do cao. Contrato: DogUlp.h; programa:
// ulp/ulp_main.c. O limiar viaja em CONTAGENS CRUAS porque o ULP nao tem a
// curva de calibracao — aqui ela e invertida por busca binaria com o mesmo
// esquema curve-fitting do BoardIO::batteryMv (ADC1, 12 dB, 12 bits).

#include "DogUlp.h"
#include "Boards/Board.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "hal/adc_types.h"
#include "ulp_adc.h"
#include "ulp_riscv.h"

extern "C" {
#include "ulp_dog.h"
extern const uint8_t ulp_dog_bin_start[] asm("_binary_ulp_dog_bin_start");
extern const uint8_t ulp_dog_bin_end[] asm("_binary_ulp_dog_bin_end");
}

namespace {

// Ciclo do watchdog: bateria muda devagar; 60 s basta e mantem o duty do
// ULP (e o RTC_PERIPH ligado) em ~zero na media.
constexpr uint32_t kPeriodUs = 60 * 1000000;
constexpr int kLowCellMv = 3300;  // Li-ion 1S: abaixo disto, avisa

// Menor contagem crua cuja tensao calibrada alcanca pinMv (curva e
// monotonica crescente): abaixo dela o pino esta abaixo do limiar.
int rawForMv(int pinMv) {
    adc_cali_handle_t cali = nullptr;
    adc_cali_curve_fitting_config_t c = {};
    c.unit_id = ADC_UNIT_1;
    c.atten = ADC_ATTEN_DB_12;
    c.bitwidth = ADC_BITWIDTH_12;
    if (adc_cali_create_scheme_curve_fitting(&c, &cali) != ESP_OK) return 0;

    int lo = 0, hi = 4095, best = 0;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        int mv = 0;
        if (adc_cali_raw_to_voltage(cali, mid, &mv) != ESP_OK) break;
        if (mv >= pinMv) {
            best = mid;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    adc_cali_delete_scheme_curve_fitting(cali);
    return best;
}

}  // namespace

namespace DogUlp {

int arm() {
    // ADC1_CH1 = GPIO2 (divisor 2:1) — mesmos parametros do BoardIO.
    ulp_adc_cfg_t acfg = {};
    acfg.adc_n = ADC_UNIT_1;
    acfg.channel = ADC_CHANNEL_1;
    acfg.width = ADC_BITWIDTH_DEFAULT;
    acfg.atten = ADC_ATTEN_DB_12;
    acfg.ulp_mode = ADC_ULP_MODE_RISCV;
    if (ulp_adc_init(&acfg) != ESP_OK) {
        ESP_LOGE("celer.ulp", "ADC nao configurou p/ o ULP: watchdog desligado");
        return 0;
    }

    // Limiar no pino: celula alvo escalada pelo divisor do perfil.
    const int scale = Board::profile().batteryScalePct;
    const int pinMv = scale > 0 ? kLowCellMv * 100 / scale : 0;
    const int thr = pinMv > 0 ? rawForMv(pinMv) : 0;
    if (thr <= 0) {
        ESP_LOGE("celer.ulp", "limiar nao calibrou (%d mV no pino): watchdog desligado", pinMv);
        return 0;
    }

    if (ulp_riscv_load_binary(ulp_dog_bin_start,
                              (size_t)(ulp_dog_bin_end - ulp_dog_bin_start)) != ESP_OK) {
        ESP_LOGE("celer.ulp", "binario ULP nao carregou: watchdog desligado");
        return 0;
    }
    ulp_cfg_thresh_raw = (uint32_t)thr;
    ulp_set_wakeup_period(0, kPeriodUs);
    if (ulp_riscv_run() != ESP_OK) {
        ESP_LOGE("celer.ulp", "ULP nao subiu: watchdog desligado");
        return 0;
    }
    // SAR ADC nas maos do ULP durante o sono exige RTC_PERIPH alimentado.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ulp_wakeup();
    ESP_LOGI("celer.ulp", "watchdog armado (ADC1_CH1 < %d cru ~= %d mV na celula, ciclo %d s)",
             thr, kLowCellMv, (int)(kPeriodUs / 1000000));
    return 1;
}

int wake() {
    static bool s_consumed = false;
    if (s_consumed) return 0;
    s_consumed = true;
    if ((esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_ULP)) == 0) return 0;
    const int r = (int)ulp_mb_wake;
    ulp_mb_wake = 0;
    if (r != 0) {
        ESP_LOGW("celer.ulp", "acordou pelo ULP: bateria fraca (media %d cru)",
                 (int)ulp_mb_raw);
    }
    return r;
}

}  // namespace DogUlp
