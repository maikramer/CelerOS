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

// =====================================================
// System nivel 3 — suporte aos apps de sistema em JS (W8)
// =====================================================

duk_ret_t JSBindings::js_setBrightness(duk_context *ctx) {
    Backlight::set(duk_require_int(ctx, 0));
    return 0;
}

duk_ret_t JSBindings::js_getBrightness(duk_context *ctx) {
    duk_push_int(ctx, Backlight::get());
    return 1;
}

duk_ret_t JSBindings::js_backlightSupported(duk_context *ctx) {
    duk_push_boolean(ctx, Backlight::isSupported() ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_openWifiSetup(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    // Empilha a tela nativa de WiFi. Como o app JS roda sincrono, a tela
    // so entra em cena quando o script devolver o controle ao loop do Kui
    // (o app deve chamar System.exitApp() logo em seguida).
    kui::Navigator::push(WifiSetupScreen::instance());
    return 0;
}

duk_ret_t JSBindings::js_setClip(duk_context *ctx) {
    // Recorte no canvas virtual: desenho fora de (x,y,w,h) e descartado
    gfx()->setClipRect(jsx(duk_require_int(ctx, 0)), jsy(duk_require_int(ctx, 1)),
                       jsx(duk_require_int(ctx, 2)), jsH(duk_require_int(ctx, 3)));
    return 0;
}

duk_ret_t JSBindings::js_clearClip(duk_context *ctx) {
    (void)ctx;
    gfx()->clearClipRect();
    return 0;
}

duk_ret_t JSBindings::js_present(duk_context *ctx) {
    // Apresentacao explicita do quadro (animacoes/loops sem delay/getTouch)
    (void)ctx;
    present();
    return 0;
}

duk_ret_t JSBindings::js_isBuffered(duk_context *ctx) {
    duk_push_boolean(ctx, s_frame != nullptr ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_exitApp(duk_context *ctx) {
    // Mesmo protocolo do canto superior direito: erro "OS_EXIT" e
    // interceptado como saida limpa pelo CelerKernel.
    duk_error(ctx, DUK_ERR_ERROR, "OS_EXIT");
    return 0;  // unreachable
}

duk_ret_t JSBindings::js_wifiStatus(duk_context *ctx) {
    duk_push_object(ctx);
    duk_push_boolean(ctx, WebManager::isActive() ? 1 : 0);
    duk_put_prop_string(ctx, -2, "connected");
    duk_push_string(ctx, WebManager::getIPAddress().c_str());
    duk_put_prop_string(ctx, -2, "ip");
    duk_push_boolean(ctx, WebManager::isServerRunning() ? 1 : 0);
    duk_put_prop_string(ctx, -2, "webServer");
    duk_push_boolean(ctx, WebManager::hasSavedNetworks() ? 1 : 0);
    duk_put_prop_string(ctx, -2, "savedNetworks");
    return 1;
}

duk_ret_t JSBindings::js_md5(duk_context *ctx) {
    // Mesmo formato do FileSystem::getFileMD5 (hex minusculo) — o PIN do
    // Settings JS tem que bater com o arquivo legado settings_pin.txt.
    const char* s = duk_require_string(ctx, 0);
    md5_context_t c;
    esp_rom_md5_init(&c);
    esp_rom_md5_update(&c, s, (uint32_t)strlen(s));
    uint8_t hash[16];
    esp_rom_md5_final(hash, &c);

    char hex[33];
    for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", hash[i]);
    hex[32] = 0;
    duk_push_string(ctx, hex);
    return 1;
}

// ---- PIN do Settings (nativo, com salt) -----------------------------------
// Substitui o fluxo JS antigo (md5 em settings_pin.txt): qualquer app podia
// ler o hash e apagar o arquivo para destravar o Settings. O estado mora no
// nativo (PinStore: settings_pin2.bin + flag NVS) e nao ha hash exposto.

duk_ret_t JSBindings::js_setPin(duk_context *ctx) {
    const char* pin = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, PinStore::set(pin) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_verifyPin(duk_context *ctx) {
    const char* pin = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, PinStore::verify(pin) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_pinClear(duk_context *ctx) {
    PinStore::clear();
    return 0;
}

duk_ret_t JSBindings::js_pinState(duk_context *ctx) {
    // 0 = sem PIN, 1 = ativo, 2 = corrompido (flag NVS sem arquivo)
    duk_push_int(ctx, PinStore::state());
    return 1;
}

// ---- senha do Web Server ---------------------------------------------------

duk_ret_t JSBindings::js_webAuthInfo(duk_context *ctx) {
    duk_push_object(ctx);
    duk_push_string(ctx, "admin");
    duk_put_prop_string(ctx, -2, "user");
    duk_push_string(ctx, WebAuth::password());
    duk_put_prop_string(ctx, -2, "pass");
    return 1;
}

duk_ret_t JSBindings::js_webAuthSetPass(duk_context *ctx) {
    const char* p = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, WebAuth::setPassword(p) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_rescanApps(duk_context *ctx) {
    LauncherUI::requestRescan();
    return 0;
}

duk_ret_t JSBindings::js_factoryReset(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    const char* mode = duk_is_string(ctx, 0) ? duk_require_string(ctx, 0) : "configs";

    if (strcmp(mode, "total") == 0) {
        // LittleFS inteiro (apps + icones + configs). Recuperacao exige
        // reflashe de data/ (tools/flash_data.sh) ou celerctl apps install.
        WebManager::forgetAllNetworks();
        FileSystem::formatLittleFS();
        PinStore::clear();     // flag NVS nao vive no LittleFS
        WebAuth::regenerate();
        duk_push_boolean(ctx, 1);
        return 1;
    }

    // "configs": zera configuracoes e credenciais, preserva apps/icones
    const char* cfgFiles[] = {
        "/local/brightness.txt", "/local/settings_pin.txt", "/local/nowifi.txt",
        "/local/web_on.txt", "/local/config_install_sd.txt", "/local/ota_url.txt",
        "/local/config_time.txt", "/local/touch_cal_p.bin", "/local/wifi.txt",
        "/local/settings_pin2.bin", "/local/ota_allow_http.txt",
    };
    for (const char* f : cfgFiles) FileSystem::deleteFile(f);
    PinStore::clear();        // limpa tambem a flag NVS do PIN
    WebAuth::regenerate();    // senha web nova (a antiga era "config")
    WebManager::forgetAllNetworks();
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_otaCheck(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    OtaManager::checkForUpdates();
    const OtaUpdateInfo& info = OtaManager::info;
    duk_push_object(ctx);
    duk_push_boolean(ctx, info.fetchFailed ? 1 : 0);
    duk_put_prop_string(ctx, -2, "fetchFailed");
    duk_push_boolean(ctx, info.available ? 1 : 0);
    duk_put_prop_string(ctx, -2, "available");
    duk_push_boolean(ctx, info.hasFirmware ? 1 : 0);
    duk_put_prop_string(ctx, -2, "hasFirmware");
    duk_push_string(ctx, info.version.c_str());
    duk_put_prop_string(ctx, -2, "version");
    duk_push_string(ctx, info.firmwareUrl.c_str());
    duk_put_prop_string(ctx, -2, "url");
    duk_push_string(ctx, info.changelog.c_str());
    duk_put_prop_string(ctx, -2, "changelog");
    duk_push_string(ctx, info.guide.c_str());
    duk_put_prop_string(ctx, -2, "guide");
    duk_push_string(ctx, info.type.c_str());
    duk_put_prop_string(ctx, -2, "type");
    return 1;
}

// performUpdate roda na mesma task do script e invoca onProgress de dentro
// do loop de flash: reentrar no Duktape pelo stash e seguro.
static duk_context* s_otaCtx = nullptr;
static void otaProgressTrampoline(int percent) {
    if (!s_otaCtx) return;
    duk_push_global_stash(s_otaCtx);
    duk_get_prop_string(s_otaCtx, -1, "_otaCb");
    if (duk_is_function(s_otaCtx, -1)) {
        duk_push_int(s_otaCtx, percent);
        duk_pcall(s_otaCtx, 1);
    }
    duk_pop_2(s_otaCtx);
    JSBindings::present();
}

duk_ret_t JSBindings::js_otaStart(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    const char* url = duk_require_string(ctx, 0);
    bool hasCb = duk_is_function(ctx, 1) ? true : false;

    if (hasCb) {
        duk_push_global_stash(ctx);
        duk_dup(ctx, 1);
        duk_put_prop_string(ctx, -2, "_otaCb");
        duk_pop(ctx);
        s_otaCtx = ctx;
    }

    bool ok = OtaManager::performUpdate(url, hasCb ? otaProgressTrampoline : nullptr);

    if (hasCb) {
        duk_push_global_stash(ctx);
        duk_push_undefined(ctx);
        duk_put_prop_string(ctx, -2, "_otaCb");
        duk_pop(ctx);
        s_otaCtx = nullptr;
    }

    duk_push_object(ctx);
    duk_push_boolean(ctx, ok ? 1 : 0);
    duk_put_prop_string(ctx, -2, "ok");
    if (!ok) {
        duk_push_string(ctx, OtaManager::lastError.c_str());
        duk_put_prop_string(ctx, -2, "error");
    }
    return 1;
}

duk_ret_t JSBindings::js_setTimezone(duk_context *ctx) {
    TimeManager::setTimezone(duk_require_string(ctx, 0));
    return 0;
}

duk_ret_t JSBindings::js_setManualTime(duk_context *ctx) {
    TimeManager::setManualTime(duk_require_int(ctx, 0), duk_require_int(ctx, 1),
                               duk_require_int(ctx, 2), duk_require_int(ctx, 3),
                               duk_require_int(ctx, 4));
    return 0;
}

duk_ret_t JSBindings::js_set24hFormat(duk_context *ctx) {
    TimeManager::setTimeFormat(duk_require_boolean(ctx, 0) ? true : false);
    return 0;
}

duk_ret_t JSBindings::js_get24hFormat(duk_context *ctx) {
    duk_push_boolean(ctx, TimeManager::use24hFormat ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_setNtpEnabled(duk_context *ctx) {
    TimeManager::setNTPEnabled(duk_require_boolean(ctx, 0) ? true : false);
    return 0;
}

duk_ret_t JSBindings::js_getNtpEnabled(duk_context *ctx) {
    duk_push_boolean(ctx, TimeManager::ntpEnabled ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_webActive(duk_context *ctx) {
    duk_push_boolean(ctx, WebManager::isServerRunning() ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_webSetActive(duk_context *ctx) {
    // Toggle do servidor web (persiste em web_on.txt e age ao vivo), igual a
    // tela C++ original — mas sem reboot. NAO desliga o WiFi (isso e o
    // nowifi.txt / WebManager::disable).
    if (duk_require_boolean(ctx, 0)) {
        FileSystem::writeTextFile("/local/web_on.txt", "1");
        if (WebManager::isActive()) WebManager::startWebServerIfNeeded();
    } else {
        FileSystem::deleteFile("/local/web_on.txt");
        WebManager::stopWebServer();
    }
    return 0;
}

// --- Net nivel 3 (WiFi) ---

duk_ret_t JSBindings::js_wifiScan(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    // Scan bloqueante (~2s) — mesmo comportamento da tela nativa.
    CelerScanEntry entries[20];
    int n = WebManager::scanNetworks(entries, 20);

    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        duk_push_object(ctx);
        duk_push_string(ctx, entries[i].ssid.c_str());
        duk_put_prop_string(ctx, -2, "ssid");
        duk_push_int(ctx, entries[i].rssi);
        duk_put_prop_string(ctx, -2, "rssi");
        duk_push_boolean(ctx, entries[i].secure ? 1 : 0);
        duk_put_prop_string(ctx, -2, "secure");
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

duk_ret_t JSBindings::js_wifiConnect(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    const char* ssid = duk_require_string(ctx, 0);
    const char* pass = duk_is_string(ctx, 1) ? duk_require_string(ctx, 1) : "";
    duk_push_boolean(ctx, WebManager::connect(ssid, pass) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_wifiDisconnect(duk_context *ctx) {
    WebManager::disconnect();
    return 0;
}

