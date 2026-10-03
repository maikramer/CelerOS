#include "JSBindings.h"
#include "sdkconfig.h"
#include "../Hardware/WakeWord.h"

#if CONFIG_CELEROS_WAKE_WORD

// =====================================================
// WakeWord Bindings - deteccao on-device "Hi ESP" (objeto WakeWord, API 20)
// =====================================================
//
// WakeNet (esp-sr, wn10_hiesp) rodando em task propria sobre o mesmo canal
// I2S do microfone — sem rede, sem nuvem. Mesma permissao do Mic: o
// objeto so nasce para apps que declararam "mic" (e o usuario concedeu),
// porque wake word tambem e ouvir o dono. O build sem CONFIG_CELEROS_WAKE_WORD
// (hoje so o cao liga) nao tem o objeto: apps fazem feature-detect
// (typeof WakeWord === "undefined") e vivem de toque/teclado.
//
// Uso tipico (Dog Face): WakeWord.start() no boot; no loop,
// if (WakeWord.poll()) { beep de ack; Mic.start({ms:3000}); ... } — durante
// a gravacao o detector descansa sozinho e volta depois do Mic.stop().

// WakeWord.start(): sobe o modelo e a task (idempotente). false = sem
// mic, sem RAM para o modelo ou modelo ausente no esp-sr.
duk_ret_t JSBindings::js_wakeStart(duk_context *ctx) {
    duk_push_boolean(ctx, WakeWord::start() ? 1 : 0);
    return 1;
}

// WakeWord.stop(): encerra a task e destroi o modelo (libera a RAM).
duk_ret_t JSBindings::js_wakeStop(duk_context *ctx) {
    WakeWord::stop();
    return 0;
}

// WakeWord.poll(): true quando "Hi ESP" foi detectado desde o ultimo
// poll (deteccoes multiplas colapsam numa — o app costuma pollar a cada
// volta do seu loop de ~60 ms).
duk_ret_t JSBindings::js_wakePoll(duk_context *ctx) {
    duk_push_boolean(ctx, WakeWord::poll() ? 1 : 0);
    return 1;
}

// WakeWord.level(): RMS 0..100 do ultimo chunk lido (mesma escala do
// micLevel/Mic.level); -1 com o detector parado.
duk_ret_t JSBindings::js_wakeLevel(duk_context *ctx) {
    duk_push_int(ctx, WakeWord::level());
    return 1;
}

// WakeWord.running(): a task esta viva?
duk_ret_t JSBindings::js_wakeRunning(duk_context *ctx) {
    duk_push_boolean(ctx, WakeWord::running() ? 1 : 0);
    return 1;
}

#endif  // CONFIG_CELEROS_WAKE_WORD
