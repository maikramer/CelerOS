#include "JSBindings.h"
#include "../USBDevice/LogSink.h"
#include "../Display/Layout.h"
#include "../FileSystem/FileSystem.h"
#include "../UI/Keyboard.h"
#include "../WebManager/WebManager.h"
#include "../WebManager/WebAuth.h"
#include "../Kernel/TimeManager.h"
#include "../Utils/StrUtils.h"
#include "../Utils/PinStore.h"
#include "HttpClient.h"
#include "SystemInfo.h"
#include "esp_rom_md5.h"
#include "../Display/Backlight.h"
#include "../Display/Theme.h"
#include "../Display/Icon.h"
#include "../OTA/OtaManager.h"
#include "../Launcher/LauncherUI.h"
#include "../Launcher/Screens.h"
#include <lgfx/v1/misc/DataWrapper.hpp>
#include "JsInternal.h"
#include "driver/ledc.h"

// =====================================================
// GPIO Bindings
// =====================================================

duk_ret_t JSBindings::js_pinMode(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int mode = duk_require_int(ctx, 1);
    pinMode(pin, mode);
    return 0;
}

duk_ret_t JSBindings::js_digitalWrite(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int val = duk_require_int(ctx, 1);
    digitalWrite(pin, val);
    return 0;
}

duk_ret_t JSBindings::js_digitalRead(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int val = digitalRead(pin);
    duk_push_int(ctx, val);
    return 1;
}

duk_ret_t JSBindings::js_analogRead(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int val = analogRead(pin);
    duk_push_int(ctx, val);
    return 1;
}

duk_ret_t JSBindings::js_analogWrite(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int val = duk_require_int(ctx, 1);
    analogWrite(pin, val);
    return 0;
}

duk_ret_t JSBindings::js_pulseIn(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int state = duk_require_int(ctx, 1);
    unsigned long timeout = 1000000L; // default 1 second timeout
    if (duk_get_top(ctx) >= 3) {
        timeout = duk_require_uint(ctx, 2);
    }

    unsigned long duration = pulseIn(pin, state, timeout);
    duk_push_uint(ctx, duration);
    return 1;
}

// ---- servos (API 10): PWM 50 Hz por LEDC ----
//
// Canais 0/1/2/7 do grupo low-speed (o BoardIO ocupa 3-5 com o LED RGB e 6
// com o tone); timer 0 dedicado a 50 Hz, resolucao 14 bits (periodo de
// 20000 us). Ate 4 servos simultaneos — as 4 perninhas do cachorro.

namespace {
constexpr int K_SERVO_MAX = 4;
constexpr ledc_channel_t kServoCh[K_SERVO_MAX] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_7};
constexpr ledc_timer_t kServoTimer = LEDC_TIMER_0;
int8_t s_servoPin[K_SERVO_MAX] = {-1, -1, -1, -1};  // -1 = canal livre

bool servoWriteUs(int pin, int us) {
    int slot = -1;
    for (int i = 0; i < K_SERVO_MAX; i++) {
        if (s_servoPin[i] == pin) { slot = i; break; }  // ja esta neste pino
        if (slot < 0 && s_servoPin[i] < 0) slot = i;    // primeiro livre
    }
    if (slot < 0) return false;  // 4 servos ja em uso: chame System.servoOff
    if (s_servoPin[slot] != pin) {
        ledc_timer_config_t tim = {};
        tim.speed_mode = LEDC_LOW_SPEED_MODE;
        tim.timer_num = kServoTimer;
        tim.duty_resolution = LEDC_TIMER_14_BIT;
        tim.freq_hz = 50;
        tim.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&tim) != ESP_OK) return false;  // idempotente
        ledc_channel_config_t ch = {};
        ch.speed_mode = LEDC_LOW_SPEED_MODE;
        ch.channel = kServoCh[slot];
        ch.timer_sel = kServoTimer;
        ch.intr_type = LEDC_INTR_DISABLE;
        ch.gpio_num = pin;
        ch.duty = 0;
        if (ledc_channel_config(&ch) != ESP_OK) return false;
        s_servoPin[slot] = (int8_t)pin;
    }
    uint32_t duty = (uint32_t)(((int64_t)us * 16383) / 20000);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kServoCh[slot], duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kServoCh[slot]);
    return true;
}
}  // namespace

// System.servo(pin, angulo) -> bool. Angulo 0..180 vira pulso de 500..2500
// us (convencao 9g/SG90); o canal e alocado na primeira escrita do pino.
duk_ret_t JSBindings::js_servo(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    int angle = duk_require_int(ctx, 1);
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    duk_push_boolean(ctx, servoWriteUs(pin, 500 + angle * 2000 / 180) ? 1 : 0);
    return 1;
}

// System.servoOff(pin) -> bool. Para o PWM e libera o canal (o servo fica
// solto, sem forca).
duk_ret_t JSBindings::js_servoOff(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    for (int i = 0; i < K_SERVO_MAX; i++) {
        if (s_servoPin[i] == pin) {
            ledc_stop(LEDC_LOW_SPEED_MODE, kServoCh[i], 0);
            s_servoPin[i] = -1;
            duk_push_boolean(ctx, 1);
            return 1;
        }
    }
    duk_push_boolean(ctx, 0);
    return 1;
}


