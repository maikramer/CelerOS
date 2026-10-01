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
#include "JsFsJail.h"

// =====================================================
// FileSystem Bindings
// =====================================================

// Operacoes de CONTEUDO em arquivo do sistema sem a capability "system":
// erro legivel (o app ve a causa em vez de um null misterioso).
static void fsDeny(duk_context *ctx, const char* path) {
    duk_error(ctx, DUK_ERR_ERROR,
              "FS: %s e arquivo do sistema (requer permissao \"system\")", path);
}

duk_ret_t JSBindings::js_readTextFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    if (!fsPathAllowed(path)) fsDeny(ctx, path);
    std::string content = FileSystem::readTextFile(path);
    if (content.length() == 0 && !FileSystem::exists(path)) {
        duk_push_null(ctx);
    } else {
        duk_push_string(ctx, content.c_str());
    }
    return 1;
}

duk_ret_t JSBindings::js_writeTextFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    if (!fsPathAllowed(path)) fsDeny(ctx, path);
    const char *content = duk_require_string(ctx, 1);
    bool success = FileSystem::writeTextFile(path, content);
    duk_push_boolean(ctx, success);
    return 1;
}

duk_ret_t JSBindings::js_deleteFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    if (!fsPathAllowed(path)) fsDeny(ctx, path);
    bool success = FileSystem::deleteFile(path);
    duk_push_boolean(ctx, success);
    return 1;
}

duk_ret_t JSBindings::js_fileExists(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    bool exists = fsPathAllowed(path) && FileSystem::exists(path);
    duk_push_boolean(ctx, exists);
    return 1;
}

duk_ret_t JSBindings::js_listDir(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    std::vector<std::string> files(128);
    int count = FileSystem::listDir(path, files.data(), (int)files.size());
    
    duk_push_array(ctx);
    for (int i = 0; i < count; i++) {
        duk_push_string(ctx, files[i].c_str());
        duk_put_prop_index(ctx, -2, i);
    }
    return 1;
}

duk_ret_t JSBindings::js_appendTextFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    if (!fsPathAllowed(path)) fsDeny(ctx, path);
    const char *content = duk_require_string(ctx, 1);
    duk_push_boolean(ctx, FileSystem::appendTextFile(path, content));
    return 1;
}

duk_ret_t JSBindings::js_renameFile(duk_context *ctx) {
    const char *pathFrom = duk_require_string(ctx, 0);
    const char *pathTo = duk_require_string(ctx, 1);
    if (!fsPathAllowed(pathFrom)) fsDeny(ctx, pathFrom);
    if (!fsPathAllowed(pathTo)) fsDeny(ctx, pathTo);
    duk_push_boolean(ctx, FileSystem::renameFile(pathFrom, pathTo));
    return 1;
}

duk_ret_t JSBindings::js_mkdir(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, FileSystem::mkdir(path));
    return 1;
}

duk_ret_t JSBindings::js_rmdir(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, FileSystem::rmdir(path));
    return 1;
}

duk_ret_t JSBindings::js_isDirectory(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, fsPathAllowed(path) && FileSystem::isDirectory(path));
    return 1;
}

duk_ret_t JSBindings::js_isFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_boolean(ctx, fsPathAllowed(path) && FileSystem::isFile(path));
    return 1;
}

duk_ret_t JSBindings::js_getFileSize(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    duk_push_uint(ctx, fsPathAllowed(path) ? FileSystem::getFileSize(path) : 0);
    return 1;
}

duk_ret_t JSBindings::js_getTotalSpace(duk_context *ctx) {
    const char *drive = duk_require_string(ctx, 0);
    duk_push_number(ctx, (double)FileSystem::getTotalSpace(drive));  // > 4 GB no SD
    return 1;
}

duk_ret_t JSBindings::js_getUsedSpace(duk_context *ctx) {
    const char *drive = duk_require_string(ctx, 0);
    duk_push_number(ctx, (double)FileSystem::getUsedSpace(drive));  // > 4 GB no SD
    return 1;
}

duk_ret_t JSBindings::js_getFreeSpace(duk_context *ctx) {
    const char *drive = duk_require_string(ctx, 0);
    duk_push_number(ctx, (double)FileSystem::getFreeSpace(drive));  // > 4 GB no SD
    return 1;
}

duk_ret_t JSBindings::js_getFileMD5(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    const char *path = duk_require_string(ctx, 0);
    if (!fsPathAllowed(path)) fsDeny(ctx, path);
    duk_push_string(ctx, FileSystem::getFileMD5(path).c_str());
    return 1;
}

duk_ret_t JSBindings::js_mountSD(duk_context *ctx) {
    present();  // chamada bloqueante: o que o app desenhou aparece antes
    duk_push_boolean(ctx, FileSystem::mountSD());
    return 1;
}

duk_ret_t JSBindings::js_unmountSD(duk_context *ctx) {
    FileSystem::unmountSD();
    return 0;
}


// API 12: leitura BINARIA — cada byte do arquivo vira um char (0..255) da
// string devolvida (duk_push_lstring preserva NUL; o readTextFile antigo
// truncava no primeiro 0). maxLen opcional limita a leitura (default 16KB,
// teto 64KB: o corpo e copiado para o heap do Duktape).
duk_ret_t JSBindings::js_readFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    if (!fsPathAllowed(path)) fsDeny(ctx, path);
    size_t maxLen = 16 * 1024;
    if (duk_is_number(ctx, 1)) {
        maxLen = duk_require_uint(ctx, 1);
        if (maxLen > 64 * 1024) maxLen = 64 * 1024;
    }

    FILE* f = fopen(path, "rb");
    if (f == nullptr) { duk_push_null(ctx); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); duk_push_null(ctx); return 1; }
    if ((size_t)sz > maxLen) sz = maxLen;

    char* buf = (char*)malloc(sz > 0 ? sz : 1);
    if (buf == nullptr) { fclose(f); duk_push_null(ctx); return 1; }
    size_t got = fread(buf, 1, sz, f);
    fclose(f);
    duk_push_lstring(ctx, buf, got);
    free(buf);
    return 1;
}

// API 12: escrita BINARIA — os bytes da string (0..255 por char) vão
// crús para o arquivo (duk_require_lstring preserva NUL). Append com o
// FS.appendTextFile (que tambem é byte-safe p/ strings com NUL).
duk_ret_t JSBindings::js_writeFile(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    if (!fsPathAllowed(path)) fsDeny(ctx, path);
    size_t len = 0;
    const char* data = duk_require_lstring(ctx, 1, &len);
    if (len > 64 * 1024) {
        duk_error(ctx, DUK_ERR_RANGE_ERROR, "FS.writeFile: maximo 64KB");
    }
    FILE* f = fopen(path, "wb");
    if (f == nullptr) { duk_push_boolean(ctx, 0); return 1; }
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
}

duk_ret_t JSBindings::js_appData(duk_context *ctx) {
    // Pasta privada do app (F4): /local/data/<packageName>/ criada na
    // primeira chamada. Sem packageName no app.json devolve "".
    if (s_appPkg.empty()) { duk_push_string(ctx, ""); return 1; }
    std::string dir = "/local/data/" + s_appPkg;
    FileSystem::mkdir("/local/data");
    FileSystem::mkdir(dir.c_str());
    duk_push_string(ctx, (dir + "/").c_str());
    return 1;
}
