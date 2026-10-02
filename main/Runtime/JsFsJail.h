#pragma once

// Jail do FS para os bindings JS (F6). Header proprio para nao engordar o
// JsInternal.h: so JsFs.cpp, JsNet.cpp e o nucleo (copy/remove) precisam disto.
#include <cstring>
#include "JsInternal.h"

// Caminho CANONICO dentro de um ponto de montagem: comeca com o segmento
// inteiro "/local" ou "/sd" e nao tem segmento vazio ("//"), "." nem "..".
// O LittleFS resolve "/local/./wifi.txt", "/local//wifi.txt" e
// "/local/data/../wifi.txt" para o mesmo arquivo que "/local/wifi.txt":
// a comparacao exata da lista protegida nao pegava essas formas, e
// "/local/." passava como raiz no removeDirectory/copyDirectory.
// Uma unica barra final e tolerada ("/sd/apps/").
inline bool fsPathCanonical(const char* path) {
    if (path == nullptr) return false;
    const char* rest;
    if (strncmp(path, "/local", 6) == 0) rest = path + 6;
    else if (strncmp(path, "/sd", 3) == 0) rest = path + 3;
    else return false;
    if (*rest != '\0' && *rest != '/') return false;  // "/localfoo", "/sdcard"
    while (*rest == '/') {
        const char* seg = rest + 1;
        const char* end = strchr(seg, '/');
        size_t n = end ? (size_t)(end - seg) : strlen(seg);
        if (n == 0) {
            if (end == nullptr) return true;  // barra final unica
            return false;                     // "//"
        }
        if ((n == 1 && seg[0] == '.') || (n == 2 && seg[0] == '.' && seg[1] == '.')) return false;
        if (end == nullptr) return true;
        rest = end;
    }
    return *rest == '\0';
}

// Raiz de um ponto de montagem ("/local", "/local/", "/sd", "/sd/")
inline bool fsIsMountRoot(const char* path, const char* mount) {
    size_t n = strlen(mount);
    return strncmp(path, mount, n) == 0 && (path[n] == '\0' || (path[n] == '/' && path[n + 1] == '\0'));
}

// Arquivos do SISTEMA sob /local (credenciais, PIN, boot, OTA) so sao
// visiveis a apps com a capability "system". /sd e o resto de /local
// (incluindo o appData do proprio app) seguem abertos para quem tem "fs".
// Sensores de metadados (exists/isFile/...) tratam negado como "nao
// existe"; operacoes de conteudo lancam erro legivel (fsDeny no JsFs.cpp).
// Caminho nao canonico e negado para TODOS (inclusive "system"): nao ha uso
// legitimo e o fopen cru do readFile/writeFile alcancaria o VFS inteiro.
inline bool fsPathAllowed(const char* path) {
    if (!fsPathCanonical(path)) return false;
    if (perm(celer::PERM_SYSTEM)) return true;
    if (strncmp(path, "/local/", 7) != 0) return true;  // /sd
    static const char* const kProtected[] = {
        "/local/wifi.txt",
        "/local/wifi.txt.migrated",
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
        "/local/notifications.txt",
    };
    for (const char* p : kProtected) {
        // tambem o ".tmp" da escrita atomica (writeTextFile escreve ao lado
        // e renomeia por cima: o app nao pode plantar o tmp)
        size_t n = strlen(p);
        if (strncmp(path, p, n) == 0 && (path[n] == '\0' || strcmp(path + n, ".tmp") == 0)) return false;
    }
    return true;
}

// Mesma regra para operacoes de ARVORE (origem E destino): a raiz /local
// so pode ser copiada/removida/sobrescrita por apps "system" (senao o app
// apagaria de uma vez credenciais, PIN e os outros apps — ou copiaria uma
// arvore por cima dos arquivos protegidos, que vivem na raiz).
inline bool fsTreeAllowed(const char* path) {
    if (!fsPathCanonical(path)) return false;
    if (perm(celer::PERM_SYSTEM)) return true;
    return !fsIsMountRoot(path, "/local");
}
