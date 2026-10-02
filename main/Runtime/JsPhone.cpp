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
    duk_push_boolean(ctx, PhoneLink::enabled());
    duk_put_prop_string(ctx, -2, "enabled");
    duk_push_boolean(ctx, PhoneLink::connected());
    duk_put_prop_string(ctx, -2, "connected");
    duk_push_int(ctx, (duk_int_t)PhoneLink::passkey());
    duk_put_prop_string(ctx, -2, "passkey");
    duk_push_string(ctx, PhoneLink::advName());
    duk_put_prop_string(ctx, -2, "name");
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
    duk_push_string(ctx, m.artist.c_str());
    duk_put_prop_string(ctx, -2, "artist");
    duk_push_string(ctx, m.track.c_str());
    duk_put_prop_string(ctx, -2, "track");
    duk_push_string(ctx, m.album.c_str());
    duk_put_prop_string(ctx, -2, "album");
    duk_push_string(ctx, m.state.c_str());
    duk_put_prop_string(ctx, -2, "state");
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
    duk_push_number(ctx, w.tempC);
    duk_put_prop_string(ctx, -2, "temp");
    duk_push_int(ctx, w.hum);
    duk_put_prop_string(ctx, -2, "hum");
    duk_push_string(ctx, w.txt.c_str());
    duk_put_prop_string(ctx, -2, "txt");
    duk_push_string(ctx, w.loc.c_str());
    duk_put_prop_string(ctx, -2, "loc");
    duk_push_number(ctx, (double)(now > w.at ? now - w.at : 0));
    duk_put_prop_string(ctx, -2, "age");
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
