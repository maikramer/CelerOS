#include "KryonLink.h"
#include "USBDevice.h"
#include "KryonShell.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !defined(KRYONOS_VERSION)
#define KRYONOS_VERSION "?"
#endif
#if !defined(KRYONOS_API_LEVEL)
#define KRYONOS_API_LEVEL 0
#endif

namespace {

// ---------------------------------------------------------------- utilidades

void respond(uint8_t cmd, uint8_t status, const void* data = nullptr, uint16_t dataLen = 0) {
    uint8_t head[4] = {0x4B, cmd, (uint8_t)(dataLen + 1), (uint8_t)((dataLen + 1) >> 8)};
    USBDevice::linkWrite(head, sizeof(head));
    USBDevice::linkWrite(&status, 1);
    if (data != nullptr && dataLen > 0) {
        USBDevice::linkWrite((const uint8_t*)data, dataLen);
    }
}

void respondError(uint8_t cmd, const char* msg) {
    respond(cmd, 1, msg, (uint16_t)strlen(msg));
}

void le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// extrai string ate '\0'; avanca *p e decresce *len
bool takeString(const uint8_t*& p, uint16_t& len, char* out, size_t outMax) {
    size_t n = 0;
    while (len > 0 && *p != '\0') {
        if (n + 1 >= outMax) return false;
        out[n++] = (char)*p++;
        len--;
    }
    if (len == 0) return false;  // sem terminador
    p++;                         // consome '\0'
    len--;
    out[n] = '\0';
    return true;
}

bool takeU32(const uint8_t*& p, uint16_t& len, uint32_t& out) {
    if (len < 4) return false;
    out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    p += 4;
    len -= 4;
    return true;
}

const char* boardId() {
#if defined(KRYONOS_BOARD_CYD)
    return "cyd";
#elif defined(KRYONOS_BOARD_SMARTDISPLAY_4IN)
    return "smartdisplay_4in";
#else
    return "unknown";
#endif
}

// estado da escrita em curso (uma por vez, como o upload web)
FILE* s_wrFile = nullptr;
char s_wrPath[256] = {0};
uint32_t s_wrTotal = 0;

void closeWrite(bool keep) {
    if (s_wrFile != nullptr) {
        fclose(s_wrFile);
        s_wrFile = nullptr;
        if (!keep) remove(s_wrPath);  // nao deixa arquivo parcial
    }
    s_wrPath[0] = '\0';
    s_wrTotal = 0;
}

// cria diretorios pais do caminho (mesmo comportamento do upload web)
void makeParentDirs(const char* path) {
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char* slash = strchr(tmp + 1, '/'); slash != nullptr; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        if (!FileSystem::exists(tmp)) FileSystem::mkdir(tmp);
        *slash = '/';
    }
}

// PrintFn que acumula a saida do shell numa std::string (para o EXEC)
struct ExecCtx {
    std::string out;
};

void execPrint(void* ctx, const char* fmt, ...) {
    ExecCtx* e = (ExecCtx*)ctx;
    va_list args;
    va_start(args, fmt);
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
        if (e->out.size() + (size_t)n > 32768) return;  // trunca em 32KB
        e->out.append(buf, (size_t)n);
    }
}

// ------------------------------------------------------------------ handlers

void handleHello(const uint8_t* payload, uint16_t len) {
    char id[32];
    snprintf(id, sizeof(id), "KRYONOS %s|%s|api %d|proto 1", KRYONOS_VERSION, boardId(), KRYONOS_API_LEVEL);
    respond(KL_HELLO, 0, id, (uint16_t)strlen(id));
}

void handleInfo() {
    char ip[16] = "";
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool hasIp = false;
    if (netif != nullptr) {
        esp_netif_ip_info_t info;
        hasIp = esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0;
        if (hasIp) snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    }

    // espaco nos filesystems
    size_t lt = 0, lu = 0, st = 0, su = 0;
    bool hasSd = FileSystem::exists("/sd");
    if (FileSystem::exists("/local")) {
        lt = FileSystem::getTotalSpace("/local");
        lu = FileSystem::getUsedSpace("/local");
    }
    if (hasSd) {
        st = FileSystem::getTotalSpace("/sd");
        su = FileSystem::getUsedSpace("/sd");
    }

    char json[640];
    snprintf(json, sizeof(json),
             "{\"version\":\"%s\",\"board\":\"%s\",\"api\":%d,\"proto\":1,"
             "\"uptime_s\":%llu,\"heap_free\":%u,\"heap_min\":%u,"
             "\"ip\":\"%s\",\"sd\":%s,"
             "\"fs\":{\"/local\":{\"total\":%u,\"used\":%u},\"/sd\":{\"total\":%u,\"used\":%u}}}",
             KRYONOS_VERSION, boardId(), KRYONOS_API_LEVEL,
             (unsigned long long)(esp_timer_get_time() / 1000000ULL),
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
             hasIp ? ip : "", hasSd ? "true" : "false",
             (unsigned)lt, (unsigned)lu, (unsigned)st, (unsigned)su);
    respond(KL_INFO, 0, json, (uint16_t)strlen(json));
}

void handleLs(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !KryonShell::pathAllowed(path)) {
        respondError(KL_LS, "caminho invalido");
        return;
    }
    DIR* dir = opendir(path);
    if (dir == nullptr) {
        respondError(KL_LS, "nao e diretorio");
        return;
    }
    std::string out;
    uint16_t count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string full = std::string(path) + "/" + ent->d_name;
        struct stat st = {0};
        bool isDir = false;
        uint32_t size = 0, mtime = 0;
        if (stat(full.c_str(), &st) == 0) {
            isDir = S_ISDIR(st.st_mode);
            size = (uint32_t)st.st_size;
            mtime = (uint32_t)st.st_mtime;
        }
        uint8_t rec[10 + 256];
        rec[0] = isDir ? 1 : 0;
        le32(rec + 1, size);
        le32(rec + 5, mtime);
        size_t nameLen = strlen(ent->d_name);
        rec[9] = (uint8_t)nameLen;
        memcpy(rec + 10, ent->d_name, nameLen);
        out.append((const char*)rec, 10 + nameLen);
        count++;
    }
    closedir(dir);

    // resposta num frame unico: u8 status + u16 n + entradas
    size_t bodyLen = 3 + out.size();
    if (bodyLen > 0xFFFF) {
        respondError(KL_LS, "diretorio grande demais");
        return;
    }
    uint8_t frameHead[4] = {0x4B, KL_LS, (uint8_t)bodyLen, (uint8_t)(bodyLen >> 8)};
    uint8_t cnt[2];
    le16(cnt, count);
    USBDevice::linkWrite(frameHead, 4);
    uint8_t status = 0;
    USBDevice::linkWrite(&status, 1);
    USBDevice::linkWrite(cnt, 2);
    USBDevice::linkWrite((const uint8_t*)out.data(), (uint16_t)out.size());
}

void handleStat(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !KryonShell::pathAllowed(path)) {
        respondError(KL_STAT, "caminho invalido");
        return;
    }
    struct stat st = {0};
    if (stat(path, &st) != 0) {
        respond(KL_STAT, 0, "\x00", 1);  // exists=0
        return;
    }
    uint8_t rec[11];
    rec[0] = 1;                                   // exists
    rec[1] = S_ISDIR(st.st_mode) ? 1 : 0;         // isDir
    le32(rec + 2, (uint32_t)st.st_size);
    le32(rec + 6, (uint32_t)st.st_mtime);
    respond(KL_STAT, 0, rec, sizeof(rec));
}

void handleRead(const uint8_t* payload, uint16_t len) {
    char path[256];
    uint32_t offset = 0, want = 0;
    if (!takeString(payload, len, path, sizeof(path)) || !takeU32(payload, len, offset) ||
        !takeU32(payload, len, want) || !KryonShell::pathAllowed(path)) {
        respondError(KL_READ, "pedido malformado");
        return;
    }
    if (want > KryonLink::MAX_PAYLOAD - 1) want = KryonLink::MAX_PAYLOAD - 1;

    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        respondError(KL_READ, "nao abriu");
        return;
    }
    static uint8_t buf[KryonLink::MAX_PAYLOAD];
    size_t got = 0;
    if (fseek(f, (long)offset, SEEK_SET) == 0) {
        got = fread(buf + 1, 1, want, f);
    }
    fclose(f);
    buf[0] = 0;  // status OK; EOF e sinalizado por got < want
    respond(KL_READ, 0, buf, (uint16_t)(1 + got));
}

void handleWriteBegin(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !KryonShell::pathAllowed(path)) {
        respondError(KL_WRITE_BEGIN, "caminho invalido");
        return;
    }
    closeWrite(false);  // cancela escrita anterior por seguranca
    makeParentDirs(path);
    s_wrFile = fopen(path, "wb");
    if (s_wrFile == nullptr) {
        respondError(KL_WRITE_BEGIN, "nao abriu para escrita");
        return;
    }
    strncpy(s_wrPath, path, sizeof(s_wrPath) - 1);
    s_wrTotal = 0;
    respond(KL_WRITE_BEGIN, 0);
}

void handleWriteChunk(const uint8_t* payload, uint16_t len) {
    if (s_wrFile == nullptr) {
        respondError(KL_WRITE_CHUNK, "escrita nao iniciada");
        return;
    }
    if (len > 0 && fwrite(payload, 1, len, s_wrFile) != len) {
        closeWrite(false);
        respondError(KL_WRITE_CHUNK, "disco cheio/erro");
        return;
    }
    s_wrTotal += len;
    respond(KL_WRITE_CHUNK, 0);
}

void handleWriteEnd() {
    if (s_wrFile == nullptr) {
        respondError(KL_WRITE_END, "escrita nao iniciada");
        return;
    }
    uint32_t total = s_wrTotal;
    closeWrite(true);
    uint8_t rec[4];
    le32(rec, total);
    respond(KL_WRITE_END, 0, rec, sizeof(rec));
}

void handleDelete(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !KryonShell::pathAllowed(path)) {
        respondError(KL_DELETE, "caminho invalido");
        return;
    }
    respond(KL_DELETE, FileSystem::deleteFile(path) ? 0 : 1, nullptr, 0);
}

void handleMkdir(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !KryonShell::pathAllowed(path)) {
        respondError(KL_MKDIR, "caminho invalido");
        return;
    }
    makeParentDirs(path);
    respond(KL_MKDIR, FileSystem::mkdir(path) ? 0 : 1, nullptr, 0);
}

void handleRename(const uint8_t* payload, uint16_t len) {
    char from[256], to[256];
    if (!takeString(payload, len, from, sizeof(from)) || !takeString(payload, len, to, sizeof(to)) ||
        !KryonShell::pathAllowed(from) || !KryonShell::pathAllowed(to)) {
        respondError(KL_RENAME, "caminho invalido");
        return;
    }
    makeParentDirs(to);
    respond(KL_RENAME, FileSystem::renameFile(from, to) ? 0 : 1, nullptr, 0);
}

void handleExec(const uint8_t* payload, uint16_t len) {
    char line[256];
    if (len > sizeof(line) - 1) len = sizeof(line) - 1;
    memcpy(line, payload, len);
    line[len] = '\0';

    ExecCtx ctx;
    int code = KryonShell::execute(line, execPrint, &ctx);

    uint8_t rec[5];
    rec[0] = (uint8_t)code;
    uint32_t outLen = (uint32_t)ctx.out.size();
    le32(rec + 1, outLen);
    respond(KL_EXEC, 0, rec, sizeof(rec));
    if (outLen > 0) {
        // saida grande e enviada como frames de continuacao com o mesmo opcode
        size_t off = 0;
        while (off < ctx.out.size()) {
            size_t n = ctx.out.size() - off;
            if (n > KryonLink::MAX_PAYLOAD - 1) n = KryonLink::MAX_PAYLOAD - 1;
            respond(0x0E /* KL_EXEC_CONT */, 0, ctx.out.data() + off, (uint16_t)n);
            off += n;
        }
    }
}

void handleReboot() {
    uint8_t rec[4];
    le32(rec, 300);  // ms ate reiniciar
    respond(KL_REBOOT, 0, rec, sizeof(rec));
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

void dispatch(uint8_t cmd, const uint8_t* payload, uint16_t len) {
    switch (cmd) {
        case KL_HELLO: handleHello(payload, len); break;
        case KL_INFO: handleInfo(); break;
        case KL_LS: handleLs(payload, len); break;
        case KL_STAT: handleStat(payload, len); break;
        case KL_READ: handleRead(payload, len); break;
        case KL_WRITE_BEGIN: handleWriteBegin(payload, len); break;
        case KL_WRITE_CHUNK: handleWriteChunk(payload, len); break;
        case KL_WRITE_END: handleWriteEnd(); break;
        case KL_DELETE: handleDelete(payload, len); break;
        case KL_MKDIR: handleMkdir(payload, len); break;
        case KL_RENAME: handleRename(payload, len); break;
        case KL_EXEC: handleExec(payload, len); break;
        case KL_REBOOT: handleReboot(); break;
        default: respondError(cmd, "opcode desconhecido"); break;
    }
}

}  // namespace

void KryonLink::run(StreamBufferHandle_t rx) {
    static uint8_t payload[MAX_PAYLOAD];

    enum State { WANT_MAGIC, WANT_CMD, WANT_LEN, WANT_PAYLOAD } state = WANT_MAGIC;
    uint8_t cmd = 0;
    uint16_t need = 0, got = 0;

    for (;;) {
        uint8_t byte;
        if (xStreamBufferReceive(rx, &byte, 1, portMAX_DELAY) == 0) continue;

        switch (state) {
            case WANT_MAGIC:
                if (byte == 0x4B) state = WANT_CMD;
                break;
            case WANT_CMD:
                cmd = byte;
                state = WANT_LEN;
                break;
            case WANT_LEN:
                // len e little-endian: primeiro byte baixo
                if (got == 0) {
                    need = byte;
                    got = 1;
                } else {
                    need |= (uint16_t)byte << 8;
                    got = 0;
                    if (need == 0) {
                        dispatch(cmd, payload, 0);
                        state = WANT_MAGIC;
                    } else if (need <= MAX_PAYLOAD) {
                        state = WANT_PAYLOAD;
                    } else {
                        // frame grande demais: descarta e responde erro
                        respondError(cmd, "payload grande demais");
                        state = WANT_MAGIC;
                    }
                }
                break;
            case WANT_PAYLOAD:
                payload[got++] = byte;
                if (got >= need) {
                    dispatch(cmd, payload, need);
                    got = 0;
                    state = WANT_MAGIC;
                }
                break;
        }
    }
}
