// Watchdog de bateria do cao robotico durante o deep sleep. O cao so dorme
// por System.deepSleep(ms) do JS — o timer sempre acorda; este programa
// acrescenta o wake ANTECIPADO quando a celula despenca: a cada ciclo
// (~60 s) o ULP-RISC-V le o ADC1_CH1 (GPIO2, divisor 2:1 da placa) e
// compara a media contra um limiar em CONTAGENS CRUAS, calibrado pelo
// DogUlp.cpp no arm() com a mesma curva do BoardIO::batteryMv.
//
// Mailbox = os globals mb_* abaixo (RTC slow mem): o header gerado
// ulp_dog.h os expoe com prefixo ulp_ para o lado main.

#include <stdint.h>
#include "ulp_riscv_utils.h"
#include "ulp_riscv_adc_ulp_core.h"
#include "hal/adc_types.h"

uint32_t mb_wake = 0;         // 1 = bateria fraca (motivo do wake)
uint32_t mb_raw = 0;          // ultima media, em contagens
uint32_t cfg_thresh_raw = 0;  // limiar cru (DogUlp calibra no arm)
uint32_t cfg_samples = 4;     // leituras por ciclo (media)

int main(void) {
    mb_wake = 0;
    uint32_t acc = 0, n = 0;
    for (uint32_t i = 0; i < cfg_samples; i++) {
        int32_t v = ulp_riscv_adc_read_channel(ADC_UNIT_1, ADC_CHANNEL_1);
        if (v >= 0) {
            acc += (uint32_t)v;
            n++;
        }
    }
    if (n > 0 && cfg_thresh_raw > 0) {
        mb_raw = acc / n;
        if (mb_raw < cfg_thresh_raw) {
            mb_wake = 1;
            ulp_riscv_wakeup_main_processor();
        }
    }
    return 0;  // halt automatico; roda de novo no proximo ciclo do timer
}
