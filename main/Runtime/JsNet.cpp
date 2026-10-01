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
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_rom_md5.h"
#include "../Display/Backlight.h"
#include "../Display/Theme.h"
#include "../Display/Icon.h"
#include "../OTA/OtaManager.h"
#include "../Launcher/LauncherUI.h"
#include "../Launcher/Screens.h"
#include <lgfx/v1/misc/DataWrapper.hpp>
#include "JsInternal.h"

// =====================================================
// Network Bindings - HTTP (objeto Net, API level 2)
// =====================================================

// Custo de memoria: o TLS (https) pede ~45KB de heap durante a chamada,
// concorrendo com o heap do Duktape — respostas grandes podem estourar o
// ~90KB do runtime JS. Limitar payloads a dezenas de KB.
#define NET_MAX_BODY 32768

// Corpo acumulado em malloc/realloc (sink do HttpClient): sem RAM a
// requisicao falha limpa (null no script). Com std::string, o crescimento
// sem excecao abortava o aparelho no heap apertado da CYD (medido).
struct NetBody {
    char* p = nullptr;
    size_t n = 0, cap = 0;
    bool append(const char* d, size_t len) {
        if (n >= NET_MAX_BODY) return true;  // teto: descarta o excedente
        if (len > NET_MAX_BODY - n) len = NET_MAX_BODY - n;
        if (n + len > cap) {
            size_t want = cap * 2 > n + len ? cap * 2 : n + len;
            if (want < 1024) want = 1024;
            if (want > NET_MAX_BODY) want = NET_MAX_BODY;
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

struct NetPush {
    const NetBody* body;
};

static duk_ret_t netPushBody(duk_context *ctx, void *udata) {
    const NetBody* b = ((NetPush*)udata)->body;
    duk_push_lstring(ctx, b->p ? b->p : "", b->n);
    return 1;
}

// Executa GET/POST e EMPILHA o body (string). false = falhou (nada
// empilhado). Sem WiFi conectado: duk_error (o script ve um erro legivel em
// vez de um null silencioso).
static bool netFetch(duk_context *ctx, bool isPost) {
    JSBindings::present();  // "Carregando..." do app aparece durante a requisicao
    if (!WebManager::isWifiConnected()) {
        duk_error(ctx, DUK_ERR_ERROR, "Net: WiFi is not connected");
        return false;
    }
    const char *url = duk_require_string(ctx, 0);

    std::string body;
    std::string contentType = "text/plain";
    if (isPost) {
        body = duk_require_string(ctx, 1);
        if (duk_is_string(ctx, 2)) contentType = duk_get_string(ctx, 2);
    }

    // Componente Http (esp_http_client): https usa o cert bundle do sistema
    NetBody got;
    bool ok;
    {
        HttpClient http;
        http.setTimeout(10000);
        http.setBodySink([&got](const char* d, size_t len) { return got.append(d, len); });
        HttpResponse resp = isPost ? http.post(url, body, contentType) : http.get(url);
        ok = resp.isOk();
    }  // TLS/cliente liberados antes de copiar o corpo para o heap JS
    if (!ok) {
        free(got.p);
        return false;
    }
    // Copia para o heap JS protegida: sem RAM o Duktape lanca (longjmp) — o
    // buffer C e liberado antes de repassar o erro ao script
    NetPush args{&got};
    duk_int_t rc = duk_safe_call(ctx, netPushBody, &args, 0, 1);
    free(got.p);
    if (rc != DUK_EXEC_SUCCESS) duk_throw(ctx);
    return true;
}

duk_ret_t JSBindings::js_netGet(duk_context *ctx) {
    if (!netFetch(ctx, false)) duk_push_null(ctx);
    return 1;
}

duk_ret_t JSBindings::js_netGetJSON(duk_context *ctx) {
    if (!netFetch(ctx, false)) { duk_push_null(ctx); return 1; }
    duk_json_decode(ctx, -1);  // parse falho vira erro visivel no script
    return 1;
}

duk_ret_t JSBindings::js_netPost(duk_context *ctx) {
    if (!netFetch(ctx, true)) duk_push_null(ctx);
    return 1;
}

// API 6: download em streaming direto para arquivo — o corpo NAO passa pela
// heap do Duktape (chunk a chunk vai pro FILE*), entao nao sofre o teto de
// 32KB do Net.get. Uso: Net.download(url, path[, onProgress]) -> true|false;
// onProgress(bytes, total) por chunk (total = -1 se o server nao mandou
// Content-Length). O callback roda DENTRO do esp_http_client: duk_pcall para
// um erro de script nao estourar o longjmp no meio do download.
duk_ret_t JSBindings::js_netDownload(duk_context *ctx) {
    JSBindings::present();  // "Carregando..." do app aparece durante a requisicao
    if (!WebManager::isWifiConnected()) {
        duk_error(ctx, DUK_ERR_ERROR, "Net: WiFi is not connected");
    }
    const char *url = duk_require_string(ctx, 0);
    const char *path = duk_require_string(ctx, 1);
    const bool hasProgress = duk_is_function(ctx, 2);

    HttpClient http;
    http.setTimeout(15000);
    http.setBufferSize(4096);  // chunk maior = menos chamadas do callback
    // callback SEMPRE presente: o reset do watchdog por chunk cobre o
    // download sem progress (o loop do app nao roda enquanto isso)
    http.setProgressCallback([ctx, hasProgress](int64_t got, int64_t total) {
        esp_task_wdt_reset();
        if (!hasProgress) return;
        duk_dup(ctx, 2);  // funcao segue no stack (arg 2 da chamada)
        duk_push_number(ctx, (duk_double_t)got);
        duk_push_number(ctx, (duk_double_t)total);
        if (duk_pcall(ctx, 2) != DUK_EXEC_SUCCESS) duk_pop(ctx);
    });
    HttpResponse resp = http.downloadToFile(url, path);
    if (!resp.isOk()) { duk_push_false(ctx); return 1; }
    duk_push_true(ctx);
    return 1;
}

duk_ret_t JSBindings::js_netIsConnected(duk_context *ctx) {
    duk_push_boolean(ctx, WebManager::isWifiConnected());
    return 1;
}

// ---- Net assincrono (F3): handle + poll, no estilo keypadOpen/keypadPoll -
//
// Sem Promise no Duktape, a API de cooperacao e explicita:
//   var h = Net.beginGet(url);        // -1 se sem slot/WiFi
//   var r = Net.pollGet(h);           // null = rodando; {done,ok,status,
//                                     //  body,error} quando termina
//   Net.cancelGet(h);                 // abandona: o proximo pollGet concluido
//                                     // devolve {ok:false,error:"cancelado"}
//                                     // e libera o slot (a task zumbi roda
//                                     // ate o timeout dela, so que descartada)
// A requisicao roda em task propria: o app continua desenhando/respondendo
// enquanto o HTTP/TLS resolve. Pool de 2 slots; cada slot ocupa a task so
// durante a requisicao (timeout 10s).
//
// Concorrencia (S3 e dual-core): um mutex por slot e UM unico escritor —
// a task dona escreve resultado/estado sob lock; o beginGet so prepara o
// slot quando nenhuma task esta viva (state 0); o pollGet so transita
// 2->0. Antes o cancelGet liberava o slot com a task ainda escrevendo e o
// beginGet reusava o slot: duas tasks no mesmo std::string.
struct NetAsyncSlot {
    SemaphoreHandle_t mux = nullptr;  // criado no 1o uso (beginGet/reset)
    int state = 0;                    // 0=livre 1=task rodando 2=resultado pronto
    bool discard = false;             // cancelGet/reset: resultado sera descartado
    bool ok = false;
    int status = 0;
    std::string url;                  // imutavel enquanto state==1
    std::string body;                 // escritos SO pela task (transicao 1->2)
    std::string error;
};
static NetAsyncSlot s_netAsync[2];

static bool netMuxTake(NetAsyncSlot& s) {
    if (s.mux == nullptr) s.mux = xSemaphoreCreateMutex();
    return s.mux != nullptr && xSemaphoreTake(s.mux, portMAX_DELAY) == pdTRUE;
}

// Chamado no lancamento de cada app: zumbis do app anterior descartam,
// resultados nao consumidos sao liberados. O slot nunca atravessa apps.
void JSBindings::netAsyncReset() {
    for (NetAsyncSlot& s : s_netAsync) {
        if (!netMuxTake(s)) continue;
        s.discard = true;
        if (s.state == 2) {
            s.state = 0;
            s.body.clear();
            s.body.shrink_to_fit();
        }
        xSemaphoreGive(s.mux);
    }
}

static void netAsyncTask(void* raw) {
    NetAsyncSlot* s = (NetAsyncSlot*)raw;
    HttpClient http;
    http.setTimeout(10000);
    HttpResponse resp = http.get(s->url.c_str());
    if (resp.body.size() > NET_MAX_BODY) resp.body.resize(NET_MAX_BODY);
    if (netMuxTake(*s)) {
        if (s->discard) {
            s->ok = false;
            s->status = 0;
            s->body.clear();
            s->body.shrink_to_fit();
            s->error = "cancelado";
        } else {
            s->ok = resp.isOk();
            s->status = resp.statusCode;
            s->body = std::move(resp.body);
            s->error = resp.success ? "" : resp.errorMessage;
        }
        s->discard = false;
        s->state = 2;
        xSemaphoreGive(s->mux);
    }
    vTaskDelete(nullptr);
}

duk_ret_t JSBindings::js_netBeginGet(duk_context *ctx) {
    const char* url = duk_require_string(ctx, 0);
    if (!WebManager::isWifiConnected()) { duk_push_int(ctx, -1); return 1; }

    for (int i = 0; i < 2; i++) {
        NetAsyncSlot& s = s_netAsync[i];
        if (!netMuxTake(s)) continue;
        if (s.state == 1) {  // task viva (rodando ou zumbi cancelado)
            xSemaphoreGive(s.mux);
            continue;
        }
        // state 0 (livre) ou 2 (resultado nao consumido): reusa
        s.discard = false;
        s.ok = false;
        s.status = 0;
        s.body.clear();
        s.body.shrink_to_fit();
        s.error.clear();
        s.url = url;  // nenhuma task viva: seguro reescrever
        s.state = 1;
        xSemaphoreGive(s.mux);

        if (xTaskCreate(netAsyncTask, "jsnet", 12288, &s, 3, nullptr) != pdPASS) {
            // task nao nasceu: nada escrevera o slot, devolve ao estado livre
            netMuxTake(s);
            if (s.state == 1) s.state = 0;
            xSemaphoreGive(s.mux);
            break;
        }
        duk_push_int(ctx, i);
        return 1;
    }
    duk_push_int(ctx, -1);
    return 1;
}

duk_ret_t JSBindings::js_netPollGet(duk_context *ctx) {
    int h = duk_require_int(ctx, 0);
    if (h < 0 || h >= 2) { duk_push_null(ctx); return 1; }
    NetAsyncSlot& s = s_netAsync[h];
    if (!netMuxTake(s)) { duk_push_null(ctx); return 1; }
    if (s.state != 2) {
        xSemaphoreGive(s.mux);
        duk_push_null(ctx);
        return 1;  // ainda rodando
    }

    // Copia o resultado para fora do lock: montar o objeto no Duktape pode
    // alocar/lanca (longjmp) e o mutex nao pode vazar nesse caminho
    bool ok = s.ok;
    int status = s.status;
    size_t bodyLen = s.body.size();
    char* bodyBuf = (char*)malloc(bodyLen > 0 ? bodyLen : 1);
    if (bodyBuf != nullptr && bodyLen > 0) memcpy(bodyBuf, s.body.data(), bodyLen);
    char errBuf[64];
    snprintf(errBuf, sizeof(errBuf), "%s", s.error.c_str());
    s.state = 0;  // slot liberado para o proximo beginGet
    s.body.clear();
    s.body.shrink_to_fit();
    xSemaphoreGive(s.mux);

    duk_push_object(ctx);
    duk_push_boolean(ctx, 1);
    duk_put_prop_string(ctx, -2, "done");
    duk_push_boolean(ctx, ok ? 1 : 0);
    duk_put_prop_string(ctx, -2, "ok");
    duk_push_int(ctx, status);
    duk_put_prop_string(ctx, -2, "status");
    if (bodyBuf != nullptr) {
        duk_push_lstring(ctx, bodyBuf, bodyLen);  // corpo pode conter NUL
        free(bodyBuf);
    } else {
        duk_push_string(ctx, "");
    }
    duk_put_prop_string(ctx, -2, "body");
    duk_push_string(ctx, errBuf);
    duk_put_prop_string(ctx, -2, "error");
    return 1;
}

duk_ret_t JSBindings::js_netCancelGet(duk_context *ctx) {
    int h = duk_require_int(ctx, 0);
    if (h < 0 || h >= 2) return 0;
    // Nao mata a task (matar no meio do TLS vaza): marca como descartavel —
    // quando a task terminar (timeout 10s no pior caso) ela mesma grava o
    // resultado "cancelado" e libera o slot
    NetAsyncSlot& s = s_netAsync[h];
    if (netMuxTake(s)) {
        if (s.state == 1) s.discard = true;
        xSemaphoreGive(s.mux);
    }
    return 0;
}
