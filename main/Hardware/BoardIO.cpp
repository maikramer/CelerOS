#include "BoardIO.h"

#include <Arduino.h>
#include "../Boards/Board.h"
#include "driver/ledc.h"
#include "esp_task_wdt.h"

namespace BoardIO {

namespace {
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kLedTimer = LEDC_TIMER_1;
constexpr ledc_channel_t kLedCh[3] = {LEDC_CHANNEL_3, LEDC_CHANNEL_4, LEDC_CHANNEL_5};
constexpr ledc_timer_t kToneTimer = LEDC_TIMER_2;
constexpr ledc_channel_t kToneCh = LEDC_CHANNEL_6;

bool s_ledReady = false;

bool ledInit() {
    if (s_ledReady) return true;
    const RgbLedPins& p = Board::profile().led;
    if (p.r < 0) return false;
    ledc_timer_config_t tim = {};
    tim.speed_mode = kMode;
    tim.timer_num = kLedTimer;
    tim.duty_resolution = LEDC_TIMER_8_BIT;
    tim.freq_hz = 5000;  // mesmo do timer 1 do analogWrite (canais 2..3 compartilham)
    tim.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&tim) != ESP_OK) return false;
    const int pins[3] = {p.r, p.g, p.b};
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t ch = {};
        ch.speed_mode = kMode;
        ch.channel = kLedCh[i];
        ch.timer_sel = kLedTimer;
        ch.intr_type = LEDC_INTR_DISABLE;
        ch.gpio_num = pins[i];
        ch.duty = p.activeLow ? 255 : 0;  // apagado
        if (ledc_channel_config(&ch) != ESP_OK) return false;
    }
    s_ledReady = true;
    return true;
}
}  // namespace

bool hasLed() { return Board::profile().led.r >= 0; }

void setLed(uint8_t r, uint8_t g, uint8_t b) {
    if (!ledInit()) return;
    const bool inv = Board::profile().led.activeLow;
    const uint8_t v[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        uint32_t duty = inv ? (uint32_t)(255 - v[i]) : v[i];
        ledc_set_duty(kMode, kLedCh[i], duty);
        ledc_update_duty(kMode, kLedCh[i]);
    }
}

void ledOff() {
    if (s_ledReady) setLed(0, 0, 0);
}

bool hasLightSensor() { return Board::profile().lightSensorPin >= 0; }

int lightRaw() {
    const int pin = Board::profile().lightSensorPin;
    if (pin < 0) return -1;
    int sum = 0;
    // 0dB: o LDR da ~0..1V; a 12dB (analogRead) ficava todo no piso do ADC
    for (int i = 0; i < 8; i++) sum += analogReadAtten(pin, 0);  // ADC ruidoso: media
    return sum / 8;
}

int lightLevel() {
    const int raw = lightRaw();
    if (raw < 0) return -1;
    // LDR da CYD: a tensao SOBE no escuro. Medido na placa a 0dB: 0 com a
    // sala iluminada, ~400 com o sensor tampado. Tudo acima de kDark = 0%.
    constexpr int kDark = 400;
    int v = raw > kDark ? kDark : raw;
    return 100 - v * 100 / kDark;
}

bool hasSpeaker() { return Board::profile().speakerPin >= 0; }

bool tone(int freqHz, int ms) {
    const int pin = Board::profile().speakerPin;
    if (pin < 0 || freqHz < 20 || freqHz > 20000 || ms <= 0 || ms > 5000) return false;
    ledc_timer_config_t tim = {};
    tim.speed_mode = kMode;
    tim.timer_num = kToneTimer;
    tim.duty_resolution = LEDC_TIMER_10_BIT;
    tim.freq_hz = (uint32_t)freqHz;
    tim.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&tim) != ESP_OK) return false;
    ledc_channel_config_t ch = {};
    ch.speed_mode = kMode;
    ch.channel = kToneCh;
    ch.timer_sel = kToneTimer;
    ch.intr_type = LEDC_INTR_DISABLE;
    ch.gpio_num = pin;
    ch.duty = 512;  // onda quadrada 50%
    if (ledc_channel_config(&ch) != ESP_OK) return false;
    const uint32_t t0 = millis();
    while ((int)(millis() - t0) < ms) {
        esp_task_wdt_reset();
        delay(10);
    }
    ledc_stop(kMode, kToneCh, 0);
    return true;
}

}  // namespace BoardIO
