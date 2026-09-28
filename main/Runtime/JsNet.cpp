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
    if (hasProgress) {
        http.setProgressCallback([ctx](int64_t got, int64_t total) {
            duk_dup(ctx, 2);  // funcao segue no stack (arg 2 da chamada)
            duk_push_number(ctx, (duk_double_t)got);
            duk_push_number(ctx, (duk_double_t)total);
            if (duk_pcall(ctx, 2) != DUK_EXEC_SUCCESS) duk_pop(ctx);
        });
    }
    HttpResponse resp = http.downloadToFile(url, path);
    if (!resp.isOk()) { duk_push_false(ctx); return 1; }
    duk_push_true(ctx);
    return 1;
}

duk_ret_t JSBindings::js_netIsConnected(duk_context *ctx) {
    duk_push_boolean(ctx, WebManager::isWifiConnected());
    return 1;
}

