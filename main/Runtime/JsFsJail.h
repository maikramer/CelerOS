#pragma once

// Jail do FS para os bindings JS (F6). Header proprio para nao engordar o
// JsInternal.h: so JsFs.cpp e o nucleo (copy/remove) precisam disto.
#include <cstring>
#include "JsInternal.h"

// Arquivos do SISTEMA sob /local (credenciais, PIN, boot, OTA) so sao
// visiveis a apps com a capability "system". /sd e o resto de /local
// (incluindo o appData do proprio app) seguem abertos para quem tem "fs".
// Sensores de metadados (exists/isFile/...) tratam negado como "nao
// existe"; operacoes de conteudo lancam erro legivel (fsDeny no JsFs.cpp).
inline bool fsPathAllowed(const char* path) {
    if (perm(celer::PERM_SYSTEM)) return true;
    if (path == nullptr) return false;
    if (strncmp(path, "/local", 6) != 0) return true;  // /sd e outros
    static const char* const kProtected[] = {
        "/local/wifi.txt",
        "/local/settings_pin.txt",
        "/local/settings_pin2.bin",
        "/local/touch_cal_p.bin",
        "/local/autostart.txt",
        "/local/ota_url.txt",
        "/local/ota_allow_http.txt",
        "/local/config_time.txt",
        "/local/nowifi.txt",
        "/local/web_on.txt",
        "/local/brightness.txt",
        "/local/config_install_sd.txt",
    };
    for (const char* p : kProtected)
        if (strcmp(path, p) == 0) return false;
    return true;
}

// Mesma regra para operacoes de ARVORE: a raiz /local so pode ser
// copiada/removida por apps "system" (senao o app apagaria de uma vez
// credenciais, PIN e os outros apps).
inline bool fsTreeAllowed(const char* path) {
    if (perm(celer::PERM_SYSTEM)) return true;
    if (path == nullptr) return false;
    if (strcmp(path, "/local") == 0 || strcmp(path, "/local/") == 0) return false;
    return true;
}
