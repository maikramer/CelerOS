#include "Arduino.h"

#include <stdarg.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#if CONFIG_IDF_TARGET_ESP32S3
  #include "driver/temperature_sensor.h"
  #define CELEROS_HAS_TSENS 1
#endif
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_private/esp_clk.h"
#include "esp_heap_caps.h"
#include "USBDevice/LogSink.h"

CelerSerial Serial;
CelerEsp ESP;

// --- Tempo ------------------------------------------------------------------

uint32_t millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

uint32_t micros(void) {
    return (uint32_t)esp_timer_get_time();
}

void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void delayMicroseconds(uint32_t us) {
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < (int64_t)us) {
        asm volatile ("nop");
    }
}

// --- GPIO -------------------------------------------------------------------

void pinMode(int pin, uint8_t mode) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    switch (mode) {
        case INPUT:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case INPUT_PULLUP:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_ENABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case INPUT_PULLDOWN:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        case OUTPUT:
        default:
            cfg.mode = GPIO_MODE_INPUT_OUTPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
    }
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void digitalWrite(int pin, uint8_t val) {
    gpio_set_level((gpio_num_t)pin, val ? 1 : 0);
}

int digitalRead(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

// --- analogRead: adc_oneshot com canais criados sob demanda ------------------

static adc_oneshot_unit_handle_t s_adc_unit[2] = {nullptr, nullptr};

static bool adc_io_to_unit_channel(int pin, adc_unit_t* unit, adc_channel_t* chan) {
    // API oficial: valida o GPIO para o alvo corrente (mapa ADC difere por chip)
    return adc_oneshot_io_to_channel(pin, unit, chan) == ESP_OK;
}

int analogRead(int pin) { return analogReadAtten(pin, ADC_ATTEN_DB_12); }

// Leitura com atenuacao escolhida: 0dB (~0..0,95V) da resolucao a sinais
// fracos, como o LDR da CYD, que fica abaixo do piso do ADC a 12dB
int analogReadAtten(int pin, int atten) {
    adc_unit_t unit;
    adc_channel_t chan;
    if (!adc_io_to_unit_channel(pin, &unit, &chan)) return 0;

    int idx = (unit == ADC_UNIT_1) ? 0 : 1;
    if (!s_adc_unit[idx]) {
        adc_oneshot_unit_init_cfg_t init = {};
        init.unit_id = unit;
        if (adc_oneshot_new_unit(&init, &s_adc_unit[idx]) != ESP_OK) return 0;
    }
    adc_oneshot_chan_cfg_t cfg = {};
    cfg.bitwidth = ADC_BITWIDTH_12;
    cfg.atten = (adc_atten_t)atten;
    adc_oneshot_config_channel(s_adc_unit[idx], chan, &cfg);

    int raw = 0;
    adc_oneshot_read(s_adc_unit[idx], chan, &raw);
    return raw;
}

// --- analogWrite: LEDC com alocacao simples de canal -------------------------

// So os canais LEDC 0..2 (timers 0/1): 3..5 sao do LED RGB, 6 do tom e 7 do
// backlight (mapa em Hardware/BoardIO.h). Antes eram os 8 — um app com
// PWM podia roubar o canal do beep ou reconfigurar o timer do backlight.
#define CELER_LEDC_MAX_CH 3
static int s_ledc_pin[CELER_LEDC_MAX_CH] = {-1, -1, -1};

void analogWrite(int pin, int val) {
    int ch = -1;
    for (int i = 0; i < CELER_LEDC_MAX_CH; i++) {
        if (s_ledc_pin[i] == pin) { ch = i; break; }
        if (s_ledc_pin[i] == -1 && ch < 0) ch = i;
    }
    if (ch < 0) return;

    ledc_timer_t timer = (ledc_timer_t)(ch / 2);  // timers 0-3, 2 canais cada
    ledc_channel_t channel = (ledc_channel_t)ch;

    if (s_ledc_pin[ch] != pin) {
        ledc_timer_config_t tim = {};
        tim.speed_mode = LEDC_LOW_SPEED_MODE;
        tim.timer_num = timer;
        tim.duty_resolution = LEDC_TIMER_8_BIT;
        tim.freq_hz = 5000;
        tim.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&tim) != ESP_OK) return;

        ledc_channel_config_t cfg = {};
        cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        cfg.channel = channel;
        cfg.timer_sel = timer;
        cfg.intr_type = LEDC_INTR_DISABLE;
        cfg.gpio_num = pin;
        cfg.duty = 0;
        cfg.hpoint = 0;
        if (ledc_channel_config(&cfg) != ESP_OK) return;
        s_ledc_pin[ch] = pin;
    }

    if (val < 0) val = 0;
    if (val > 255) val = 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, (uint32_t)val);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

// --- pulseIn: polling com timeout -------------------------------------------

unsigned long pulseIn(int pin, uint8_t state, unsigned long timeout_us) {
    int64_t start = esp_timer_get_time();
    // Espera a condicao inicial (nivel diferente de "state")
    while (digitalRead(pin) == (state ? 1 : 0)) {
        if ((esp_timer_get_time() - start) >= (int64_t)timeout_us) return 0;
    }
    // Espera o pulso comecar
    while (digitalRead(pin) != (state ? 1 : 0)) {
        if ((esp_timer_get_time() - start) >= (int64_t)timeout_us) return 0;
    }
    int64_t pulse_start = esp_timer_get_time();
    while (digitalRead(pin) == (state ? 1 : 0)) {
        if ((esp_timer_get_time() - start) >= (int64_t)timeout_us) return 0;
    }
    return (unsigned long)(esp_timer_get_time() - pulse_start);
}

// --- Matematica -------------------------------------------------------------

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    long divisor = in_max - in_min;
    return (x - in_min) * (out_max - out_min) / divisor + out_min;
}

// --- Temperatura interna (S3 tem sensor dedicado) ----------------------------

float temperatureRead(void) {
#if !CELEROS_HAS_TSENS
    return 53.33f;  // ESP32 classico nao tem sensor de temperatura
#else
    static temperature_sensor_handle_t s_ts = nullptr;
    static bool s_failed = false;
    if (s_failed) return 53.33f;  // mesmo valor sentinela do arduino-esp32

    if (!s_ts) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
        if (temperature_sensor_install(&cfg, &s_ts) != ESP_OK ||
            temperature_sensor_enable(s_ts) != ESP_OK) {
            s_failed = true;
            return 53.33f;
        }
    }
    float t = 53.33f;
    temperature_sensor_get_celsius(s_ts, &t);
    return t;
#endif
}

// --- Serial ------------------------------------------------------------------

void CelerSerial::printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    // passa pelo LogSink: sessoes celerctl (modo link) nao podem receber
    // texto de log intercalado nos frames binarios
    celer_log_vprintf(fmt, args);
    va_end(args);
}

// --- ESP.* --------------------------------------------------------------------

void CelerEsp::restart(void) {
    esp_restart();
}

uint32_t CelerEsp::getHeapSize(void) {
    return heap_caps_get_total_size(MALLOC_CAP_8BIT);
}

uint32_t CelerEsp::getFreeHeap(void) {
    return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

uint32_t CelerEsp::getMinFreeHeap(void) {
    return heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
}

uint32_t CelerEsp::getMaxAllocHeap(void) {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

uint32_t CelerEsp::getPsramSize(void) {
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

uint32_t CelerEsp::getFreePsram(void) {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

uint32_t CelerEsp::getCpuFreqMHz(void) {
    return (uint32_t)(esp_clk_cpu_freq() / 1000000);
}

const char* CelerEsp::getChipModel(void) {
    static esp_chip_info_t info;
    esp_chip_info(&info);
    switch (info.model) {
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32:   return "ESP32";
        default:           return "ESP32-?";
    }
}

uint8_t CelerEsp::getChipCores(void) {
    esp_chip_info_t info;
    esp_chip_info(&info);
    return info.cores;
}

uint8_t CelerEsp::getChipRevision(void) {
    esp_chip_info_t info;
    esp_chip_info(&info);
    return info.revision / 100u;
}

uint32_t CelerEsp::getFlashChipSize(void) {
    uint32_t size = 0;
    esp_flash_get_size(NULL, &size);
    return size;
}
