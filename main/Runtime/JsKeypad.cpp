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
#include <cstring>
#include "JsInternal.h"

// =====================================================
// Keyboard Bindings
// =====================================================

// API 11: {hint:"num"} — teclado numerico (pagina Num do KeyboardScreen).
// Valor desconhecido devolve 0 (QWERTY normal): firmware antigo que ignora
// a opcao degrada igual.
static char parseHint(duk_context *ctx, duk_idx_t idx) {
    if (!duk_is_object(ctx, idx)) return 0;
    duk_get_prop_string(ctx, idx, "hint");
    char h = 0;
    if (duk_is_string(ctx, -1)) {
        const char *s = duk_get_string(ctx, -1);
        if (s != nullptr && strcmp(s, "num") == 0) h = 'n';
    }
    duk_pop(ctx);
    return h;
}

duk_ret_t JSBindings::js_prompt(duk_context *ctx) {
    const char *promptMsg = "";
    if (duk_is_string(ctx, 0)) promptMsg = duk_require_string(ctx, 0);

    const char *initialText = "";
    if (duk_is_string(ctx, 1)) initialText = duk_require_string(ctx, 1);

    // API level 7: 3o argumento opcional {mask} — senha/PIN em bullets
    bool mask = false;
    if (duk_is_object(ctx, 2)) {
        if (duk_get_prop_string(ctx, 2, "mask") && duk_is_boolean(ctx, -1)) {
            mask = duk_get_boolean(ctx, -1) != 0;
        }
        duk_pop(ctx);
    }
    char hint = parseHint(ctx, 2);  // API 11: {hint:"num"}

    present();
    std::string result = kui::getString(initialText, promptMsg, 64, mask, hint);
    // o teclado desenhou direto no display: o proximo present repoe o app
    s_frameDirty = true;

    duk_push_string(ctx, result.c_str());

    return 1;
}

// =====================================================
// Keyboard acoplado (API level 5) — sessao NAO-bloqueante
//
// O app abre o teclado (keypadOpen), bombeia com keypadPoll no proprio loop
// e desenha em volta: o teclado vai no MESMO alvo do app (gfx(): sprite do
// app > quadro PSRAM > display) e sobrevive ao present(). Eventos chegam um
// por poll: change (buffer mudou), enter (OK: texto em ev.text) e cancel
// (X — encerra a sessao sozinho). O canto de saida do OS segue valendo.
// =====================================================

static kui::KeyboardScreen *s_kb = nullptr;
static kui::TouchPump s_kbPump;
static uint32_t s_kbLastPollMs = 0;
enum KbEvent { KB_EV_NONE = 0, KB_EV_CHANGE, KB_EV_ENTER, KB_EV_CANCEL };
static int s_kbEvent = KB_EV_NONE;
static std::string s_kbEnterText;

void keypadCloseSession() {
    if (s_kb) {
        delete s_kb;
        s_kb = nullptr;
    }
    s_kbEvent = KB_EV_NONE;
    s_kbEnterText.clear();
    s_frameDirty = true;  // proximo present restaura o frame do app
}

duk_ret_t JSBindings::js_keypadOpen(duk_context *ctx) {
    if (!tftInstance) {
        duk_push_false(ctx);
        return 1;
    }
    if (s_kb) {  // so uma sessao por vez
        duk_push_false(ctx);
        return 1;
    }

    std::string title, initial;
    int maxLen = 64;
    bool field = true;
    bool mask = false;
    char hint = 0;
    if (duk_is_object(ctx, 0)) {
        if (duk_get_prop_string(ctx, 0, "title") && duk_is_string(ctx, -1)) title = duk_get_string(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "initial") && duk_is_string(ctx, -1)) initial = duk_get_string(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "maxLen") && duk_is_number(ctx, -1)) maxLen = duk_get_int(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "field") && duk_is_boolean(ctx, -1)) field = duk_get_boolean(ctx, -1) != 0;
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "mask") && duk_is_boolean(ctx, -1)) mask = duk_get_boolean(ctx, -1) != 0;
        duk_pop(ctx);
        hint = parseHint(ctx, 0);  // API 11
    }
    if (maxLen < 1) maxLen = 1;

    present();
    s_kb = new kui::KeyboardScreen(title, initial, maxLen);
    s_kb->setPersistent(true);
    s_kb->setShowField(field);
    s_kb->setMask(mask);
    s_kb->setHint(hint);
    s_kb->onChange = [] { s_kbEvent = KB_EV_CHANGE; };
    s_kb->onEnter = [](const std::string& t) {
        s_kbEnterText = t;
        s_kbEvent = KB_EV_ENTER;
    };
    s_kb->onResult = [](const std::string&, bool ok) {
        if (!ok) s_kbEvent = KB_EV_CANCEL;  // X (so existe com field)
    };
    s_kbEvent = KB_EV_NONE;
    s_kbEnterText.clear();
    s_kbLastPollMs = millis();

    {
        GfxStateGuard guard(*JSBindings::gfx());  // cor/datum do app intactos
        kui::Canvas c(*tftInstance, JSBindings::gfx());
        s_kb->draw(c);
    }
    duk_push_boolean(ctx, true);
    return 1;
}

duk_ret_t JSBindings::js_keypadPoll(duk_context *ctx) {
    checkRemoteAppExit(ctx);  // shell "exit": encerra mesmo sem teclado aberto
    if (!s_kb || !tftInstance) {
        duk_push_null(ctx);
        return 1;
    }
    present();

    uint16_t tx = 0, ty = 0;
    // pollAppChrome SEMPRE roda: o disparo da saida e no release (readTouch
    // false) — dentro de um if(readTouch) o release nunca seria visto e o X
    // acendia sem sair (bug do Terminal)
    bool touched = kui::readTouch(&tx, &ty);
    if (pollAppChrome(touched, tx, ty)) {
        keypadCloseSession();
        throwAppExit(ctx);  // nao retorna
    }

    s_kbEvent = KB_EV_NONE;
    s_kbPump.poll([](const kui::TouchEvent& ev) {
        s_kb->onTouch(ev);
        if (ev.type != kui::TouchEvent::Drag) s_kb->markDirty();  // tecla "afunda"
    });
    uint32_t now = millis();
    uint32_t dt = now - s_kbLastPollMs;
    if (dt > 100) dt = 100;
    s_kbLastPollMs = now;
    s_kb->onTick(dt);
    if (s_kb->consumeDirty()) {
        GfxStateGuard guard(*JSBindings::gfx());  // cor/datum do app intactos
        kui::Canvas c(*tftInstance, JSBindings::gfx());
        s_kb->draw(c);
    }

    if (s_kbEvent == KB_EV_NONE) {
        duk_push_null(ctx);
        return 1;
    }

    duk_push_object(ctx);
    if (s_kbEvent == KB_EV_ENTER) {
        duk_push_string(ctx, "enter");
        duk_put_prop_string(ctx, -2, "type");
        duk_push_string(ctx, s_kbEnterText.c_str());
        duk_put_prop_string(ctx, -2, "text");
        s_kbEnterText.clear();
    } else if (s_kbEvent == KB_EV_CHANGE) {
        duk_push_string(ctx, "change");
        duk_put_prop_string(ctx, -2, "type");
    } else {  // cancel: o X encerrou a sessao
        duk_push_string(ctx, "cancel");
        duk_put_prop_string(ctx, -2, "type");
        keypadCloseSession();
    }
    return 1;
}

duk_ret_t JSBindings::js_keypadText(duk_context *ctx) {
    duk_push_string(ctx, s_kb ? s_kb->text().c_str() : "");
    return 1;
}

duk_ret_t JSBindings::js_keypadRect(duk_context *ctx) {
    // Area das teclas no espaco virtual 240x320; fechado: faixa nula no rodape.
    // O espaco do app comeca abaixo da topbar do sistema.
    int topV = 320;
    if (s_kb && tftInstance) {
        int offY = s_topbarFixed ? UI::topbarH() : 0;
        int appH = tftInstance->height() - offY;
        if (appH < 1) appH = 1;
        topV = (int)(((long)s_kb->keysTop() - offY) * 320 / appH);
        if (topV < 0) topV = 0;
        if (topV > 320) topV = 320;
    }
    duk_push_object(ctx);
    duk_push_int(ctx, 0);
    duk_put_prop_string(ctx, -2, "x");
    duk_push_int(ctx, topV);
    duk_put_prop_string(ctx, -2, "y");
    duk_push_int(ctx, 240);
    duk_put_prop_string(ctx, -2, "w");
    duk_push_int(ctx, 320 - topV);
    duk_put_prop_string(ctx, -2, "h");
    return 1;
}

duk_ret_t JSBindings::js_keypadDraw(duk_context *ctx) {
    // App redesenhou a tela: repoe o teclado por cima (mesmo alvo do app)
    if (s_kb && tftInstance) {
        GfxStateGuard guard(*JSBindings::gfx());  // cor/datum do app intactos
        kui::Canvas c(*tftInstance, JSBindings::gfx());
        s_kb->draw(c);
    }
    return 0;
}

duk_ret_t JSBindings::js_keypadClose(duk_context *ctx) {
    keypadCloseSession();
    return 0;
}

