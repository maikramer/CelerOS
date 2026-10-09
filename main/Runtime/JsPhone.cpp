// Phone.* (API 15, CONFIG_CELEROS_PHONE_LINK) — o celular pareado pelo
// Gadgetbridge (Bluetooth/PhoneLink). Sem o Kconfig o objeto nao existe:
// apps testam typeof Phone. Leituras e comandos de musica/encontrar sao
// abertos; ligar/desligar o link exige "system" (Settings/ajustes).

#include "JSBindings.h"
#include "JsInternal.h"
#include "../Bluetooth/PhoneLink.h"
#include "../Utils/AppPerms.h"
#include <string.h>
#include <time.h>

// Phone.status() -> {enabled, connected, passkey, name}
duk_ret_t JSBindings::js_phoneStatus(duk_context* ctx) {
    duk_push_object(ctx);
    putBool(ctx, "enabled", PhoneLink::enabled());
    putBool(ctx, "connected", PhoneLink::connected());
    putInt(ctx, "passkey", (duk_int_t)PhoneLink::passkey());
    putStr(ctx, "name", PhoneLink::advName());
    return 1;
}

// Phone.setEnabled(bool) — "system" (lint-perm: system)
duk_ret_t JSBindings::js_phoneSetEnabled(duk_context* ctx) {
    if (!perm(celer::PERM_SYSTEM)) {
        duk_error(ctx, DUK_ERR_ERROR, "Phone.setEnabled requer permissao \"system\"");
    }
    PhoneLink::setEnabled(duk_to_boolean(ctx, 0));
    return 0;
}

// Phone.music(cmd) -> bool. cmd: play|pause|playpause|next|previous|volumeup|volumedown
duk_ret_t JSBindings::js_phoneMusic(duk_context* ctx) {
    static const char* const kOk[] = {"play", "pause", "playpause", "next",
                                      "previous", "volumeup", "volumedown"};
    const char* cmd = duk_require_string(ctx, 0);
    bool valid = false;
    for (const char* k : kOk) valid = valid || strcmp(k, cmd) == 0;
    duk_push_boolean(ctx, valid && PhoneLink::sendMusic(cmd));
    return 1;
}

// Phone.musicInfo() -> {artist, track, album, state} | null
duk_ret_t JSBindings::js_phoneMusicInfo(duk_context* ctx) {
    PhoneLink::Music m;
    if (!PhoneLink::music(m)) {
        duk_push_null(ctx);
        return 1;
    }
    duk_push_object(ctx);
    putStr(ctx, "artist", m.artist.c_str());
    putStr(ctx, "track", m.track.c_str());
    putStr(ctx, "album", m.album.c_str());
    putStr(ctx, "state", m.state.c_str());
    return 1;
}

// Phone.weather() -> {temp (°C), hum, txt, loc, age (s)} | null
duk_ret_t JSBindings::js_phoneWeather(duk_context* ctx) {
    PhoneLink::Weather w;
    if (!PhoneLink::weather(w)) {
        duk_push_null(ctx);
        return 1;
    }
    time_t now;
    time(&now);
    duk_push_object(ctx);
    putNum(ctx, "temp", w.tempC);
    putInt(ctx, "hum", w.hum);
    putStr(ctx, "txt", w.txt.c_str());
    putStr(ctx, "loc", w.loc.c_str());
    putNum(ctx, "age", (double)(now > w.at ? now - w.at : 0));
    return 1;
}

// Phone.find(bool) -> bool: faz o celular tocar ("encontrar celular")
duk_ret_t JSBindings::js_phoneFind(duk_context* ctx) {
    duk_push_boolean(ctx, PhoneLink::findPhone(duk_to_boolean(ctx, 0)));
    return 1;
}

// Phone.forget() — "system" (lint-perm: system): apaga o pareamento
duk_ret_t JSBindings::js_phoneForget(duk_context* ctx) {
    if (!perm(celer::PERM_SYSTEM)) {
        duk_error(ctx, DUK_ERR_ERROR, "Phone.forget requer permissao \"system\"");
    }
    PhoneLink::forget();
    return 0;
}
