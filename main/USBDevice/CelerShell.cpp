#include "CelerShell.h"
#include "FileSystem/FileSystem.h"
#include "Launcher/LauncherUI.h"
#include "Boards/Board.h"
#include "Display/Theme.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !defined(CELEROS_VERSION)
#define CELEROS_VERSION "?"
#endif
#if !defined(CELEROS_API_LEVEL)
#define CELEROS_API_LEVEL 0
#endif

namespace {

const char* boardName() {
    return Board::profile().name;
}

std::string humanSize(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024 * 1024) snprintf(buf, sizeof(buf), "%.1fG", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024 * 1024) snprintf(buf, sizeof(buf), "%.1fM", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024) snprintf(buf, sizeof(buf), "%.1fK", bytes / 1024.0);
    else snprintf(buf, sizeof(buf), "%uB", (unsigned)bytes);
    return buf;
}

bool ipOf(char* out, size_t outLen) {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) return false;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) return false;
    snprintf(out, outLen, IPSTR, IP2STR(&ip.ip));
    return true;
}

// ------------------------------------------------------------------ comandos

int cmdHelp(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    print(ctx,
        "CelerOS shell " CELEROS_VERSION "\n"
        "comandos:\n"
        "  help            esta ajuda\n"
        "  ls [dir]        lista diretorio (default /)\n"
        "  cat <arq>       mostra conteudo\n"
        "  rm <arq>        apaga arquivo\n"
        "  mv <de> <para>  renomeia/move\n"
        "  mkdir <dir>     cria diretorio\n"
        "  df              espaco em /local e /sd\n"
        "  free            heap livre\n"
        "  ps              tarefas FreeRTOS\n"
        "  uptime          tempo ligado\n"
        "  info            versao/board/rede\n"
        "  reboot          reinicia o sistema\n"
        "  rescan          reler lista de apps do launcher\n"
        "  run <app>       abre um app (pasta, nome ou pacote)\n");
    return 0;
}

int cmdLs(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    std::string dir = (argc > 1) ? argv[1] : "/";
    if (!CelerShell::pathAllowed(dir) && dir != "/") {
        print(ctx, "ls: caminho invalido (use /local ou /sd)\n");
        return 1;
    }
    if (dir != "/" && !FileSystem::isDirectory(dir.c_str())) {
        print(ctx, "ls: %s nao e diretorio\n", dir.c_str());
        return 1;
    }

    FileEntry entries[32];
    int n = FileSystem::listDirectory(dir.c_str(), entries, 32);
    if (n < 0) {
        print(ctx, "ls: erro ao abrir %s\n", dir.c_str());
        return 1;
    }
    for (int i = 0; i < n; i++) {
        std::string full = entries[i].path;
        struct stat st;
        memset(&st, 0, sizeof(st));
        size_t size = 0;
        time_t mtime = 0;
        if (stat(full.c_str(), &st) == 0) {
            size = (size_t)st.st_size;
            mtime = st.st_mtime;
        }
        struct tm tminfo;
        memset(&tminfo, 0, sizeof(tminfo));
        char when[24] = "--------------------";
        if (mtime > 0 && localtime_r(&mtime, &tminfo) != nullptr) {
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tminfo);
        }
        print(ctx, "%c %10s  %s  %s\r\n", entries[i].isDir ? 'd' : '-',
              humanSize(size).c_str(), when, entries[i].name.c_str());
    }
    return 0;
}

int cmdCat(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    if (argc < 2) {
        print(ctx, "uso: cat <arquivo>\n");
        return 1;
    }
    std::string path = argv[1];
    if (!CelerShell::pathAllowed(path)) {
        print(ctx, "cat: caminho invalido (use /local ou /sd)\n");
        return 1;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        print(ctx, "cat: nao consegui abrir %s\n", path.c_str());
        return 1;
    }
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        print(ctx, "%.*s", (int)n, buf);
    }
    fclose(f);
    print(ctx, "\r\n");
    return 0;
}

int cmdRm(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    if (argc < 2) {
        print(ctx, "uso: rm <arquivo>\n");
        return 1;
    }
    if (!CelerShell::pathAllowed(argv[1])) {
        print(ctx, "rm: caminho invalido (use /local ou /sd)\n");
        return 1;
    }
    if (!FileSystem::deleteFile(argv[1])) {
        print(ctx, "rm: falhou em %s\n", argv[1]);
        return 1;
    }
    return 0;
}

int cmdMv(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    if (argc < 3) {
        print(ctx, "uso: mv <de> <para>\n");
        return 1;
    }
    if (!CelerShell::pathAllowed(argv[1]) || !CelerShell::pathAllowed(argv[2])) {
        print(ctx, "mv: caminho invalido (use /local ou /sd)\n");
        return 1;
    }
    if (!FileSystem::renameFile(argv[1], argv[2])) {
        print(ctx, "mv: falhou\n");
        return 1;
    }
    return 0;
}

int cmdMkdir(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    if (argc < 2) {
        print(ctx, "uso: mkdir <dir>\n");
        return 1;
    }
    if (!CelerShell::pathAllowed(argv[1])) {
        print(ctx, "mkdir: caminho invalido (use /local ou /sd)\n");
        return 1;
    }
    if (!FileSystem::mkdir(argv[1])) {
        print(ctx, "mkdir: falhou em %s\n", argv[1]);
        return 1;
    }
    return 0;
}

int cmdDf(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    struct {
        const char* path;
        const char* label;
    } drives[] = {
        {"/local", "littlefs"},
        {"/sd", "sdcard"},
    };
    for (auto& d : drives) {
        if (!FileSystem::exists(d.path)) {
            print(ctx, "%-8s %-9s (ausente)\r\n", d.path, d.label);
            continue;
        }
        uint64_t total = FileSystem::getTotalSpace(d.path);
        uint64_t used = FileSystem::getUsedSpace(d.path);
        print(ctx, "%-8s %-9s total %-7s usado %-7s livre %s\r\n", d.path, d.label,
              humanSize(total).c_str(), humanSize(used).c_str(), humanSize(total - used).c_str());
    }
    return 0;
}

int cmdFree(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    print(ctx,
        "heap livre     : %s\r\n"
        "heap minimo    : %s\r\n"
        "maior bloco 8b : %s\r\n"
        "PSRAM livre    : %s\r\n"
        "interna livre  : %s (min %s, maior bloco %s)\r\n",
        humanSize(esp_get_free_heap_size()).c_str(),
        humanSize(esp_get_minimum_free_heap_size()).c_str(),
        humanSize(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)).c_str(),
        humanSize(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)).c_str(),
        humanSize(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)).c_str(),
        humanSize(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)).c_str(),
        humanSize(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)).c_str());
    return 0;
}

int cmdPs(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    static char table[4096];
    vTaskList(table);
    print(ctx, "task            estado  prio  stack\r\n");
    print(ctx, "%s", table);
    return 0;
}

int cmdUptime(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    uint64_t us = esp_timer_get_time();
    uint32_t secs = (uint32_t)(us / 1000000ULL);
    print(ctx, "up %uh %um %us (%llu s)\r\n", secs / 3600, (secs / 60) % 60, secs % 60,
          (unsigned long long)(us / 1000000ULL));
    return 0;
}

int cmdInfo(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    char ip[16] = "";
    bool hasIp = ipOf(ip, sizeof(ip));
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char fsLocal[64] = "", fsSd[64] = "";
    if (FileSystem::exists("/local")) {
        snprintf(fsLocal, sizeof(fsLocal), "%s/%s livres", humanSize(FileSystem::getTotalSpace("/local")).c_str(),
                 humanSize(FileSystem::getFreeSpace("/local")).c_str());
    }
    if (FileSystem::exists("/sd")) {
        snprintf(fsSd, sizeof(fsSd), "%s/%s livres", humanSize(FileSystem::getTotalSpace("/sd")).c_str(),
                 humanSize(FileSystem::getFreeSpace("/sd")).c_str());
    }

    print(ctx,
        "CelerOS  : %s (API level %d)\r\n"
        "board    : %s\r\n"
        "SDK      : %s\r\n"
        "uptime   : %llus\r\n"
        "heap     : %s livres / min %s\r\n"
        "ip       : %s\r\n"
        "mac      : %02X:%02X:%02X:%02X:%02X:%02X\r\n"
        "/local   : %s\r\n"
        "/sd      : %s\r\n",
        CELEROS_VERSION, CELEROS_API_LEVEL, boardName(), esp_get_idf_version(),
        (unsigned long long)(esp_timer_get_time() / 1000000ULL),
        humanSize(esp_get_free_heap_size()).c_str(),
        humanSize(esp_get_minimum_free_heap_size()).c_str(),
        hasIp ? ip : "(desconectado)", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        fsLocal[0] ? fsLocal : "(ausente)", fsSd[0] ? fsSd : "(ausente)");
    return 0;
}

// Diagnostico de cor: desenha, le de volta e imprime o conteudo do fb
int cmdColorBars(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    CelerDisplay& tft = Board::display();
    char line[96];
    snprintf(line, sizeof(line), "depth=%u rot=%u w=%u h=%u\r\n",
             (unsigned)tft.getColorDepth(), (unsigned)tft.getRotation(),
             (unsigned)tft.width(), (unsigned)tft.height());
    print(ctx, line);

    const uint32_t bars[6] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0xFFFF00, 0x00FFFF};
    const uint16_t exp565[6] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0xFFE0, 0x07FF};
    const char* nm[6] = {"RED", "GRN", "BLU", "WHT", "YEL", "CYN"};
    int bw = tft.width() / 6;
    uint16_t got;
    for (int i = 0; i < 6; i++) {
        tft.fillRect(i * bw, 0, bw, tft.height(), bars[i]);
        tft.readRect(i * bw + bw / 2, tft.height() / 2, 1, 1, &got);
        snprintf(line, sizeof(line), "%s exp=%04X got=%04X\r\n", nm[i], exp565[i], got);
        print(ctx, line);
    }
    // tema: fill + readback direto
    const uint32_t theme[3] = {THEME_BG, THEME_CARD, THEME_ACCENT};
    const uint16_t themeExp[3] = {0x0882, 0x10E4, 0x269D};
    const char* tn[3] = {"BG  ", "CARD", "ACNT"};
    for (int i = 0; i < 3; i++) {
        tft.fillRect(0, 0, 100, 100, theme[i]);
        tft.readRect(50, 50, 1, 1, &got);
        snprintf(line, sizeof(line), "%s exp=%04X got=%04X\r\n", tn[i], themeExp[i], got);
        print(ctx, line);
    }
    // pushImage de 565 puro (vermelho) e readback
    uint16_t raw[4] = {0xF800, 0xF800, 0x07E0, 0x07E0};
    tft.pushImage(0, 0, 2, 2, raw);
    tft.readRect(0, 0, 1, 1, &got);
    snprintf(line, sizeof(line), "PUSH exp=F800 got=%04X\r\n", got);
    print(ctx, line);
    print(ctx, "feito\r\n");
    return 0;
}

int cmdRescan(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    (void) argc; (void) argv;
    LauncherUI::requestRescan();
    print(ctx, "launcher rescaneando apps\r\n");
    return 0;
}

int cmdRun(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    if (argc < 2) {
        print(ctx, "uso: run <pasta|nome|pacote do app>\r\n");
        return 1;
    }
    // junta argumentos (nomes com espaco: run App Store)
    std::string target = argv[1];
    for (int i = 2; i < argc; i++) target += std::string(" ") + argv[i];
    LauncherUI::requestRescan();  // app recem-instalado entra na lista
    LauncherUI::requestLaunch(target);
    print(ctx, "abrindo %s (volta ao launcher quando o app atual sair)\r\n", target.c_str());
    return 0;
}

int cmdReboot(int argc, char** argv, CelerShell::PrintFn print, void* ctx) {
    print(ctx, "reiniciando...\r\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

struct ShellCmd {
    const char* name;
    int (*fn)(int argc, char** argv, CelerShell::PrintFn print, void* ctx);
};

const ShellCmd kCommands[] = {
    {"help", cmdHelp},   {"ls", cmdLs},     {"cat", cmdCat},       {"rm", cmdRm},
    {"mv", cmdMv},       {"mkdir", cmdMkdir}, {"df", cmdDf},      {"free", cmdFree},
    {"ps", cmdPs},       {"uptime", cmdUptime}, {"info", cmdInfo}, {"reboot", cmdReboot},
    {"rescan", cmdRescan}, {"run", cmdRun},
    {"colorbars", cmdColorBars},
};

}  // namespace

bool CelerShell::pathAllowed(const std::string& path) {
    return path.rfind("/local", 0) == 0 || path.rfind("/sd", 0) == 0;
}

bool LineEditor::feed(uint8_t byte, char* line, size_t maxLen) {
    if (byte == '\r' || byte == '\n') {
        size_t n = (m_len < maxLen - 1) ? m_len : maxLen - 1;
        memcpy(line, m_buf, n);
        line[n] = '\0';
        m_len = 0;
        return true;
    }
    if (byte == 0x08 || byte == 0x7F) {  // backspace
        if (m_len > 0) {
            m_len--;
            m_echo(m_ctx, "\b \b");
        }
    } else if (byte >= 0x20 && byte < 0x7F && m_len < sizeof(m_buf) - 1) {
        m_buf[m_len++] = (char)byte;
        char echo = (char)byte;
        m_echo(m_ctx, "%c", echo);
    }
    return false;
}

int CelerShell::execute(const char* line, PrintFn print, void* ctx) {
    // copia para tokenizar
    char buf[MAX_LINE];
    strncpy(buf, line, MAX_LINE - 1);
    buf[MAX_LINE - 1] = '\0';

    // separa em argc/argv por espacos
    char* argv[16];
    int argc = 0;
    char* save = nullptr;
    for (char* tok = strtok_r(buf, " \t", &save); tok != nullptr && argc < 16;
         tok = strtok_r(nullptr, " \t", &save)) {
        argv[argc++] = tok;
    }
    if (argc == 0) return 0;

    for (const ShellCmd& c : kCommands) {
        if (strcmp(argv[0], c.name) == 0) {
            return c.fn(argc, argv, print, ctx);
        }
    }
    print(ctx, "%s: comando nao encontrado (help para lista)\r\n", argv[0]);
    return -1;
}
