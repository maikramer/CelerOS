#include "CelerLink.h"
#include "CelerShell.h"
#include "LogSink.h"
#include "FileSystem/FileSystem.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_idf_version.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Boards/Board.h"  // display (captura de tela) e id da placa
#include "../UI/Kui.h"     // TouchInjector (injecao de touch do celerctl)

#if !defined(CELEROS_VERSION)
#define CELEROS_VERSION "?"
#endif
#if !defined(CELEROS_API_LEVEL)
#define CELEROS_API_LEVEL 0
#endif

namespace {

// transporte injetado (default: CDC nativo do USBDevice)
CelerLink::WriteFn s_writer = nullptr;
CelerLink::BaudFn s_baudHook = nullptr;

// ---------------------------------------------------------------- utilidades

void respond(uint8_t cmd, uint8_t status, const void* data = nullptr, uint16_t dataLen = 0) {
    if (s_writer == nullptr) return;  // nenhum transporte instalado
    // frame inteiro numa unica escrita: logs concorrentes (logcat) nunca
    // intercalam bytes no meio de uma resposta
    static uint8_t frame[4 + 1 + CelerLink::MAX_PAYLOAD];
    uint16_t total = (uint16_t)(1 + dataLen);
    frame[0] = 0x43;
    frame[1] = cmd;
    frame[2] = (uint8_t)total;
    frame[3] = (uint8_t)(total >> 8);
    frame[4] = status;
    if (data != nullptr && dataLen > 0) {
        memcpy(frame + 5, data, dataLen);
    }
    s_writer(frame, (size_t)(4 + total));
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
    return Board::profile().id;
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
    char id[64];
    snprintf(id, sizeof(id), "CELEROS %s|%s|api %d|proto 1", CELEROS_VERSION, boardId(), CELEROS_API_LEVEL);
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
    unsigned long long lt = 0, lu = 0, st = 0, su = 0;
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
             "\"fs\":{\"/local\":{\"total\":%llu,\"used\":%llu},\"/sd\":{\"total\":%llu,\"used\":%llu}}}",
             CELEROS_VERSION, boardId(), CELEROS_API_LEVEL,
             (unsigned long long)(esp_timer_get_time() / 1000000ULL),
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
             hasIp ? ip : "", hasSd ? "true" : "false",
             lt, lu, st, su);
    respond(KL_INFO, 0, json, (uint16_t)strlen(json));
}

void handleLs(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !CelerShell::pathAllowed(path)) {
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
        struct stat st;
        memset(&st, 0, sizeof(st));
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
    size_t bodyLen = 2 + out.size();
    if (bodyLen + 5 > 0xFFFF) {
        respondError(KL_LS, "diretorio grande demais");
        return;
    }
    std::string frame;
    frame.resize(4 + 1 + bodyLen);
    frame[0] = 0x43;
    frame[1] = KL_LS;
    frame[2] = (uint8_t)(bodyLen + 1);
    frame[3] = (uint8_t)((bodyLen + 1) >> 8);
    frame[4] = 0;  // status OK
    frame[5] = (uint8_t)count;
    frame[6] = (uint8_t)(count >> 8);
    memcpy(frame.data() + 7, out.data(), out.size());
    if (s_writer != nullptr) s_writer((const uint8_t*)frame.data(), frame.size());
}

void handleStat(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !CelerShell::pathAllowed(path)) {
        respondError(KL_STAT, "caminho invalido");
        return;
    }
    struct stat st;
    memset(&st, 0, sizeof(st));
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
        !takeU32(payload, len, want) || !CelerShell::pathAllowed(path)) {
        respondError(KL_READ, "pedido malformado");
        return;
    }
    if (want > CelerLink::MAX_PAYLOAD) want = CelerLink::MAX_PAYLOAD;

    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        respondError(KL_READ, "nao abriu");
        return;
    }
    static uint8_t buf[CelerLink::MAX_PAYLOAD];
    size_t got = 0;
    if (fseek(f, (long)offset, SEEK_SET) == 0) {
        got = fread(buf, 1, want, f);
    }
    fclose(f);
    // respond() acrescenta o status; EOF e sinalizado por got < want
    respond(KL_READ, 0, buf, (uint16_t)got);
}

void handleWriteBegin(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !CelerShell::pathAllowed(path)) {
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
    snprintf(s_wrPath, sizeof(s_wrPath), "%s", path);
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
    if (!takeString(payload, len, path, sizeof(path)) || !CelerShell::pathAllowed(path)) {
        respondError(KL_DELETE, "caminho invalido");
        return;
    }
    respond(KL_DELETE, FileSystem::deleteFile(path) ? 0 : 1, nullptr, 0);
}

void handleMkdir(const uint8_t* payload, uint16_t len) {
    char path[256];
    if (!takeString(payload, len, path, sizeof(path)) || !CelerShell::pathAllowed(path)) {
        respondError(KL_MKDIR, "caminho invalido");
        return;
    }
    makeParentDirs(path);
    respond(KL_MKDIR, FileSystem::mkdir(path) ? 0 : 1, nullptr, 0);
}

void handleRename(const uint8_t* payload, uint16_t len) {
    char from[256], to[256];
    if (!takeString(payload, len, from, sizeof(from)) || !takeString(payload, len, to, sizeof(to)) ||
        !CelerShell::pathAllowed(from) || !CelerShell::pathAllowed(to)) {
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
    int code = CelerShell::execute(line, execPrint, &ctx);

    uint8_t rec[5];
    rec[0] = (uint8_t)code;
    uint32_t outLen = (uint32_t)ctx.out.size();
    le32(rec + 1, outLen);
    respond(KL_EXEC, 0, rec, sizeof(rec));
    if (outLen > 0) {
        // saida grande e enviada como frames de continuacao
        size_t off = 0;
        while (off < ctx.out.size()) {
            size_t n = ctx.out.size() - off;
            if (n > CelerLink::MAX_PAYLOAD - 1) n = CelerLink::MAX_PAYLOAD - 1;
            respond(KL_EXEC_CONT, 0, ctx.out.data() + off, (uint16_t)n);
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

void handleSetBaud(const uint8_t* payload, uint16_t len) {
    uint32_t baud = 0;
    if (!takeU32(payload, len, baud) || s_baudHook == nullptr) {
        respondError(KL_SET_BAUD, "troca de baud nao suportada neste canal");
        return;
    }
    uint8_t rec[4];
    le32(rec, baud);
    respond(KL_SET_BAUD, 0, rec, sizeof(rec));
    // da tempo do ACK sair da FIFO antes de mudar o baud
    vTaskDelay(pdMS_TO_TICKS(20));
    s_baudHook(baud);
}

// ------------------------------------------------------------------- logcat

void handleLogOn() {
    celer_logcat_set(true);
    respond(KL_LOG_ON, 0);
}

void handleLogOff() {
    celer_logcat_set(false);
    respond(KL_LOG_OFF, 0);
}

// ---------------------------------------------------------------------- OTA

esp_ota_handle_t s_ota = 0;
const esp_partition_t* s_otaPart = nullptr;
uint32_t s_otaWritten = 0;

void handleOtaBegin() {
    if (s_ota != 0) {
        esp_ota_abort(s_ota);
        s_ota = 0;
    }
    s_otaPart = esp_ota_get_next_update_partition(nullptr);
    if (s_otaPart == nullptr) {
        respondError(KL_OTA_BEGIN, "sem particao OTA disponivel");
        return;
    }
    esp_err_t err = esp_ota_begin(s_otaPart, OTA_SIZE_UNKNOWN, &s_ota);
    if (err != ESP_OK) {
        s_ota = 0;
        respondError(KL_OTA_BEGIN, esp_err_to_name(err));
        return;
    }
    s_otaWritten = 0;
    respond(KL_OTA_BEGIN, 0, s_otaPart->label, (uint16_t)strlen(s_otaPart->label));
}

void handleOtaChunk(const uint8_t* payload, uint16_t len) {
    if (s_ota == 0) {
        respondError(KL_OTA_CHUNK, "OTA nao iniciada");
        return;
    }
    if (len > 0 && esp_ota_write(s_ota, payload, len) != ESP_OK) {
        esp_ota_abort(s_ota);
        s_ota = 0;
        respondError(KL_OTA_CHUNK, "falha ao gravar particao");
        return;
    }
    s_otaWritten += len;
    respond(KL_OTA_CHUNK, 0);
}

void handleOtaEnd() {
    if (s_ota == 0) {
        respondError(KL_OTA_END, "OTA nao iniciada");
        return;
    }
    esp_err_t errEnd = esp_ota_end(s_ota);
    s_ota = 0;
    if (errEnd != ESP_OK) {
        respondError(KL_OTA_END, esp_err_to_name(errEnd));
        return;
    }
    if (esp_ota_set_boot_partition(s_otaPart) != ESP_OK) {
        respondError(KL_OTA_END, "falha ao marcar boot partition");
        return;
    }
    uint8_t rec[4];
    le32(rec, s_otaWritten);
    respond(KL_OTA_END, 0, rec, sizeof(rec));  // reboot fica por conta do host
}

void handleOtaAbort() {
    if (s_ota != 0) {
        esp_ota_abort(s_ota);
        s_ota = 0;
    }
    respond(KL_OTA_ABORT, 0);
}

// ---------------------------------------------------------------- screenshot

void handleScreenshot(const uint8_t* payload, uint16_t len) {
    // Pedido: u8 formato opcional (1 = RLE). Resposta: u16 w + u16 h + u8
    // formato; depois chunks KL_SCR_DATA. Cliente antigo nao manda payload e
    // recebe o cru de sempre (e ignora o byte extra do cabecalho).
    //   cru: pixels RGB565 LE
    //   RLE: pares {u16 contagem, u16 pixel} LE — a UI e quase so cor chapada:
    //        ~10x menos bytes (captura de 5 s a 921600 vira < 1 s, e a 115200
    //        deixa de levar 40 s)
    // Leitura linha a linha: sem buffer do tamanho da tela (a CYD, sem PSRAM,
    // nao conseguia capturar).
    const bool rle = len >= 1 && payload[0] == 1;
    CelerDisplay& tft = Board::display();
    const uint16_t w = (uint16_t)tft.width();
    const uint16_t h = (uint16_t)tft.height();
    uint16_t* row = (uint16_t*)malloc((size_t)w * 2);
    const size_t cap = (CelerLink::MAX_PAYLOAD - 1) & ~(size_t)3;  // multiplo de 4
    uint8_t* out = (uint8_t*)malloc(cap);
    if (row == nullptr || out == nullptr) {
        free(row);
        free(out);
        respondError(KL_SCREENSHOT, "sem memoria para captura");
        return;
    }
    uint8_t head[5] = {(uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)(h >> 8), (uint8_t)(rle ? 1 : 0)};
    respond(KL_SCREENSHOT, 0, head, sizeof(head));

    size_t used = 0;
    auto flush = [&]() {
        if (used > 0 && s_writer != nullptr) respond(KL_SCR_DATA, 0, out, (uint16_t)used);
        used = 0;
    };
    uint16_t runPx = 0, runLen = 0;
    auto emitRun = [&]() {
        if (runLen == 0) return;
        if (used + 4 > cap) flush();
        out[used++] = (uint8_t)runLen;
        out[used++] = (uint8_t)(runLen >> 8);
        out[used++] = (uint8_t)runPx;
        out[used++] = (uint8_t)(runPx >> 8);
        runLen = 0;
    };

    for (uint16_t y = 0; y < h && s_writer != nullptr; y++) {
        tft.readRect(0, y, w, 1, row);  // RGB565 (swapBytes: ordem nativa LE)
        if (!rle) {
            const uint8_t* src = (const uint8_t*)row;
            size_t rem = (size_t)w * 2;
            while (rem > 0) {
                size_t n = cap - used < rem ? cap - used : rem;
                memcpy(out + used, src, n);
                used += n;
                src += n;
                rem -= n;
                if (used == cap) flush();
            }
            continue;
        }
        for (uint16_t x = 0; x < w; x++) {
            if (runLen > 0 && row[x] == runPx && runLen < 0xFFFF) {
                runLen++;
            } else {
                emitRun();
                runPx = row[x];
                runLen = 1;
            }
        }
    }
    emitRun();
    flush();
    free(row);
    free(out);
}

// ------------------------------------------------------------- touch inject

void handleTouch(const uint8_t* payload, uint16_t len) {
    // u8 n + n entradas {u8 down, u16 x, u16 y, u16 delayMs}
    if (len < 1) {
        respondError(KL_TOUCH, "payload vazio");
        return;
    }
    uint8_t n = payload[0];
    if (n == 0 || (size_t)len < 1 + (size_t)n * 7) {
        respondError(KL_TOUCH, "payload malformado");
        return;
    }
    if (n > 16) n = 16;
    kui::TouchInjector::Sample s[16];
    for (int i = 0; i < n; i++) {
        const uint8_t* p = payload + 1 + i * 7;
        s[i].down = p[0] != 0;
        s[i].x = (uint16_t)(p[1] | (p[2] << 8));
        s[i].y = (uint16_t)(p[3] | (p[4] << 8));
        s[i].delayMs = (uint16_t)(p[5] | (p[6] << 8));
    }
    if (!kui::TouchInjector::push(s, n)) {
        respondError(KL_TOUCH, "fila de touch cheia");
        return;
    }
    respond(KL_TOUCH, 0);
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
        case KL_SET_BAUD: handleSetBaud(payload, len); break;
        case KL_LOG_ON: handleLogOn(); break;
        case KL_LOG_OFF: handleLogOff(); break;
        case KL_OTA_BEGIN: handleOtaBegin(); break;
        case KL_OTA_CHUNK: handleOtaChunk(payload, len); break;
        case KL_OTA_END: handleOtaEnd(); break;
        case KL_OTA_ABORT: handleOtaAbort(); break;
        case KL_SCREENSHOT: handleScreenshot(payload, len); break;
        case KL_TOUCH: handleTouch(payload, len); break;
        default: respondError(cmd, "opcode desconhecido"); break;
    }
}

}  // namespace

void CelerLink::setWriter(WriteFn fn) {
    s_writer = fn;
}

void CelerLink::setBaudHook(BaudFn fn) {
    s_baudHook = fn;
}

// Maquina de estados de frames alimentada byte a byte.
void CelerLink::feed(uint8_t byte) {
    static uint8_t payload[MAX_PAYLOAD];
    static enum { WANT_MAGIC, WANT_CMD, WANT_LEN_LO, WANT_LEN_HI, WANT_PAYLOAD } state = WANT_MAGIC;
    static uint8_t cmd = 0;
    static uint16_t need = 0, got = 0;

    switch (state) {
        case WANT_MAGIC:
            if (byte == 0x43) state = WANT_CMD;
            break;
        case WANT_CMD:
            cmd = byte;
            state = WANT_LEN_LO;
            break;
        case WANT_LEN_LO:
            need = byte;
            state = WANT_LEN_HI;
            break;
        case WANT_LEN_HI:
            need |= (uint16_t)byte << 8;
            got = 0;
            if (need == 0) {
                dispatch(cmd, payload, 0);
                state = WANT_MAGIC;
            } else if (need <= MAX_PAYLOAD) {
                state = WANT_PAYLOAD;
            } else {
                respondError(cmd, "payload grande demais");
                state = WANT_MAGIC;
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

void CelerLink::run(StreamBufferHandle_t rx) {
    for (;;) {
        uint8_t byte;
        if (xStreamBufferReceive(rx, &byte, 1, portMAX_DELAY) == 0) continue;
        feed(byte);
    }
}
