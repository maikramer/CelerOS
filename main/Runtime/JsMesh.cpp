#include "JSBindings.h"
#include "JsInternal.h"
#include "../Bluetooth/CelerNet.h"
#include "../Kernel/Core/CelerKernel.h"

// =====================================================
// CelerNet Bindings (malha BLE por flood de advertising, API 26)
//
// Objeto global CelerNet: start/stop (liga o no; persiste no
// setting), broadcast (mensagem para TODA a rede, saltos por TTL),
// poll (mensagens que chegaram, com origem e saltos), nodes
// (presenca: nos ouvidos recentemente) e status. Nenhuma chamada
// bloqueia (a maquina de radio roda no servico ALWAYS) — exceto a
// 1a start, que pode subir o NimBLE (~300 ms).
// =====================================================

duk_ret_t JSBindings::js_meshStart(duk_context *ctx) {
    const char* name = nullptr;
    const char* net = nullptr;
    bool relay = true;
    if (duk_is_object(ctx, 0) && !duk_is_callable(ctx, 0)) {
        duk_get_prop_string(ctx, 0, "name");
        if (duk_is_string(ctx, -1)) name = duk_get_string(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "net");
        if (duk_is_string(ctx, -1)) net = duk_get_string(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "relay");
        if (duk_is_boolean(ctx, -1)) relay = duk_get_boolean(ctx, -1) != 0;
        duk_pop(ctx);
    }
    present();  // a 1a chamada pode inicializar o NimBLE (~300 ms)
    bool ok = CelerNet::start(name, net, relay);
    CelerKernel::noteAppYield();
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_meshStop(duk_context *ctx) {
    duk_push_boolean(ctx, CelerNet::stop() ? 1 : 0);
    return 1;
}

// Erro de tamanho com mensagem pre-formatada: throw com %d a partir de
// lightfunc corrompe o heap do runtime (mesma razao do CelerLink.send).
static void throwMeshSize(duk_context* ctx) {
    char msg[64];
    snprintf(msg, sizeof(msg), "mensagem deve ter 1 a %d bytes", (int)CelerNet::MAX_MSG);
    duk_error(ctx, DUK_ERR_RANGE_ERROR, msg);
}

duk_ret_t JSBindings::js_meshBroadcast(duk_context *ctx) {
    // Mesma regra do CelerLink.send: string crua, objeto vira JSON — quem
    // le do outro lado decide o formato.
    uint8_t buf[CelerNet::MAX_MSG];
    size_t len;
    if (duk_is_object(ctx, 0) && !duk_is_callable(ctx, 0)) {
        const char* json = duk_json_encode(ctx, 0);
        if (json == nullptr) {
            duk_error(ctx, DUK_ERR_TYPE_ERROR, "valor nao serializa como JSON");
            return 0;
        }
        len = strlen(json);
        if (len == 0 || len > CelerNet::MAX_MSG) {
            throwMeshSize(ctx);
            return 0;
        }
        memcpy(buf, json, len);
    } else {
        const char* s = duk_require_lstring(ctx, 0, &len);
        if (len == 0 || len > CelerNet::MAX_MSG) {
            throwMeshSize(ctx);
            return 0;
        }
        memcpy(buf, s, len);
    }
    uint32_t ttl = CelerNet::TTL_DEFAULT;
    if (!duk_is_null_or_undefined(ctx, 1)) {
        ttl = (uint32_t)duk_require_int(ctx, 1);
        if (ttl < 1) ttl = 1;
        if (ttl > 8) ttl = 8;
    }
    duk_push_boolean(ctx, CelerNet::broadcast(buf, len, (uint8_t)ttl) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_meshPoll(duk_context *ctx) {
    CelerNet::Msg m;
    if (!CelerNet::poll(&m)) {
        duk_push_null(ctx);
        return 1;
    }
    char id[8];
    snprintf(id, sizeof(id), "%04X", m.from);
    duk_push_object(ctx);
    duk_push_string(ctx, id);
    duk_put_prop_string(ctx, -2, "from");
    duk_push_string(ctx, m.fromName);
    duk_put_prop_string(ctx, -2, "fromName");
    duk_push_lstring(ctx, (const char*)m.data, (duk_size_t)m.len);
    duk_put_prop_string(ctx, -2, "msg");
    duk_push_int(ctx, m.hops);
    duk_put_prop_string(ctx, -2, "hops");
    duk_push_int(ctx, m.rssi);
    duk_put_prop_string(ctx, -2, "rssi");
    return 1;
}

duk_ret_t JSBindings::js_meshNodes(duk_context *ctx) {
    CelerNet::Node list[CelerNet::NODES_MAX];
    int n = CelerNet::nodes(list, CelerNet::NODES_MAX);
    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        char id[8];
        snprintf(id, sizeof(id), "%04X", list[i].id);
        duk_push_object(ctx);
        duk_push_string(ctx, id);
        duk_put_prop_string(ctx, -2, "id");
        duk_push_string(ctx, list[i].name);
        duk_put_prop_string(ctx, -2, "name");
        duk_push_int(ctx, list[i].rssi);
        duk_put_prop_string(ctx, -2, "rssi");
        duk_push_int(ctx, list[i].hops);
        duk_put_prop_string(ctx, -2, "hops");
        // idade em segundos: e o que se mostra na lista
        duk_push_uint(ctx, list[i].lastSeenMs / 1000);
        duk_put_prop_string(ctx, -2, "lastSeen");
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

duk_ret_t JSBindings::js_meshStatus(duk_context *ctx) {
    CelerNet::Info st;
    CelerNet::info(&st);
    char id[8];
    snprintf(id, sizeof(id), "%04X", st.node);
    duk_push_object(ctx);
    duk_push_boolean(ctx, st.active ? 1 : 0);
    duk_put_prop_string(ctx, -2, "active");
    duk_push_boolean(ctx, st.relay ? 1 : 0);
    duk_put_prop_string(ctx, -2, "relay");
    duk_push_string(ctx, id);
    duk_put_prop_string(ctx, -2, "node");
    duk_push_string(ctx, st.name);
    duk_put_prop_string(ctx, -2, "name");
    duk_push_string(ctx, st.net);
    duk_put_prop_string(ctx, -2, "net");
    duk_push_int(ctx, st.txQueued);
    duk_put_prop_string(ctx, -2, "txQueued");
    duk_push_uint(ctx, st.txDropped);
    duk_put_prop_string(ctx, -2, "txDropped");
    duk_push_uint(ctx, st.rxDropped);
    duk_put_prop_string(ctx, -2, "rxDropped");
    duk_push_uint(ctx, st.relayed);
    duk_put_prop_string(ctx, -2, "relayed");
    duk_push_int(ctx, st.heard);
    duk_put_prop_string(ctx, -2, "heard");
    return 1;
}
