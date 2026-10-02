#include "JSBindings.h"
#include "JsInternal.h"
#include <Arduino.h>
#include "esp_task_wdt.h"

// =====================================================
// Timers JS (API 12): setTimeout/setInterval/clearTimeout/clearInterval
// =====================================================
//
// Sem event loop dedicado, os timers cooperam com o app: disparam no
// present() — que roda no comeco de System.delay, System.getTouch e de
// toda chamada bloqueante. Um app em loop apertado SEM ceder (sem
// delay/getTouch) nao ve os timers dispararem (e ja derruba o WDT sozinho).
//
// Erro no callback PROPAGA para o frame do app (mesma politica de erro de
// qualquer binding): o app morre com a tela de erro e a stack do callback.
// O present() e sempre a 1a linha dos bindings que o chamam, então o
// longjmp nao atravessa recurso C alocado (convencao do runtime).
//
// Estado: tabela C fixa (8 slots) + referencias das funcoes num array do
// heap stash "_tmr" (morre com o heap do app — nada atravessa apps).

namespace {
struct TimerSlot {
    bool used = false;
    bool repeat = false;
    uint32_t period = 0;     // ms (interval) / timeout (timeout)
    uint32_t nextFire = 0;   // ms()
    int id = 0;              // handle unico do app (>= 1); slot reusado ganha id novo
};
TimerSlot s_timers[8];
bool s_inTick = false;       // callback chamou present(): adia o proximo disparo
int s_nextId = 0;

// Ajusta a base do array "_tmr" no heap stash (cria se 1a vez). Pilha: +1
// (o array fica no topo).
void timerStashArray(duk_context* ctx) {
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "_tmr");
    if (duk_is_array(ctx, -1)) {
        duk_remove(ctx, -2);  // tira o stash, fica o array
        return;
    }
    duk_pop(ctx);
    duk_push_array(ctx);
    for (int i = 0; i < 8; i++) {
        duk_push_undefined(ctx);
        duk_put_prop_index(ctx, -2, i);
    }
    duk_put_prop_string(ctx, -2, "_tmr");
    duk_pop(ctx);              // stash
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "_tmr");
    duk_remove(ctx, -2);
}

void timersReset() {
    for (auto& t : s_timers) t = TimerSlot{};
    s_nextId = 0;
    s_inTick = false;
    // As referencias vivem no heap stash: morrem com o heap do app.
}
}  // namespace

// Chamado pelo present(). Pode lancar (erro do callback propagando).
void JSBindings::timersTick(duk_context* ctx) {
    if (ctx == nullptr || s_inTick) return;
    uint32_t now = millis();
    for (int i = 0; i < 8; i++) {
        TimerSlot& t = s_timers[i];
        if (!t.used || (int32_t)(now - t.nextFire) < 0) continue;

        // Agenda ANTES de disparar: um interval que demora mais que o
        // periodo nao dispara em rajada (pula os atrasados)
        if (t.repeat) {
            t.nextFire += t.period;
            while ((int32_t)(now - t.nextFire) >= 0) t.nextFire += t.period;
        } else {
            t.used = false;
        }

        timerStashArray(ctx);
        duk_get_prop_index(ctx, -1, i);
        duk_require_callable(ctx, -1);  // slot valido sempre tem callable
        // remove a referencia de one-shot JA disparado / de quem cancelou
        if (!t.used) {
            duk_push_undefined(ctx);
            duk_put_prop_index(ctx, -3, i);
        }
        duk_remove(ctx, -2);  // tira o array; fn no topo

        s_inTick = true;
        esp_task_wdt_reset();  // callback pode desenhar bastante
        duk_int_t rc = duk_pcall(ctx, 0);
        s_inTick = false;
        if (rc != DUK_EXEC_SUCCESS) {
            duk_throw(ctx);  // erro do callback vira erro do app (nao retorna)
        }
        now = millis();
    }
}

static duk_ret_t timerStart(duk_context *ctx, bool repeat) {
    duk_require_callable(ctx, 0);
    duk_uint_t ms = duk_require_uint(ctx, 1);

    int slot = -1;
    for (int i = 0; i < 8; i++)
        if (!s_timers[i].used) { slot = i; break; }
    if (slot < 0) { duk_push_int(ctx, 0); return 1; }  // sem slot

    timerStashArray(ctx);
    duk_dup(ctx, 0);
    duk_put_prop_index(ctx, -2, slot);
    duk_pop(ctx);  // array

    TimerSlot& t = s_timers[slot];
    t.used = true;
    t.repeat = repeat;
    t.period = ms < 10 ? 10 : ms;  // piso de 10ms: dispara no proximo present()
    t.nextFire = millis() + t.period;
    // id monotonico por app (nunca o slot): o id de um timeout que ja
    // disparou nao pode cancelar o timer novo que reusou o mesmo slot
    if (++s_nextId <= 0) s_nextId = 1;
    t.id = s_nextId;
    duk_push_int(ctx, t.id);  // >= 1 (0 = falha: sem slot)
    return 1;
}

duk_ret_t JSBindings::js_setTimeout(duk_context *ctx) {
    return timerStart(ctx, false);
}

duk_ret_t JSBindings::js_setInterval(duk_context *ctx) {
    return timerStart(ctx, true);
}

// clearTimeout e clearInterval sao o mesmo corretor: id 0/ausente nao faz nada
static duk_ret_t timerClear(duk_context *ctx) {
    // id = handle opaco do setTimeout/setInterval: procura o slot VIVO com
    // esse id (id de timer ja disparado/cancelado nao acha nada)
    int id = duk_is_number(ctx, 0) ? duk_require_int(ctx, 0) : 0;
    if (id < 1) return 0;
    for (int i = 0; i < 8; i++) {
        if (!s_timers[i].used || s_timers[i].id != id) continue;
        s_timers[i].used = false;
        timerStashArray(ctx);
        duk_push_undefined(ctx);
        duk_put_prop_index(ctx, -2, i);
        duk_pop(ctx);  // array
        break;
    }
    return 0;
}

duk_ret_t JSBindings::js_clearTimeout(duk_context *ctx) { return timerClear(ctx); }
duk_ret_t JSBindings::js_clearInterval(duk_context *ctx) { return timerClear(ctx); }

// Reset por app (chamado no init da fachada)
void JSBindings::timersResetAll() { timersReset(); }
