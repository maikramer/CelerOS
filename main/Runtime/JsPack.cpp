#include "JSBindings.h"
#include "JsInternal.h"
#include "../Bluetooth/CelerNet.h"
#include "../Bluetooth/NetFrame.h"
#include "../Kernel/Core/CelerKernel.h"
#include "../Pack/Pack.h"

#include <stdio.h>
#include <string.h>

// =====================================================
// Pack Bindings (matilha, API 27)
//
// Objeto global Pack: a SEMANTICA da malha por cima do transporte
// CelerNet — membros com o papel de cada um (caps do BEAT), envelopes
// custom unicast (dedup por msgId no firmware) e o handoff da musica em
// curso (a festa chiptune continua no vizinho com alto-falante, do mesmo
// ponto). Nada bloqueia; o handoff devolve false se nao ha musica
// tocando, malha desligada ou vizinho sem speaker.
// =====================================================

namespace {
// caps num bitmask -> {speaker, mic, display, motors, leds, hub}
void pushCaps(duk_context* ctx, uint8_t caps) {
    duk_push_object(ctx);
    duk_push_boolean(ctx, (caps & netframe::CAPS_SPEAKER) ? 1 : 0);
    duk_put_prop_string(ctx, -2, "speaker");
    duk_push_boolean(ctx, (caps & netframe::CAPS_MIC) ? 1 : 0);
    duk_put_prop_string(ctx, -2, "mic");
    duk_push_boolean(ctx, (caps & netframe::CAPS_DISPLAY) ? 1 : 0);
    duk_put_prop_string(ctx, -2, "display");
    duk_push_boolean(ctx, (caps & netframe::CAPS_MOTORS) ? 1 : 0);
    duk_put_prop_string(ctx, -2, "motors");
    duk_push_boolean(ctx, (caps & netframe::CAPS_LEDS) ? 1 : 0);
    duk_put_prop_string(ctx, -2, "leds");
    duk_push_boolean(ctx, (caps & netframe::CAPS_HUB) ? 1 : 0);
    duk_put_prop_string(ctx, -2, "hub");
}
}  // namespace

duk_ret_t JSBindings::js_packMe(duk_context *ctx) {
    CelerNet::Info st;
    CelerNet::info(&st);
    char id[8];
    snprintf(id, sizeof(id), "%04X", st.node);
    duk_push_object(ctx);
    duk_push_string(ctx, id);
    duk_put_prop_string(ctx, -2, "id");
    duk_push_string(ctx, st.name);
    duk_put_prop_string(ctx, -2, "name");
    pushCaps(ctx, Pack::myCaps());
    duk_put_prop_string(ctx, -2, "caps");
    duk_push_boolean(ctx, st.active ? 1 : 0);
    duk_put_prop_string(ctx, -2, "meshActive");
    return 1;
}

duk_ret_t JSBindings::js_packMembers(duk_context *ctx) {
    Pack::Member list[CelerNet::NODES_MAX];
    int n = Pack::members(list, CelerNet::NODES_MAX);
    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        char id[8];
        snprintf(id, sizeof(id), "%04X", list[i].id);
        duk_push_object(ctx);
        duk_push_string(ctx, id);
        duk_put_prop_string(ctx, -2, "id");
        duk_push_string(ctx, list[i].name);
        duk_put_prop_string(ctx, -2, "name");
        pushCaps(ctx, list[i].caps);
        duk_put_prop_string(ctx, -2, "caps");
        duk_push_int(ctx, list[i].rssi);
        duk_put_prop_string(ctx, -2, "rssi");
        duk_push_int(ctx, list[i].hops);
        duk_put_prop_string(ctx, -2, "hops");
        duk_push_uint(ctx, list[i].lastSeenMs / 1000);
        duk_put_prop_string(ctx, -2, "lastSeen");
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

duk_ret_t JSBindings::js_packSend(duk_context *ctx) {
    const char* to = duk_require_string(ctx, 0);
    uint16_t dst = 0;
    // Destino sumido devolve FALSE (nao throw): a presenca na malha oscila
    // por natureza e um app de radio movel nao pode morrer porque o vizinho
    // saiu da tabela no instante do send — mesmo contrato do stub do
    // harness e do CelerNet.broadcast com a malha desligada.
    if (!CelerNet::resolveDest(to, &dst)) {
        duk_push_false(ctx);
        return 1;
    }
    uint8_t buf[Pack::MAX_PAYLOAD];
    size_t len;
    if (duk_is_object(ctx, 1) && !duk_is_callable(ctx, 1)) {
        const char* json = duk_json_encode(ctx, 1);
        if (json == nullptr) {
            duk_error(ctx, DUK_ERR_TYPE_ERROR, "valor nao serializa como JSON");
            return 0;
        }
        len = strlen(json);
        if (len == 0 || len > Pack::MAX_PAYLOAD) {
            char msg[64];
            snprintf(msg, sizeof(msg), "envelope deve ter 1 a %d bytes", (int)Pack::MAX_PAYLOAD);
            duk_error(ctx, DUK_ERR_RANGE_ERROR, msg);
            return 0;
        }
        memcpy(buf, json, len);
    } else {
        const char* s = duk_require_lstring(ctx, 1, &len);
        if (len == 0 || len > Pack::MAX_PAYLOAD) {
            char msg[64];
            snprintf(msg, sizeof(msg), "envelope deve ter 1 a %d bytes", (int)Pack::MAX_PAYLOAD);
            duk_error(ctx, DUK_ERR_RANGE_ERROR, msg);
            return 0;
        }
        memcpy(buf, s, len);
    }
    bool urgent = false;
    if (duk_is_object(ctx, 2) && !duk_is_callable(ctx, 2)) {
        duk_get_prop_string(ctx, 2, "urgent");
        if (duk_is_boolean(ctx, -1)) urgent = duk_get_boolean(ctx, -1) != 0;
        duk_pop(ctx);
    }
    duk_push_boolean(ctx, Pack::send(dst, Pack::KIND_CUSTOM, buf, len, urgent) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_packPoll(duk_context *ctx) {
    uint16_t from = 0;
    char fromName[16];
    uint8_t data[Pack::MAX_PAYLOAD];
    size_t len = sizeof(data);
    if (!Pack::pollCustom(&from, fromName, sizeof(fromName), data, &len)) {
        duk_push_null(ctx);
        return 1;
    }
    char id[8];
    snprintf(id, sizeof(id), "%04X", from);
    duk_push_object(ctx);
    duk_push_string(ctx, id);
    duk_put_prop_string(ctx, -2, "from");
    duk_push_string(ctx, fromName);
    duk_put_prop_string(ctx, -2, "fromName");
    duk_push_lstring(ctx, (const char*)data, (duk_size_t)len);
    duk_put_prop_string(ctx, -2, "data");
    return 1;
}

duk_ret_t JSBindings::js_packHandoffMusic(duk_context *ctx) {
    uint16_t to = 0;
    if (!duk_is_null_or_undefined(ctx, 0)) {
        const char* dest = duk_require_string(ctx, 0);
        // destino sumido = false (sem throw), como o resto da API de rede
        if (!CelerNet::resolveDest(dest, &to)) {
            duk_push_false(ctx);
            return 1;
        }
    }
    present();  // o envio drena a fila da malha: da chance ao tick
    bool ok = Pack::handoffMusic(to);
    CelerKernel::noteAppYield();
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}
