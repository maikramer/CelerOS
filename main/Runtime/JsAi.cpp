#include "JSBindings.h"
#include "../FileSystem/FileSystem.h"
#include "../USBDevice/LogSink.h"
#include "../Utils/StrUtils.h"
#include "../WebManager/WebManager.h"
#include "HttpClient.h"
#include "JsInternal.h"
#include "esp_task_wdt.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"  // xTaskCreateWithCaps
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "../Boards/Board.h"

// =====================================================
// AI Bindings - chat LLM (objeto AI, API level 18)
// =====================================================
//
// A chave da API NUNCA entra no JS: vive em /local/<provider>_key.txt
// (arquivo protegido pelo jail do FS, gravado pelo dono via celerctl
// push ou file manager da web) e e lida aqui a cada requisicao — trocar
// a chave nao pede reboot (mesma filosofia do /local/ota_url.txt).
//
// Formato OpenAI-compatible (DeepSeek e OpenRouter falam o mesmo schema):
// AI.chat(opts, cb) serializa o propio opts com duk_json_encode (o C++ so
// fixa stream:false e injeta defaults) — nenhuma montagem manual de JSON,
// nenhum escape de aspas do usuario. opts.provider escolhe a casa ("deepseek"
// default, "openrouter" para o app Qwen e afins); o campo sai do payload.
//
// O POST roda em task propria (padrao do Net assincrono em JsNet.cpp) e
// o resultado e entregue ao callback no present() (padrao dos timers em
// JsTimers.cpp: funcao no heap stash, duk_pcall; erro de script PROPAGA
// como erro do app).
//
// Function calling (API 20): `tools`/`tool_choice` no opts ja atravessam
// o duk_json_encode sem toque nosso — o que entra aqui de novo e o parse
// da resposta: choices[0].message.tool_calls vira r.toolCalls
// ([{id,name,args}]; args decodificado quando e JSON valido) e
// choices[0].finish_reason vira r.finishReason ("tool_calls" ou "stop").

// Cert (*.deepseek.com, Amazon RSA 2048 / *.openrouter.ai, GTS) ja esta no
// cert bundle FULL e CMN — nenhuma board precisa de PEM custom.
struct AiProvider {
    const char* id;           // nome no opts.provider
    const char* url;
    const char* keyFile;
    const char* defaultModel;
    const char* referer;      // atribuicao opcional (OpenRouter): null = nao ha
    const char* title;
};
static const AiProvider AI_PROVIDERS[] = {
    {"deepseek", "https://api.deepseek.com/chat/completions",
     "/local/deepseek_key.txt", "deepseek-flash", nullptr, nullptr},
    {"openrouter", "https://openrouter.ai/api/v1/chat/completions",
     "/local/openrouter_key.txt", "qwen/qwen3.8-omni-flash",
     "https://os.celer.tec.br", "CelerOS"},
};
static const AiProvider& aiProviderByName(const char* name) {
    // null/undefined/deepseek = [0]; nome desconhecido tambem cai no [0]
    // (o chat valida e lanca antes de chegar aqui)
    for (const AiProvider& p : AI_PROVIDERS) {
        if (name == nullptr || strcmp(name, p.id) == 0) return p;
    }
    return AI_PROVIDERS[0];
}

// duk_error SEM varargs (regra do projeto: argumentos de conversao em
// lightfunc corrompem o heap) — mensagem pre-formatada com snprintf
static void aiThrow(duk_context* ctx, const char* fmt, const char* a) {
    char msg[110];
    snprintf(msg, sizeof(msg), fmt, a != nullptr ? a : "?");
    duk_error(ctx, DUK_ERR_ERROR, msg);
}

// Teto do corpo da resposta (mesmo sink malloc/realloc do Net.get: sem
// RAM a requisicao falha limpa em vez de abortar o aparelho). Com o
// default de max_tokens 1024 a resposta tipica fica em poucos KB.
#define AI_MAX_BODY 32768

// Completions demoradas passam com folga dos 10s do Net.post. O omni
// responde em 5-15 s na pratica; 60 s segura os casos lentos sem deixar
// uma requisicao orfa (app fechado no meio) travar o worker por muito tempo.
#define AI_TIMEOUT_MS 60000

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
    const AiProvider* prov = &AI_PROVIDERS[0];  // resolved no begin
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

static std::string aiReadKey(const AiProvider& p) {
    if (!FileSystem::exists(p.keyFile)) return std::string();
    return kstr::trim(FileSystem::readTextFile(p.keyFile));
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

// Um POST: le o pedido do slot (state==1) e publica o resultado (state==2).
// Corpo do worker persistente abaixo.
static void aiRunRequest(AiSlot* s) {
    AiBody got;
    bool ok;
    int status;
    char err[96];
    {
        HttpClient http;
        http.setTimeout(AI_TIMEOUT_MS);
        http.setBearerAuth(s->key ? s->key : "");
        if (s->prov->referer != nullptr) {  // atribuicao OpenRouter (opcional)
            http.setHeader("HTTP-Referer", s->prov->referer);
            http.setHeader("X-Title", s->prov->title);
        }
        http.setBodySink([&got](const char* d, size_t len) { return got.append(d, len); });
        HttpResponse resp = http.postJson(s->prov->url,
                                          std::string(s->payload ? s->payload : "", s->payloadLen));
        ok = resp.isOk();
        status = resp.statusCode;
        // %.95s: a mensagem pode vir maior que err[96] — corta em vez de
        // acionar o -Werror=format-truncation do GCC do IDF
        snprintf(err, sizeof(err), "%.95s", resp.success ? "" : resp.errorMessage.c_str());
    }  // TLS/cliente liberados antes de publicar o resultado
    // Pedido consumido: o worker e o unico que toca payload/key enquanto
    // state==1 (o begin so reescreve quando ele esta dormindo no semaforo)
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
}

// Worker PERSISTENTE (nao nasce/morre por pedido): a stack de 24KB vem da
// RAM INTERNA do xTaskCreate, e apos a 1a sessao TLS o heap interno do watch
// fragmenta a ponto de nao ter mais bloco de 24KB (bancada 2026-10-02: a
// 2a chamada devolvia "ocupado" para sempre). Nasce no 1o chat do boot e
// dorme num semaforo binario ate o proximo — custo zero entre pedidos.
static TaskHandle_t s_aiWorker = nullptr;
static SemaphoreHandle_t s_aiWork = nullptr;

static void aiWorker(void*) {
    for (;;) {
        xSemaphoreTake(s_aiWork, portMAX_DELAY);
        aiRunRequest(&s_aiSlot);
    }
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
    // Provider (opts.provider): "deepseek" e o default. Nome desconhecido
    // lanca na cara do app — errar a casa mandaria a chave pro lugar errado.
    duk_get_prop_string(ctx, 0, "provider");
    const char* provName = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : nullptr;
    duk_pop(ctx);
    bool provKnown = false;
    for (const AiProvider& p : AI_PROVIDERS) {
        if (provName == nullptr || strcmp(provName, p.id) == 0) { provKnown = true; break; }
    }
    if (!provKnown) {
        aiThrow(ctx, "AI: provider '%.40s' desconhecido", provName);
    }
    const AiProvider& prov = aiProviderByName(provName);
    std::string key = aiReadKey(prov);
    if (key.empty()) {
        aiThrow(ctx, "AI: chave ausente (%.60s)", prov.keyFile);
    }

    // Defaults do framework por cima do opts do app
    duk_dup(ctx, 0);
    duk_del_prop_string(ctx, -1, "provider");  // campo interno: nao viaja no JSON
    duk_push_boolean(ctx, 0);  // o runtime nao consome SSE
    duk_put_prop_string(ctx, -2, "stream");
    if (!duk_has_prop_string(ctx, -1, "model")) {
        duk_push_string(ctx, prov.defaultModel);
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
    // JSON codificado fica na pilha ate o malloc do slot: sem std::string
    // intermediaria (o audio base64 do Qwen chega a centenas de KB — eram
    // tres copias vivas no pico: string do duk, std::string e o malloc)
    duk_size_t encLen = 0;
    duk_json_encode(ctx, -1);
    const char* encoded = duk_get_lstring(ctx, -1, &encLen);

    AiSlot& s = s_aiSlot;
    if (!aiMuxTake(s)) { duk_pop(ctx); duk_push_boolean(ctx, 0); return 1; }
    if (s.state == 1) {  // requisicao em curso (ou zumbi cancelado)
        xSemaphoreGive(s.mux);
        duk_pop(ctx);
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
    s.payload = aiDupBuf(encoded ? encoded : "{}", encoded ? encLen : 2);
    free(s.key);
    s.key = aiDupBuf(key.data(), key.size());
    if (s.payload == nullptr || s.key == nullptr) {
        free(s.payload); s.payload = nullptr;
        free(s.key); s.key = nullptr;
        xSemaphoreGive(s.mux);
        duk_error(ctx, DUK_ERR_ERROR, "AI: sem RAM para montar o pedido");
    }
    s.payloadLen = encoded ? encLen : 2;
    duk_pop(ctx);  // JSON copiado para o slot: a string do heap JS pode ir
    s.prov = &prov;
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

    // Stack de 24KB: mesma do Net assincrono (JsNet) — o TLS do handshake
    // nao cabe em 12KB (bancada 2026-10-02). O worker nasce UMA vez e
    // dorme entre pedidos (ver aiWorker); a falha de nascimento so ocorre
    // no 1o chat do boot, com o heap interno ainda inteiro.
    if (s_aiWork == nullptr) s_aiWork = xSemaphoreCreateBinary();
    if (s_aiWork != nullptr && s_aiWorker == nullptr) {
        // Stack na PSRAM nas placas que tem (S3 do cao/watch: 8 MB livres) —
        // a RAM interna do boot do watch nao garante bloco de 24 KB (o
        // httpd + TinyUSB + NimBLE comem o heap: bancada 2026-10-02, o
        // xTaskCreate puro falhava em TODA criacao e o app so via
        // "ocupado"). Sem PSRAM cai no heap interno (caminho antigo).
        TaskHandle_t h = nullptr;
        BaseType_t okc = pdFAIL;
        if (Board::profile().hasPsram) {
            okc = xTaskCreateWithCaps(aiWorker, "jsai", 24576, nullptr, 3, &h,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (okc != pdPASS) {
            okc = xTaskCreate(aiWorker, "jsai", 24576, nullptr, 3, &h);
        }
        if (okc != pdPASS) {
            celer_log_println("[ai] chat -1: xTaskCreate falhou (RAM p/ stack de 24KB)");
        } else {
            s_aiWorker = h;
        }
    }
    if (s_aiWorker == nullptr || s_aiWork == nullptr) {
        // worker nao nasceu: devolve o slot e esquece o callback
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
    xSemaphoreGive(s_aiWork);  // acorda o worker (pedido ja esta no slot)
    duk_push_boolean(ctx, 1);
    return 1;
}

// AI.configured() = DeepSeek; AI.configured("openrouter") checa a casa certa.
// Nome desconhecido devolve false (nao lanca: e um detector, nao uma chamada)
duk_ret_t JSBindings::js_aiConfigured(duk_context *ctx) {
    const char* provName = duk_is_string(ctx, 0) ? duk_get_string(ctx, 0) : nullptr;
    bool known = false;
    for (const AiProvider& p : AI_PROVIDERS) {
        if (provName == nullptr || strcmp(provName, p.id) == 0) { known = true; break; }
    }
    duk_push_boolean(ctx, (known && !aiReadKey(aiProviderByName(provName)).empty()) ? 1 : 0);
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

// message.tool_calls -> array [{id,name,args}] no topo da pilha (API 20).
// `args` e o `function.arguments` decodificado quando JSON valido, senao a
// string crua — o app de ES5 nao teria como tratar falha de parse. Chamado
// com o message no topo; nao lanca (decode vem sob safe_call).
static void aiToolCallsFromMessage(duk_context* ctx) {
    duk_push_array(ctx);
    duk_uarridx_t nOut = 0;
    duk_get_prop_string(ctx, -2, "tool_calls");
    if (duk_is_array(ctx, -1)) {
        const duk_uarridx_t n = (duk_uarridx_t)duk_get_length(ctx, -1);
        for (duk_uarridx_t i = 0; i < n; i++) {
            duk_get_prop_index(ctx, -1, i);              // tc
            duk_push_object(ctx);                        // item
            duk_get_prop_string(ctx, -2, "id");
            if (duk_is_string(ctx, -1)) duk_put_prop_string(ctx, -2, "id");
            else duk_pop(ctx);
            duk_get_prop_string(ctx, -2, "function");
            if (duk_is_object(ctx, -1)) {
                duk_get_prop_string(ctx, -1, "name");
                if (duk_is_string(ctx, -1)) duk_put_prop_string(ctx, -3, "name");
                else duk_pop(ctx);
                duk_get_prop_string(ctx, -1, "arguments");
                if (duk_is_string(ctx, -1)) {
                    duk_dup(ctx, -1);                    // copia p/ o decode
                    if (duk_safe_call(ctx, aiDecodeJson, nullptr, 1, 1) == DUK_EXEC_SUCCESS &&
                        (duk_is_object(ctx, -1) || duk_is_array(ctx, -1))) {
                        duk_remove(ctx, -2);             // tira a string, fica o obj
                    } else {
                        duk_pop(ctx);                    // lixo do decode; fica a string
                    }
                }
                duk_put_prop_string(ctx, -3, "args");    // objeto ou string crua
                duk_pop(ctx);                            // function
            } else {
                duk_pop(ctx);
            }
            // pilha: [.. out, tool_calls, tc, item] — o item vai para o array
            // de SAIDA (-4) e so depois sai o tc. (Ate 2026-10 o pop tirava o
            // item e o tc era gravado de volta em tool_calls: r.toolCalls
            // chegava SEMPRE vazio com finishReason "tool_calls".)
            duk_put_prop_index(ctx, -4, nOut++);         // out[nOut] = item
            duk_pop(ctx);                                // tc
        }
    }
    duk_pop(ctx);  // tool_calls — array de saida fica no topo
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
        // Detalhe do erro da API (corpo {"error":{"message":...}}): chave
        // invalida, saldo, rate limit — sem isso o app so ve "HTTP 401"
        duk_push_lstring(ctx, r->body ? r->body : "", r->bodyLen);
        if (duk_safe_call(ctx, aiDecodeJson, nullptr, 1, 1) == DUK_EXEC_SUCCESS &&
            duk_is_object(ctx, -1)) {
            duk_get_prop_string(ctx, -1, "error");        // [res, parsed, err]
            if (duk_is_object(ctx, -1)) {
                duk_get_prop_string(ctx, -1, "message");  // +message
                if (duk_is_string(ctx, -1)) {
                    duk_substring(ctx, -1, 0, 120);
                    duk_put_prop_string(ctx, -4, "detail");  // res.detail
                } else {
                    duk_pop(ctx);
                }
                duk_pop(ctx);  // err
            } else {
                duk_pop(ctx);
            }
        }
        duk_pop(ctx);  // parsed (ou string de parse quebrado)
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
            // Function calling (API 20): res.finishReason (choices[0]) e
            // res.toolCalls (message) quando o modelo responde com chamada
            // de ferramenta em vez de texto. Stack: [res,parsed,choices,c0,msg]
            duk_get_prop_string(ctx, -2, "finish_reason");
            if (duk_is_string(ctx, -1)) {
                duk_put_prop_string(ctx, -6, "finishReason");
            } else {
                duk_pop(ctx);
            }
            aiToolCallsFromMessage(ctx);  // +arr
            duk_put_prop_string(ctx, -6, "toolCalls");
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
