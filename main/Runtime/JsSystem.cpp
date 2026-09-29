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
#include "../Kernel/Core/CelerKernel.h"
#include "../Launcher/LauncherUI.h"
#include "../Launcher/Screens.h"
#include <lgfx/v1/misc/DataWrapper.hpp>
#include "JsInternal.h"
#include "../Utils/CelerSettings.h"
#include "../UI/Kui.h"
#include "../Boards/Board.h"
#include "driver/ledc.h"
#include "esp_task_wdt.h"

// =====================================================
// Touch Input
// =====================================================

// Returns an object { x, y, touched } 
duk_ret_t JSBindings::js_getTouch(duk_context *ctx) {
    uint16_t tx = 0, ty = 0;
    bool touched = false;
    present();  // app cedeu: o frame desenhado ate aqui vai ao vidro
    if (tftInstance) {
        touched = kui::readTouch(&tx, &ty);

        // Topbar (X de sair): dispara so no release; toque na faixa e chrome
        if (pollAppChrome(touched, tx, ty)) {
            throwAppExit(ctx);  // nao retorna // Unreachable, but good practice
        }
    }

    // Coordenadas no espaco de projeto 240x320 (hit-zones dos apps batem);
    // vertical desconta a topbar do sistema
    int jx = tftInstance ? ((int)tx * 240 / tftInstance->width()) : 0;
    int jy = 0;
    if (tftInstance) {
        // fixo: vertical desconta a topbar; retratil: tela cheia 1:1
        int offY = s_topbarFixed ? UI::topbarH() : 0;
        jy = ((int)ty - offY) * 320 / ((int)tftInstance->height() - offY);
    }
    duk_push_object(ctx);
    duk_push_int(ctx, touched ? jx : 0);
    duk_put_prop_string(ctx, -2, "x");
    duk_push_int(ctx, touched ? jy : 0);
    duk_put_prop_string(ctx, -2, "y");
    duk_push_boolean(ctx, touched ? 1 : 0);
    duk_put_prop_string(ctx, -2, "touched");
    return 1;
}

// =====================================================
// System Utilities
// =====================================================

duk_ret_t JSBindings::js_millis(duk_context *ctx) {
    duk_push_uint(ctx, millis());
    return 1;
}

duk_ret_t JSBindings::js_micros(duk_context *ctx) {
    duk_push_uint(ctx, micros());
    return 1;
}

duk_ret_t JSBindings::js_delay(duk_context *ctx) {
    int ms = duk_require_int(ctx, 0);
    present();
    uint32_t t0 = millis();
    // Mark-and-sweep completo (so ciclos; o resto e refcount) custa ms em
    // heaps grandes: antes rodava a CADA delay (loops de 20 ms gastavam boa
    // parte do tempo aqui). Agora no maximo 1x/s, ou ja se o heap aperta.
    static uint32_t lastGcMs = 0;
    if (t0 - lastGcMs > 1000 || ESP.getMaxAllocHeap() < 24 * 1024) {
        duk_gc(ctx, 0);
        lastGcMs = t0;
    }
    if (ms > 0 && ms < 30000) { // Safety cap at 30 seconds
        // fatias de 4s com reset do watchdog (F2): um System.delay(30000)
        // nao pode derrubar o WDT de 15s da main task (app rodando = sem
        // celerLoop; os pontos de espera alimentam o WDT)
        uint32_t remain = (uint32_t)ms - (millis() - t0);
        while (remain > 0) {
            esp_task_wdt_reset();
            uint32_t slice = remain > 4000 ? 4000 : remain;
            delay(slice);
            remain -= slice;
        }
    }
    return 0;
}

duk_ret_t JSBindings::js_delayMicroseconds(duk_context *ctx) {
    int us = duk_require_int(ctx, 0);
    if (us > 0) {
        delayMicroseconds(us);
    }
    return 0;
}

duk_ret_t JSBindings::js_print(duk_context *ctx) {
    const char *msg = duk_require_string(ctx, 0);
    celer_log_println(msg);
    return 0;
}

duk_ret_t JSBindings::js_getTemperature(duk_context *ctx) {
    float temp = temperatureRead();
    duk_push_number(ctx, temp);
    return 1;
}

duk_ret_t JSBindings::js_hasTemperatureSensor(duk_context *ctx) {
    float temp = temperatureRead();
    // 53.33 is a common return value when the sensor is unsupported or disconnected internally
    bool hasSensor = (temp != 53.33f);
    duk_push_boolean(ctx, hasSensor);
    return 1;
}

duk_ret_t JSBindings::js_getInfo(duk_context *ctx) {
    SystemInfo& sys = SystemInfo::instance();
    MemoryInfo mem = sys.getMemoryInfo();
    ChipInfo chip = sys.getChipInfo();

    duk_push_object(ctx);

    // RAM
    duk_push_uint(ctx, mem.totalHeap);
    duk_put_prop_string(ctx, -2, "totalRAM");

    duk_push_uint(ctx, mem.freeHeap);
    duk_put_prop_string(ctx, -2, "freeRAM");

    duk_push_uint(ctx, mem.minFreeHeap);
    duk_put_prop_string(ctx, -2, "minFreeRAM");

    duk_push_uint(ctx, mem.largestFreeBlock);
    duk_put_prop_string(ctx, -2, "maxAllocRAM");

    duk_push_uint(ctx, mem.totalPsram);
    duk_put_prop_string(ctx, -2, "totalPSRAM");

    duk_push_uint(ctx, mem.freePsram);
    duk_put_prop_string(ctx, -2, "freePSRAM");

    // Heap livre no INICIO deste app (antes do fonte/heap JS): quanto um app
    // pode ocupar nesta placa. freeRAM e medido agora, com o app atual ja
    // carregado — a loja usa appRAM para o teto de tamanho sem PSRAM.
    duk_push_uint(ctx, (duk_uint_t)CelerKernel::appLaunchFreeHeap);
    duk_put_prop_string(ctx, -2, "appRAM");

    // Chip & CPU (frequencia vem do Compat — SystemInfo nao expoe)
    duk_push_uint(ctx, ESP.getCpuFreqMHz());
    duk_put_prop_string(ctx, -2, "cpuFreqMHz");

    duk_push_string(ctx, sys.getChipModel().c_str());
    duk_put_prop_string(ctx, -2, "chipModel");

    duk_push_uint(ctx, chip.cores);
    duk_put_prop_string(ctx, -2, "chipCores");

    duk_push_uint(ctx, chip.revision);
    duk_put_prop_string(ctx, -2, "chipRevision");

    duk_push_uint(ctx, sys.getFlashSize());
    duk_put_prop_string(ctx, -2, "flashSize");

    // Uptime e identidade
    duk_push_uint(ctx, (uint32_t)sys.getUptimeMillis());
    duk_put_prop_string(ctx, -2, "uptimeMs");

    duk_push_string(ctx, sys.getMacAddress().c_str());
    duk_put_prop_string(ctx, -2, "macAddress");

    duk_push_string(ctx, sys.getResetReasonString().c_str());
    duk_put_prop_string(ctx, -2, "resetReason");

    duk_push_string(ctx, sys.getIdfVersion().c_str());
    duk_put_prop_string(ctx, -2, "idfVersion");

    return 1;
}

duk_ret_t JSBindings::js_restart(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    ESP.restart();
    return 0;
}

duk_ret_t JSBindings::js_getTime(duk_context *ctx) {
    duk_push_string(ctx, TimeManager::getFormattedTime().c_str());
    return 1;
}

duk_ret_t JSBindings::js_getSeconds(duk_context *ctx) {
    duk_push_int(ctx, TimeManager::getSeconds());
    return 1;
}

duk_ret_t JSBindings::js_getDate(duk_context *ctx) {
    duk_push_string(ctx, TimeManager::getFormattedDate().c_str());
    return 1;
}

duk_ret_t JSBindings::js_getYear(duk_context *ctx) {
    duk_push_int(ctx, TimeManager::getYear());
    return 1;
}

duk_ret_t JSBindings::js_getMonth(duk_context *ctx) {
    duk_push_int(ctx, TimeManager::getMonth());
    return 1;
}

duk_ret_t JSBindings::js_getDay(duk_context *ctx) {
    duk_push_int(ctx, TimeManager::getDay());
    return 1;
}

duk_ret_t JSBindings::js_getTimezone(duk_context *ctx) {
    duk_push_string(ctx, TimeManager::currentTimezone.c_str());
    return 1;
}

duk_ret_t JSBindings::js_getOSVersion(duk_context *ctx) {
    duk_push_string(ctx, CELEROS_VERSION);
    return 1;
}

duk_ret_t JSBindings::js_getAPILevel(duk_context *ctx) {
    duk_push_int(ctx, CELEROS_API_LEVEL);
    return 1;
}

// UI Bindings=====================================================
// Network Bindings
// =====================================================

duk_ret_t JSBindings::js_getIPAddress(duk_context *ctx) {
    duk_push_string(ctx, WebManager::getIPAddress().c_str());
    return 1;
}

duk_ret_t JSBindings::js_isWiFiActive(duk_context *ctx) {
    duk_push_boolean(ctx, WebManager::isActive());
    return 1;
}


// ---- Config em NVS (F3) / notificacao e buzzer (F4) ------------------------

duk_ret_t JSBindings::js_setting(duk_context *ctx) {
    // System.setting(k) -> "1" | null; System.setting(k, v) -> bool
    const char* key = duk_require_string(ctx, 0);
    if (duk_is_string(ctx, 1)) {
        duk_push_boolean(ctx, CelerSettings::set(key, duk_get_string(ctx, 1)) ? 1 : 0);
        return 1;
    }
    std::string v = CelerSettings::get(key);
    if (v.empty()) { duk_push_null(ctx); return 1; }
    duk_push_string(ctx, v.c_str());
    return 1;
}

duk_ret_t JSBindings::js_toast(duk_context *ctx) {
    const char* msg = duk_require_string(ctx, 0);
    kui::Navigator::toast(msg);
    return 0;
}

duk_ret_t JSBindings::js_beep(duk_context *ctx) {
    int freq = duk_require_int(ctx, 0);
    int ms = duk_require_int(ctx, 1);
    if (freq < 20 || freq > 20000 || ms <= 0 || ms > 5000) {
        duk_push_boolean(ctx, 0);
        return 1;
    }
    int pin = Board::profile().speakerPin;
    if (pin < 0) { duk_push_boolean(ctx, 0); return 1; }
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.freq_hz = (uint32_t)freq;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);
    ledc_channel_config_t ch = {};
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = LEDC_CHANNEL_0;
    ch.timer_sel = LEDC_TIMER_0;
    ch.intr_type = LEDC_INTR_DISABLE;
    ch.gpio_num = (gpio_num_t)pin;
    ch.duty = 512;  // 50%
    if (ledc_channel_config(&ch) != ESP_OK) { duk_push_boolean(ctx, 0); return 1; }
    uint32_t t0 = millis();
    while ((int)(millis() - t0) < ms) { esp_task_wdt_reset(); delay(10); }
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    duk_push_boolean(ctx, 1);
    return 1;
}
