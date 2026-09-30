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
#include "../Hardware/BoardIO.h"

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
// Alocacao de canal/timer no BoardIO (mapa LEDC unico do firmware).

// System.gpio.servo(pin, angulo) -> bool. Angulo 0..180 vira pulso de
// 500..2500 us (convencao 9g/SG90); aceita fracao (rampas suaves).
duk_ret_t JSBindings::js_servo(duk_context *ctx) {
    int pin = duk_require_int(ctx, 0);
    double angle = duk_require_number(ctx, 1);
    if (!(angle >= 0)) angle = 0;  // NaN tambem
    if (angle > 180) angle = 180;
    duk_push_boolean(ctx, BoardIO::servoWrite(pin, (int)(500 + angle * 2000.0 / 180.0 + 0.5)) ? 1 : 0);
    return 1;
}

// System.gpio.servoOff(pin) -> bool. Para o PWM e libera o canal (o servo
// fica solto, sem forca).
duk_ret_t JSBindings::js_servoOff(duk_context *ctx) {
    duk_push_boolean(ctx, BoardIO::servoOff(duk_require_int(ctx, 0)) ? 1 : 0);
    return 1;
}
