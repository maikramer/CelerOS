#include "JSBindings.h"
#include "JsInternal.h"
#include <Arduino.h>
#include <string>
#include "nvs.h"
#include "../Utils/AppPerms.h"

// =====================================================
// Storage (API 12): persistencia chave-valor PRIVADA do app
// =====================================================
//
// O localStorage dos apps: NVS namespace proprio por packageName (o
// System.setting e global e apps colidiam entre si; appData e arquivo
// solto). Valores sao strings (numeros/booleans sao serializados com
// duk_to_string — get devolve SEMPRE string, quem gravou numero converte
// de volta). Chave ate 15 chars (limite do NVS), valor ate 4 KB.
//
// App sem packageName (.js avulso) compartilha o namespace "app__anon" —
// documentado: script solto nao tem identidade.

namespace {

// namespace NVS: "app_" + packageName sanitizado, max 15 chars no total
std::string pkgNamespace(const std::string& pkg) {
    std::string ns = "app_";
    for (size_t i = 0; i < pkg.size() && ns.size() < 15; i++) {
        char c = pkg[i];
        ns += (c == '.' || c == '-' || c == '_') ? '_' :
              ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) ? c : '_';
    }
    if (ns.size() < 6) ns = "app__anon";  // sem packageName (script avulso)
    return ns;
}
std::string storageNamespace() { return pkgNamespace(s_appPkg); }

const char* requireKey(duk_context* ctx, duk_idx_t idx) {
    const char* k = duk_require_string(ctx, idx);
    size_t len = strlen(k);
    if (len == 0 || len > 15) {
        duk_error(ctx, DUK_ERR_RANGE_ERROR,
                  "Storage: chave deve ter 1 a 15 caracteres (limite do NVS)");
    }
    return k;
}

// Capacidade: 4 KB por valor (blob NVS). Grava e devolve sucesso.
// Valor vazio remove a chave (mesma semantica do CelerSettings).
bool nvsPut(const std::string& ns, const char* key, const char* data, size_t len) {
    nvs_handle_t h;
    if (nvs_open(ns.c_str(), NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok;
    if (len == 0) {
        nvs_erase_key(h, key);  // ausente nao e erro
        ok = true;
    } else {
        ok = nvs_set_blob(h, key, data, len) == ESP_OK;
    }
    ok = ok && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

}  // namespace

duk_ret_t JSBindings::js_storageGet(duk_context *ctx) {
    const char* key = requireKey(ctx, 0);
    nvs_handle_t h;
    std::string ns = storageNamespace();
    if (nvs_open(ns.c_str(), NVS_READONLY, &h) != ESP_OK) {
        // namespace novo: valor padrao (2o arg) ou string vazia
        duk_dup(ctx, 1);
        return 1;
    }
    size_t len = 0;
    if (nvs_get_blob(h, key, nullptr, &len) != ESP_OK) {
        nvs_close(h);
        duk_dup(ctx, 1);
        return 1;
    }
    char* buf = (char*)malloc(len);
    if (buf == nullptr) {
        nvs_close(h);
        duk_dup(ctx, 1);
        return 1;
    }
    nvs_get_blob(h, key, buf, &len);
    nvs_close(h);
    duk_push_lstring(ctx, buf, len);  // valor pode conter NUL (binario ok)
    free(buf);
    return 1;
}

duk_ret_t JSBindings::js_storageSet(duk_context *ctx) {
    const char* key = requireKey(ctx, 0);
    duk_to_string(ctx, 1);  // numero/boolean serializa; string segue crua
    const char* val = duk_require_string(ctx, 1);
    size_t len = strlen(val);
    if (len > 4096) {
        duk_error(ctx, DUK_ERR_RANGE_ERROR, "Storage: valor acima de 4KB");
    }
    duk_push_boolean(ctx, nvsPut(storageNamespace(), key, val, len) ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_storageRemove(duk_context *ctx) {
    const char* key = requireKey(ctx, 0);
    nvs_handle_t h;
    if (nvs_open(storageNamespace().c_str(), NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, key);  // chave ausente nao e erro
        nvs_commit(h);
        nvs_close(h);
    }
    return 0;
}

// Apaga TUDO do app (usado pelo proprio app e pela desinstalacao via loja)
duk_ret_t JSBindings::js_storageClear(duk_context *ctx) {
    nvs_handle_t h;
    bool ok = false;
    if (nvs_open(storageNamespace().c_str(), NVS_READWRITE, &h) == ESP_OK) {
        ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}

// clearFor(pkg): apaga o Storage de OUTRO app (desinstalacao pela loja).
// Capability "system": um app comum nao pode apagar o dado de terceiros.
duk_ret_t JSBindings::js_storageClearFor(duk_context *ctx) {
    if (!perm(celer::PERM_SYSTEM)) {
        duk_error(ctx, DUK_ERR_ERROR, "Storage.clearFor requer permissao \"system\"");
    }
    const char* pkg = duk_require_string(ctx, 0);
    nvs_handle_t h;
    bool ok = false;
    if (nvs_open(pkgNamespace(pkg).c_str(), NVS_READWRITE, &h) == ESP_OK) {
        ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}
