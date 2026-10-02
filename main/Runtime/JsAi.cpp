#include "JSBindings.h"
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"
#include "../WebManager/WebManager.h"
#include "HttpClient.h"
#include "JsInternal.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// =====================================================
// AI Bindings - DeepSeek (objeto AI, API level 18)
// =====================================================
//
// A chave da API NUNCA entra no JS: vive em /local/deepseek_key.txt
// (arquivo protegido pelo jail do FS, gravado pelo dono via celerctl
// push ou file manager da web) e e lida aqui a cada requisicao — trocar
// a chave nao pede reboot (mesma filosofia do /local/ota_url.txt).
//
// Formato OpenAI-compatible: AI.chat(opts, cb) serializa o propio opts
// com duk_json_encode (o C++ so fixa stream:false e injeta defaults) —
// nenhuma montagem manual de JSON, nenhum escape de aspas do usuario.
//
// O POST roda em task propria (padrao do Net assincrono em JsNet.cpp) e
// o resultado e entregue ao callback no present() (padrao dos timers em
// JsTimers.cpp: funcao no heap stash, duk_pcall; erro de script PROPAGA
// como erro do app).

// Cert (*.deepseek.com, Amazon RSA 2048) ja esta no cert bundle FULL e
// CMN — nenhuma board precisa de PEM custom.
static const char* AI_URL = "https://api.deepseek.com/chat/completions";
static const char* AI_KEY_FILE = "/local/deepseek_key.txt";
static const char* AI_DEFAULT_MODEL = "deepseek-flash";

// Teto do corpo da resposta (mesmo sink malloc/realloc do Net.get: sem
// RAM a requisicao falha limpa em vez de abortar o aparelho). Com o
// default de max_tokens 1024 a resposta tipica fica em poucos KB.
#define AI_MAX_BODY 32768

// Completions demoradas passam com folga dos 10s do Net.post.
#define AI_TIMEOUT_MS 90000

struct AiBody {
    char* p = nullptr;
    size_t n = 0, cap = 0;
    bool append(const char* d, size_t len) {
        if (n >= AI_MAX_BODY) return true;  // teto: descarta o excedente
        if (len > AI_MAX_BODY - n) len = AI_MAX_BODY - n;
        if (n + len > cap) {
            size_t want = cap * 2 > n + len ? cap * 2 : n + len;
            if (want < 1024) want = 1024;
            if (want > AI_MAX_BODY) want = AI_MAX_BODY;
            char* q = (char*)realloc(p, want);
            if (q == nullptr) q = (char*)realloc(p, want = n + len);  // exato
            if (q == nullptr) return false;
            p = q;
            cap = want;
        }
        memcpy(p + n, d, len);
        n += len;
        return true;
    }
};

// Um slot serial: assistente conversa uma requisicao por vez. Estado
// guardado por mutex e UM unico escritor — a task dona publica 1->2
// (mesma disciplina de concorrencia do Net assincrono).
struct AiSlot {
    SemaphoreHandle_t mux = nullptr;  // criado no 1o uso
    int state = 0;                    // 0=livre 1=task rodando 2=resultado pronto
    bool discard = false;             // cancel/reset: resultado sera descartado
    bool ok = false;                  // HTTP 2xx
    int status = 0;
    char* body = nullptr;             // malloc (AiBody), escrito SO pela task (1->2)
    size_t bodyLen = 0;
    char error[96] = {0};
    // Pedido (imutavel enquanto state==1; a task libera apos o POST)
    char* payload = nullptr;
    size_t payloadLen = 0;
    char* key = nullptr;
};
static AiSlot s_aiSlot;
static bool s_aiPending = false;  // callback do app esperando resultado

static bool aiMuxTake(AiSlot& s) {
    if (s.mux == nullptr) s.mux = xSemaphoreCreateMutex();
    return s.mux != nullptr && xSemaphoreTake(s.mux, portMAX_DELAY) == pdTRUE;
}

static void aiFreeBody(AiSlot& s) {
    free(s.body);
    s.body = nullptr;
    s.bodyLen = 0;
}

static char* aiDupBuf(const char* src, size_t n) {
    char* p = (char*)malloc(n + 1);
    if (p == nullptr) return nullptr;
    memcpy(p, src, n);
    p[n] = '\0';
    return p;
}

static std::string aiReadKey() {
    if (!FileSystem::exists(AI_KEY_FILE)) return std::string();
    return kstr::trim(FileSystem::readTextFile(AI_KEY_FILE));
}

// Chamado no lancamento de cada app (JSBindings::init): a requisicao do
// app anterior e descartada e o stash com o callback morreu com o heap.
void JSBindings::aiReset() {
    s_aiPending = false;
    AiSlot& s = s_aiSlot;
    if (!aiMuxTake(s)) return;
    s.discard = true;
    if (s.state == 2) {
        s.state = 0;
        aiFreeBody(s);
    }
    xSemaphoreGive(s.mux);
}

static void aiTask(void* raw) {
    AiSlot* s = (AiSlot*)raw;
    AiBody got;
    bool ok;
    int status;
    char err[96];
    {
        HttpClient http;
        http.setTimeout(AI_TIMEOUT_MS);
        http.setBearerAuth(s->key ? s->key : "");
        http.setBodySink([&got](const char* d, size_t len) { return got.append(d, len); });
        HttpResponse resp =
            http.postJson(AI_URL, std::string(s->payload ? s->payload : "", s->payloadLen));
        ok = resp.isOk();
        status = resp.statusCode;
        snprintf(err, sizeof(err), "%s", resp.success ? "" : resp.errorMessage.c_str());
    }  // TLS/cliente liberados antes de publicar o resultado
    // Pedido consumido: a task e a unica que toca payload/key enquanto
    // state==1 (o begin so reescreve quando nenhuma task esta viva)
    free(s->payload);
    s->payload = nullptr;
    s->payloadLen = 0;
    free(s->key);
    s->key = nullptr;
    if (aiMuxTake(*s)) {
        aiFreeBody(*s);
        if (s->discard) {
            free(got.p);
            s->ok = false;
            s->status = 0;
            snprintf(s->error, sizeof(s->error), "cancelado");
        } else {
            s->ok = ok;
            s->status = status;
            s->body = got.p;  // posse transferida ao slot
            s->bodyLen = got.n;
            snprintf(s->error, sizeof(s->error), "%s", err);
        }
        s->discard = false;
        s->state = 2;
        xSemaphoreGive(s->mux);
    } else {
        free(got.p);
    }
    vTaskDelete(nullptr);
}

// true = pedido no ar (o callback dispara 1x); false = ocupado/sem RAM
// (NENHUM callback). Erros de configuracao lancam (script ve mensagem
// legivel; o app pode checar AI.configured()/Net.isConnected() antes).
duk_ret_t JSBindings::js_aiChat(duk_context *ctx) {
    JSBindings::present();  // cedida universal: o "Pensando..." aparece antes
    duk_require_object(ctx, 0);
    duk_require_callable(ctx, 1);
    if (!WebManager::isWifiConnected()) {
        duk_error(ctx, DUK_ERR_ERROR, "AI: WiFi is not connected");
    }
    std::string key = aiReadKey();
    if (key.empty()) {
        duk_error(ctx, DUK_ERR_ERROR, "AI: chave ausente (/local/deepseek_key.txt)");
    }

    // Defaults do framework por cima do opts do app
    duk_dup(ctx, 0);
    duk_push_boolean(ctx, 0);  // o runtime nao consome SSE
    duk_put_prop_string(ctx, -2, "stream");
    if (!duk_has_prop_string(ctx, -1, "model")) {
        duk_push_string(ctx, AI_DEFAULT_MODEL);
        duk_put_prop_string(ctx, -2, "model");
    }
    if (!duk_has_prop_string(ctx, -1, "max_tokens")) {
        duk_push_int(ctx, 1024);  // limita latencia e o corpo longe do teto
        duk_put_prop_string(ctx, -2, "max_tokens");
    }
    duk_get_prop_string(ctx, -1, "messages");
    if (!duk_is_array(ctx, -1)) {
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "AI: opts.messages deve ser um array");
    }
    duk_pop(ctx);
    const char* encoded = duk_json_encode(ctx, -1);
    std::string payload = encoded ? encoded : "{}";
    duk_pop(ctx);  // opts ja codificado

    AiSlot& s = s_aiSlot;
    if (!aiMuxTake(s)) { duk_push_boolean(ctx, 0); return 1; }
    if (s.state == 1) {  // requisicao em curso (ou zumbi cancelado)
        xSemaphoreGive(s.mux);
        duk_push_boolean(ctx, 0);
        return 1;
    }
    // state 0/2: slot livre (resultado anterior nao consumido some aqui)
    s.discard = false;
    s.ok = false;
    s.status = 0;
    aiFreeBody(s);
    s.error[0] = '\0';
    free(s.payload);
    s.payload = aiDupBuf(payload.data(), payload.size());
    free(s.key);
    s.key = aiDupBuf(key.data(), key.size());
    if (s.payload == nullptr || s.key == nullptr) {
        free(s.payload); s.payload = nullptr;
        free(s.key); s.key = nullptr;
        xSemaphoreGive(s.mux);
        duk_error(ctx, DUK_ERR_ERROR, "AI: sem RAM para montar o pedido");
    }
    s.payloadLen = payload.size();
    s.state = 1;
    xSemaphoreGive(s.mux);

    // Referencia do callback no heap stash: morre com o app (nada atravessa)
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "_ai");
    if (!duk_is_array(ctx, -1)) {
        duk_pop(ctx);  // nao e array (novo heap): cria
        duk_push_array(ctx);
        duk_put_prop_string(ctx, -2, "_ai");
        duk_get_prop_string(ctx, -1, "_ai");
        duk_remove(ctx, -2);
    }
    duk_dup(ctx, 1);
    duk_put_prop_index(ctx, -2, 0);
    duk_pop(ctx);  // array
    duk_pop(ctx);  // stash
    s_aiPending = true;

    // Stack de 32KB: mesmo HttpClient/TLS do Net assincrono (com 12KB o
    // handshake estourava — bancada 2026-10-02). Boards sem PSRAM nao
    // abrem apps com "net", o custo e so no S3.
    if (xTaskCreate(aiTask, "jsai", 32768, &s, 3, nullptr) != pdPASS) {
        // task nao nasceu: devolve o slot e esquece o callback
        aiMuxTake(s);
        if (s.state == 1) s.state = 0;
        xSemaphoreGive(s.mux);
        s_aiPending = false;
        duk_push_heap_stash(ctx);
        duk_get_prop_string(ctx, -1, "_ai");
        duk_push_undefined(ctx);
        duk_put_prop_index(ctx, -2, 0);
        duk_pop(ctx);   // array
        duk_pop(ctx);   // stash
        duk_push_boolean(ctx, 0);
        return 1;
    }
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_aiConfigured(duk_context *ctx) {
    duk_push_boolean(ctx, aiReadKey().empty() ? 0 : 1);
    return 1;
}

// Esquece a requisicao em curso (botao "cancelar" do app; o exit do app
// chama aiReset que faz o mesmo). Nao mata a task (matar no meio do TLS
// vaza): ela mesma publica "cancelado" num slot que ninguem vai ler.
duk_ret_t JSBindings::js_aiCancel(duk_context *ctx) {
    AiSlot& s = s_aiSlot;
    bool dropped = false;
    if (aiMuxTake(s)) {
        if (s.state == 1) {
            s.discard = true;
            dropped = true;
        }
        xSemaphoreGive(s.mux);
    }
    s_aiPending = false;
    duk_push_boolean(ctx, dropped ? 1 : 0);
    return 1;
}

// Monta o objeto do callback. Body (malloc) ainda do chamador: nada aqui
// pode vazar — duk_safe_call captura o longjmp de OOM e o dono libera.
struct AiResult {
    bool ok;
    int status;
    char* body;
    size_t bodyLen;
    const char* error;
};

// decode protegido: resposta cortada no teto de 32KB vem com JSON invalido
// (content fica null e o raw cru segue pro app, que decide)
static duk_ret_t aiDecodeJson(duk_context* ctx, void*) {
    duk_json_decode(ctx, -1);
    return 1;
}

static duk_ret_t aiPushResult(duk_context* ctx, void* udata) {
    AiResult* r = (AiResult*)udata;
    duk_push_object(ctx);
    duk_push_boolean(ctx, r->ok ? 1 : 0);
    duk_put_prop_string(ctx, -2, "ok");
    duk_push_int(ctx, r->status);
    duk_put_prop_string(ctx, -2, "status");
    duk_push_lstring(ctx, r->body ? r->body : "", r->bodyLen);
    duk_put_prop_string(ctx, -2, "raw");
    duk_push_null(ctx);
    duk_put_prop_string(ctx, -2, "content");
    if (!r->ok) {
        duk_push_string(ctx, r->error);
        duk_put_prop_string(ctx, -2, "error");
        return 1;
    }
    // Envelope OpenAI: choices[0].message.content + usage. Falha de parse
    // NAO e erro da entrega (o app tem o raw).
    duk_push_lstring(ctx, r->body ? r->body : "", r->bodyLen);
    if (duk_safe_call(ctx, aiDecodeJson, nullptr, 1, 1) == DUK_EXEC_SUCCESS &&
        duk_is_object(ctx, -1)) {
        duk_get_prop_string(ctx, -1, "choices");  // [res, parsed, choices]
        if (duk_is_array(ctx, -1)) {
            duk_get_prop_index(ctx, -1, 0);           // +choices[0]
            duk_get_prop_string(ctx, -1, "message");  // +message
            duk_get_prop_string(ctx, -1, "content");  // +content
            if (duk_is_string(ctx, -1)) {
                duk_put_prop_string(ctx, -6, "content");  // res.content
            } else {
                duk_pop(ctx);
            }
            duk_pop(ctx);  // message
            duk_pop(ctx);  // choices[0]
        }
        duk_pop(ctx);  // choices
        duk_get_prop_string(ctx, -1, "usage");  // [res, parsed, usage]
        if (duk_is_object(ctx, -1)) {
            duk_put_prop_string(ctx, -3, "usage");  // res.usage
        } else {
            duk_pop(ctx);
        }
    }
    duk_pop(ctx);  // parsed (ou string de parse quebrado)
    return 1;
}

// Chamado pelo present() (apos timersTick). Entrega 1x o resultado ao
// callback do app. Erro de script PROPAGA (mesma politica dos timers: o
// app morre com a tela de erro; o present e 1a linha dos bindings
// chamadores, o longjmp nao atravessa recurso C aberto).
void JSBindings::aiTick(duk_context* ctx) {
    if (ctx == nullptr || !s_aiPending) return;
    AiSlot& s = s_aiSlot;
    if (!aiMuxTake(s)) return;
    if (s.state != 2) {
        xSemaphoreGive(s.mux);
        return;
    }
    // Toma posse do resultado sob o lock e monta o objeto fora dele
    char errBuf[96];
    AiResult r{s.ok, s.status, s.body, s.bodyLen, errBuf};
    memcpy(errBuf, s.error, sizeof(errBuf));  // s.error sempre NUL-terminado
    s.body = nullptr;
    s.bodyLen = 0;
    s.state = 0;  // slot liberado para o proximo chat
    s_aiPending = false;
    xSemaphoreGive(s.mux);

    // callback do stash (one-shot: a referencia sai ja)
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "_ai");
    if (!duk_is_array(ctx, -1)) {
        duk_pop_2(ctx);
        free(r.body);
        return;
    }
    duk_get_prop_index(ctx, -1, 0);
    duk_push_undefined(ctx);
    duk_put_prop_index(ctx, -3, 0);
    duk_remove(ctx, -2);  // tira o array
    duk_remove(ctx, -2);  // tira o stash — fn no topo
    if (!duk_is_callable(ctx, -1)) {
        duk_pop(ctx);
        free(r.body);
        return;
    }

    char* owned = r.body;
    duk_int_t rc = duk_safe_call(ctx, aiPushResult, &r, 0, 1);
    free(owned);
    if (rc != DUK_EXEC_SUCCESS) duk_throw(ctx);
    esp_task_wdt_reset();  // callback pode desenhar bastante
    if (duk_pcall(ctx, 1) != DUK_EXEC_SUCCESS) duk_throw(ctx);
}
