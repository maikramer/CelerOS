#include "JSBindings.h"
#include "JsInternal.h"
#include "../Bluetooth/CelerLink.h"
#include "../Kernel/Core/CelerKernel.h"

// =====================================================
// CelerLink Bindings (Bluetooth entre CelerOS, API 9)
//
// Objeto global CelerLink: start/stop (papel peripheral),
// scan/connect/disconnect (papel central), send/poll
// (mensagens de ate 240 bytes) e status. Chamadas bloqueantes
// seguem o padrao wifiScan: present() antes, timeout com clamp.
// Pareamento por codigo de 6 digitos: API 11 (start {pairing},
// verify, unpair e os campos pairing/verified/code do status).
// =====================================================

duk_ret_t JSBindings::js_linkStart(duk_context *ctx) {
    const char* name = nullptr;
    // lightfunc: a pilha SEMPRE tem nargs valores (faltante = undefined),
    // entao duk_get_top nao detecta argumento omitido — testa o valor
    if (!duk_is_null_or_undefined(ctx, 0)) {
        name = duk_require_string(ctx, 0);
    }
    // API 14: pareamento por codigo e o PADRAO (link aberto deixava qualquer
    // aparelho BLE por perto mandar comandos). Abrir exige {pairing:false}.
    bool pairing = true;
    if (duk_is_object(ctx, 1) && !duk_is_callable(ctx, 1)) {
        duk_get_prop_string(ctx, 1, "pairing");
        if (duk_is_boolean(ctx, -1)) pairing = duk_get_boolean(ctx, -1) != 0;
        duk_pop(ctx);
    }
    present();  // a 1a chamada inicializa o NimBLE (~300ms)
    duk_push_boolean(ctx, CelerLink::start(name, pairing) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_linkStop(duk_context *ctx) {
    duk_push_boolean(ctx, CelerLink::stop() ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_linkScan(duk_context *ctx) {
    int timeoutMs = 2500;
    if (!duk_is_null_or_undefined(ctx, 0)) timeoutMs = duk_require_int(ctx, 0);  // scan() sem arg lancava TypeError
    if (timeoutMs < 500) timeoutMs = 500;
    if (timeoutMs > 8000) timeoutMs = 8000;

    present();  // bloqueante por segundos
    CelerLink::Peer peers[16];  // = cache do scan; RSSI mais forte primeiro
    int n = CelerLink::scan((uint32_t)timeoutMs, peers, 16);
    CelerKernel::noteAppYield();  // espera de 0,5-8s: renova a janela p/ o retorno

    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        duk_push_object(ctx);
        duk_push_string(ctx, peers[i].id);
        duk_put_prop_string(ctx, -2, "id");
        duk_push_string(ctx, peers[i].name);
        duk_put_prop_string(ctx, -2, "name");
        duk_push_int(ctx, peers[i].rssi);
        duk_put_prop_string(ctx, -2, "rssi");
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

duk_ret_t JSBindings::js_linkConnect(duk_context *ctx) {
    const char* id = duk_require_string(ctx, 0);
    int timeoutMs = 4000;
    if (!duk_is_null_or_undefined(ctx, 1)) timeoutMs = duk_require_int(ctx, 1);  // connect(id) idem
    if (timeoutMs < 1000) timeoutMs = 1000;
    if (timeoutMs > 8000) timeoutMs = 8000;

    present();  // bloqueante por segundos
    bool ok = CelerLink::connect(id, (uint32_t)timeoutMs);
    CelerKernel::noteAppYield();  // espera de 1-8s
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_linkDisconnect(duk_context *ctx) {
    duk_push_boolean(ctx, CelerLink::disconnect() ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_linkVerify(duk_context *ctx) {
    const char* code = duk_require_string(ctx, 0);
    present();  // write + read ATT podem esperar segundos
    bool ok = CelerLink::verify(code);
    CelerKernel::noteAppYield();
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_linkUnpair(duk_context *ctx) {
    const char* id = nullptr;
    if (!duk_is_null_or_undefined(ctx, 0)) {
        id = duk_require_string(ctx, 0);
    }
    duk_push_boolean(ctx, CelerLink::unpair(id) ? 1 : 0);
    return 1;
}

// Erro de tamanho com mensagem pre-formatada e duk_error SEM argumentos de
// conversao — throw com %d a partir de lightfunc corrompe o heap do runtime
// (mesma razao do requirePin; bancada 2026-10-02).
static void throwLinkSize(duk_context* ctx) {
    char msg[64];
    snprintf(msg, sizeof(msg), "mensagem deve ter 1 a %d bytes", (int)CelerLink::MAX_MSG);
    duk_error(ctx, DUK_ERR_RANGE_ERROR, msg);
}

duk_ret_t JSBindings::js_linkSend(duk_context *ctx) {
    // String vai crua (bytes UTF-8); objeto e serializado como JSON —
    // comunicacao estruturada sem parser no firmware (quem le decide).
    const char* data;
    size_t len;
    if (duk_is_object(ctx, 0) && !duk_is_callable(ctx, 0)) {
        const char* json = duk_json_encode(ctx, 0);
        if (json == nullptr) {
            duk_error(ctx, DUK_ERR_TYPE_ERROR, "valor nao serializa como JSON");
            return 0;
        }
        len = strlen(json);
        if (len == 0 || len > CelerLink::MAX_MSG) {
            throwLinkSize(ctx);
            return 0;
        }
        char buf[CelerLink::MAX_MSG];
        memcpy(buf, json, len);  // ponteiro do duk nao sobrevive a proxima chamada
        duk_push_boolean(ctx, CelerLink::send(buf, len) ? 1 : 0);
        return 1;
    }
    data = duk_require_lstring(ctx, 0, &len);
    if (len == 0 || len > CelerLink::MAX_MSG) {
        throwLinkSize(ctx);
        return 0;
    }
    duk_push_boolean(ctx, CelerLink::send(data, len) ? 1 : 0);
    return 1;
}

static void throwSealedSize(duk_context* ctx) {
    char msg[64];
    snprintf(msg, sizeof(msg), "mensagem selada deve ter 1 a %d bytes", (int)CelerLink::MAX_SEALED);
    duk_error(ctx, DUK_ERR_RANGE_ERROR, msg);
}

// CelerLink.sendSealed(objOuString) -> bool (API 21): igual ao send, mas
// AES-128-GCM com a chave do pareamento (CelerLink::sendSealed). Teto menor
// (MAX_SEALED): o selo ocupa nonce + tag. false sem bond com o peer.
duk_ret_t JSBindings::js_linkSendSealed(duk_context *ctx) {
    char buf[CelerLink::MAX_MSG];
    size_t len;
    if (duk_is_object(ctx, 0) && !duk_is_callable(ctx, 0)) {
        const char* json = duk_json_encode(ctx, 0);
        if (json == nullptr) {
            duk_error(ctx, DUK_ERR_TYPE_ERROR, "valor nao serializa como JSON");
            return 0;
        }
        len = strlen(json);
        if (len == 0 || len > CelerLink::MAX_SEALED) {
            throwSealedSize(ctx);
            return 0;
        }
        memcpy(buf, json, len);
    } else {
        const char* s = duk_require_lstring(ctx, 0, &len);
        if (len == 0 || len > CelerLink::MAX_SEALED) {
            throwSealedSize(ctx);
            return 0;
        }
        memcpy(buf, s, len);
    }
    const bool ok = CelerLink::sendSealed(buf, len);
    memset(buf, 0, sizeof(buf));  // segredo nao fica na pilha
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}

// CelerLink.pollSealed() -> string | null (API 21): so mensagens seladas que
// autenticaram com o bond do peer conectado. O poll() comum nunca as ve.
duk_ret_t JSBindings::js_linkPollSealed(duk_context *ctx) {
    char buf[CelerLink::MAX_MSG];
    size_t len = 0;
    if (!CelerLink::pollSealed(buf, sizeof(buf), &len)) {
        duk_push_null(ctx);
        return 1;
    }
    duk_push_lstring(ctx, buf, (duk_size_t)len);
    memset(buf, 0, sizeof(buf));
    return 1;
}

duk_ret_t JSBindings::js_linkPoll(duk_context *ctx) {
    char buf[CelerLink::MAX_MSG];
    size_t len = 0;
    if (!CelerLink::poll(buf, sizeof(buf), &len)) {
        duk_push_null(ctx);
        return 1;
    }
    duk_push_lstring(ctx, buf, (duk_size_t)len);
    return 1;
}

duk_ret_t JSBindings::js_linkStatus(duk_context *ctx) {
    CelerLink::Info st;
    CelerLink::info(&st);
    duk_push_object(ctx);
    duk_push_boolean(ctx, st.connected ? 1 : 0);
    duk_put_prop_string(ctx, -2, "connected");
    duk_push_string(ctx, st.peer);
    duk_put_prop_string(ctx, -2, "peer");
    duk_push_boolean(ctx, st.listening ? 1 : 0);
    duk_put_prop_string(ctx, -2, "listening");
    // "central" = nos conectamos; "peripheral" = conectaram em nos
    duk_push_string(ctx, !st.connected ? "" : (st.central ? "central" : "peripheral"));
    duk_put_prop_string(ctx, -2, "role");
    duk_push_string(ctx, st.name);
    duk_put_prop_string(ctx, -2, "name");
    duk_push_boolean(ctx, st.pairing ? 1 : 0);
    duk_put_prop_string(ctx, -2, "pairing");
    duk_push_boolean(ctx, st.verified ? 1 : 0);
    duk_put_prop_string(ctx, -2, "verified");
    // so faz sentido no peripheral em handshake pendente ("" nos demais):
    // e o codigo pra MOSTRAR NA TELA, nunca vai parar no peer
    duk_push_string(ctx, st.code);
    duk_put_prop_string(ctx, -2, "code");
    duk_push_int(ctx, st.mtu);
    duk_put_prop_string(ctx, -2, "mtu");
    duk_push_int(ctx, st.rssi);
    duk_put_prop_string(ctx, -2, "rssi");
    duk_push_int(ctx, st.pending);
    duk_put_prop_string(ctx, -2, "pending");
    duk_push_uint(ctx, st.dropped);
    duk_put_prop_string(ctx, -2, "dropped");
    return 1;
}
