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

// Executa GET/POST e devolve o body em "out". Sem WiFi conectado: duk_error
// (o script ve um erro legivel em vez de um null silencioso).
static bool netFetch(duk_context *ctx, bool isPost, std::string &out) {
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
    HttpClient http;
    http.setTimeout(10000);
    HttpResponse resp;
    if (isPost) {
        resp = http.post(url, body, contentType);
    } else {
        resp = http.get(url);
    }
    if (!resp.isOk()) return false;
    out = resp.body;
    if (out.length() > NET_MAX_BODY) out.resize(NET_MAX_BODY);
    return true;
}

duk_ret_t JSBindings::js_netGet(duk_context *ctx) {
    std::string body;
    if (!netFetch(ctx, false, body)) { duk_push_null(ctx); return 1; }
    duk_push_string(ctx, body.c_str());
    return 1;
}

duk_ret_t JSBindings::js_netGetJSON(duk_context *ctx) {
    std::string body;
    if (!netFetch(ctx, false, body)) { duk_push_null(ctx); return 1; }
    duk_push_string(ctx, body.c_str());
    duk_json_decode(ctx, -1);  // parse falho vira erro visivel no script
    return 1;
}

duk_ret_t JSBindings::js_netPost(duk_context *ctx) {
    std::string body;
    if (!netFetch(ctx, true, body)) { duk_push_null(ctx); return 1; }
    duk_push_string(ctx, body.c_str());
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
//   Net.cancelGet(h);                 // abandona (slot volta sozinho no fim)
// A requisicao roda em task propria: o app continua desenhando/respondendo
// enquanto o HTTP/TLS resolve. Pool de 2 slots; cada slot ocupa a task so
// durante a requisicao (timeout 10s).
struct NetAsyncSlot {
    volatile bool busy = false;   // slot em uso (requisicao ou zumbi)
    volatile bool done = false;   // resultado pronto para o poll
    volatile bool ok = false;
    int status = 0;
    std::string url;
    std::string body;
    std::string error;
};
static NetAsyncSlot s_netAsync[2];

static void netAsyncTask(void* raw) {
    NetAsyncSlot* s = (NetAsyncSlot*)raw;
    HttpClient http;
    http.setTimeout(10000);
    HttpResponse resp = http.get(s->url.c_str());
    s->ok = resp.isOk();
    s->status = resp.statusCode;
    if (resp.body.size() > NET_MAX_BODY) resp.body.resize(NET_MAX_BODY);
    s->body = std::move(resp.body);
    s->error = resp.success ? "" : resp.errorMessage;
    s->done = true;
    vTaskDelete(nullptr);
}

duk_ret_t JSBindings::js_netBeginGet(duk_context *ctx) {
    const char* url = duk_require_string(ctx, 0);
    if (!WebManager::isWifiConnected()) { duk_push_int(ctx, -1); return 1; }

    for (int i = 0; i < 2; i++) {
        NetAsyncSlot& s = s_netAsync[i];
        if (s.busy && !s.done) continue;  // ainda rodando (ou zumbi cancelado)
        if (s.busy && s.done) {           // resultado nao consumido: descarta
            s.body.clear();
            s.body.shrink_to_fit();
        }
        s.busy = true;
        s.done = false;
        s.ok = false;
        s.status = 0;
        s.url = url;
        s.error.clear();

        if (xTaskCreate(netAsyncTask, "jsnet", 12288, &s, 3, nullptr) != pdPASS) {
            s.busy = false;
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
    if (h < 0 || h >= 2 || !s_netAsync[h].busy) { duk_push_null(ctx); return 1; }
    NetAsyncSlot& s = s_netAsync[h];
    if (!s.done) { duk_push_null(ctx); return 1; }  // ainda rodando

    duk_push_object(ctx);
    duk_push_boolean(ctx, 1);
    duk_put_prop_string(ctx, -2, "done");
    duk_push_boolean(ctx, s.ok ? 1 : 0);
    duk_put_prop_string(ctx, -2, "ok");
    duk_push_int(ctx, s.status);
    duk_put_prop_string(ctx, -2, "status");
    duk_push_string(ctx, s.body.c_str());
    duk_put_prop_string(ctx, -2, "body");
    duk_push_string(ctx, s.error.c_str());
    duk_put_prop_string(ctx, -2, "error");

    s.busy = false;  // slot liberado; corpo nao e mais valido apos o return
    s.body.clear();
    s.body.shrink_to_fit();
    return 1;
}

duk_ret_t JSBindings::js_netCancelGet(duk_context *ctx) {
    int h = duk_require_int(ctx, 0);
    if (h < 0 || h >= 2) return 0;
    // Nao mata a task (matar no meio do TLS vaza): marca como descartavel —
    // o slot fica ocupado ate a task terminar sozinha (timeout 10s)
    s_netAsync[h].done = true;
    return 0;
}
