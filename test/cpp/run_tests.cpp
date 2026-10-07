// Testes host da lógica C++ pura do firmware (F5) — rodam no CI e local:
//   g++ -std=c++17 -Wall -Wextra -o celeros_tests test/cpp/run_tests.cpp && ./celeros_tests
// Sem framework: asserts com contagem (falha = exit 1). O alvo aqui é o
// código header-only extraído para main/Utils (SemVer, AppPerms) — a barra
// é subir a cada PR o que antes só era validado no device.

#include "../../main/Utils/SemVer.h"
#include "../../main/Utils/AppPerms.h"
#include "../../main/Utils/JsStrip.h"
#include "../../main/USBDevice/HostFrame.h"
#include "../../main/Utils/AlarmCalc.h"
#include "../../main/Utils/GbProto.h"
#include "../../main/Kernel/DeviceStats.h"
#include "../../main/Hardware/MusicEngine.h"
#include "../../main/Bluetooth/NetFrame.h"

// Jail do FS dos apps JS: o teste faz o papel do runtime (perm/s_appPkg)
#include <cstdint>
#include <dirent.h>
#include <cstdio>
#include <string>
static uint32_t s_perms = 0;
static std::string s_appPkg;
inline bool perm(uint32_t bit) { return (s_perms & bit) != 0; }
#define CELER_HOST_TEST 1
#include "../../main/Runtime/JsFsJail.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failed = 0, g_total = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_total++;                                                         \
        if (!(cond)) {                                                     \
            g_failed++;                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

using namespace celer;

static void testSemVer() {
    CHECK(versionGreater("1.2.1", "1.2.0"));
    CHECK(versionGreater("1.3.0", "1.2.9"));
    CHECK(versionGreater("2.0.0", "1.99.99"));
    CHECK(!versionGreater("1.2.0", "1.2.0"));
    CHECK(!versionGreater("1.2.0", "1.2.1"));
    CHECK(!versionGreater("1.2.9", "1.3.0"));
    // formas incompletas (firmware antigo publicava x.y)
    CHECK(versionGreater("1.3", "1.2.5"));
    CHECK(!versionGreater("1.2", "1.2.0"));
    CHECK(versionGreater("0.9.1", "0.9"));
    // sujeira nao numerica: atoi para em letra (mesma semantica do device)
    CHECK(!versionGreater("abc", "0.0.1"));
}

static void testPermissions() {
    CHECK(parsePermissions("{\"name\":\"x\"}") == PERM_ALL);                       // ausente
    CHECK(parsePermissions("{\"permissions\":[\"fs\",\"net\"]}") == (PERM_FS | PERM_NET));
    CHECK(parsePermissions("{\"permissions\":[\"gpio\"]}") == PERM_GPIO);
    CHECK(parsePermissions("{\"permissions\":[\"system\"]}") == PERM_SYSTEM);
    CHECK(parsePermissions("{\"permissions\":[\"mic\"]}") == PERM_MIC);             // API 19
    CHECK(parsePermissions("{\"permissions\":[\"fs\",\"gpio\",\"system\",\"net\",\"mic\"]}") ==
          (PERM_FS | PERM_NET | PERM_GPIO | PERM_SYSTEM | PERM_MIC));
    CHECK(parsePermissions("{\"permissions\":[]}") == 0);                         // vazio = nenhuma
    CHECK(parsePermissions("{\"permissions\":[}") == PERM_ALL);                   // invalido = nao tranca
    // campo vizinho nao confunde (ordem no JSON nao importa)
    CHECK(parsePermissions("{\"api\":6,\"permissions\":[\"fs\"],\"topbar\":true}") == PERM_FS);
    // token desconhecido nao concede nada
    CHECK((parsePermissions("{\"permissions\":[\"fs\",\"netfs\"]}") & (PERM_NET | PERM_GPIO | PERM_SYSTEM)) == 0);
    CHECK(parsePermissions("{\"permissions\":[\"camera\"]}") == 0);
}

// Enxuga em chunks de 3 bytes (estado atravessa a fronteira dos pedacos)
static std::string strip(const char* src) {
    size_t n = strlen(src);
    JsStripper count;
    for (size_t i = 0; i < n; i += 3) count.feed(src + i, n - i < 3 ? n - i : 3);
    std::string out(count.finish(), '\0');
    JsStripper fill(&out[0]);
    fill.feed(src, n);
    fill.finish();
    return out;
}

// In-place (CelerKernel com PSRAM: le o fonte uma vez e enxuga no proprio
// buffer) tem que dar o mesmo resultado das duas passadas
static std::string stripInPlace(const std::string& src) {
    std::string buf = src;
    JsStripper fill(&buf[0]);
    fill.feed(buf.data(), buf.size());
    buf.resize(fill.finish());
    return buf;
}

static void testJsStripInPlaceApps() {
    // todo main.js de fabrica e da loja (rodando da raiz do repo, como no CI)
    const char* roots[] = {"data/apps", "hub_apps"};
    int files = 0;
    for (const char* root : roots) {
        DIR* d = opendir(root);
        if (d == nullptr) continue;
        while (struct dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string path = std::string(root) + "/" + e->d_name + "/main.js";
            FILE* f = fopen(path.c_str(), "rb");
            if (f == nullptr) continue;
            std::string src;
            char chunk[4096];
            size_t n;
            while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) src.append(chunk, n);
            fclose(f);
            if (stripInPlace(src) != strip(src.c_str())) {
                printf("FALHA in-place: %s\n", path.c_str());
                CHECK(false);
            }
            files++;
        }
        closedir(d);
    }
    CHECK(files > 10);
}

static void testJsStrip() {
    CHECK(stripInPlace("a = b / c; r = /x\\/y/g; /* k */ d") == strip("a = b / c; r = /x\\/y/g; /* k */ d"));
    CHECK(stripInPlace("x=1/**/2;// fim") == strip("x=1/**/2;// fim"));
    CHECK(strip("var a = 1; // c\nvar b = 2;") == "var a = 1;\nvar b = 2;");
    CHECK(strip("    if (x) {\n\t\ty();   \n    }\n") == "if (x) {\ny();\n}\n");
    CHECK(strip("a/**/b") == "a b");
    CHECK(strip("a /* x\n y */ b") == "a\nb");                  // linhas preservadas
    CHECK(strip("s = \"// nao\"; t = '/* nao */';") == "s = \"// nao\"; t = '/* nao */';");
    CHECK(strip("r = /\\/\\//g; // c") == "r = /\\/\\//g;");  // regex com barras escapadas
    CHECK(strip("r = /[/]/.test(x)") == "r = /[/]/.test(x)");      // '/' dentro de classe
    CHECK(strip("return /a\\/b/.test(s)") == "return /a\\/b/.test(s)");
    CHECK(strip("x = a / b / c") == "x = a / b / c");             // divisao
    CHECK(strip("y = (a)/2; z = arr[0]/2") == "y = (a)/2; z = arr[0]/2");
    CHECK(strip("s = 'it\\'s // ok'") == "s = 'it\\'s // ok'");
    CHECK(strip("a\n\n\nb") == "a\n\n\nb");                    // linhas vazias ficam
}

// ------------------------------------------------------------------ HostFrame

struct RxFrame {
    uint8_t cmd = 0;
    std::vector<uint8_t> payload;
    int count = 0;
};

static void onFrame(void* ctx, uint8_t cmd, const uint8_t* payload, uint16_t len) {
    RxFrame* rx = (RxFrame*)ctx;
    rx->cmd = cmd;
    rx->payload.assign(payload, payload + len);
    rx->count++;
}

// alimenta o parser com um buffer, na cadencia de relogio dada
static RxFrame parseAll(hostframe::FrameParser& p, const uint8_t* d, size_t n, int64_t now = 0) {
    RxFrame rx;
    for (size_t i = 0; i < n; i++) p.feed(d[i], now, onFrame, &rx);
    return rx;
}

static void testHostFrame() {
    using namespace hostframe;

    // CRC32: valor canonico (mesma conta do zlib.crc32 do celerctl)
    const uint8_t probe[] = "123456789";
    CHECK(crc32(probe, 9) == 0xCBF43926u);
    // encadeamento: crc32(a||b) == crc32(b, seed=crc32(a))
    const uint8_t ab[] = "abcdef";
    CHECK(crc32(ab, 6) == crc32(ab + 3, 3, crc32(ab, 3)));

    uint8_t payload[4096];
    uint8_t frame[4104];

    // ---- proto 1: montagem e roundtrip
    memcpy(payload, "CELERCTL1", 9);
    size_t n = build(frame, sizeof(frame), false, 0x01, payload, 9);
    CHECK(n == 13);
    CHECK(frame[0] == MAGIC && frame[1] == 0x01 && frame[2] == 9 && frame[3] == 0);
    uint8_t pbuf[256];
    FrameParser p1(pbuf, sizeof(pbuf));
    RxFrame rx = parseAll(p1, frame, n);
    CHECK(rx.count == 1 && rx.cmd == 0x01 && rx.payload.size() == 9);
    CHECK(memcmp(rx.payload.data(), "CELERCTL1", 9) == 0);

    // frame com len 0 (REBOOT/WRITE_END sem payload)
    n = build(frame, sizeof(frame), false, 0x0D, nullptr, 0);
    CHECK(n == 4);
    p1.setV2(false);
    rx = parseAll(p1, frame, n);
    CHECK(rx.count == 1 && rx.cmd == 0x0D && rx.payload.empty());

    // ---- proto 2: CRC no fio, roundtrip, corrupcao
    memcpy(payload, "CELERCTL2", 9);
    n = build(frame, sizeof(frame), true, 0x01, payload, 9);
    CHECK(n == 17);  // 8 de header + 9 de payload
    FrameParser p2(pbuf, sizeof(pbuf));
    p2.setV2(true);
    rx = parseAll(p2, frame, n);
    CHECK(rx.count == 1 && rx.cmd == 0x01 && rx.payload.size() == 9);
    CHECK(memcmp(rx.payload.data(), "CELERCTL2", 9) == 0);

    p2.setV2(true);
    frame[n - 1] ^= 0xFF;  // corrompe o ultimo byte do payload
    rx = parseAll(p2, frame, n);
    CHECK(rx.count == 0);                 // CRC rejeita: nada entregue
    CHECK(p2.takeReject() == FrameParser::REJ_CRC);
    frame[n - 1] ^= 0xFF;

    // payload grande (limite do parser)
    for (size_t i = 0; i < sizeof(pbuf); i++) payload[i] = (uint8_t)(i * 7);
    n = build(frame, sizeof(frame), true, 0x07, payload, sizeof(pbuf));
    CHECK(n == 8 + sizeof(pbuf));
    p2.setV2(true);
    rx = parseAll(p2, frame, n);
    CHECK(rx.count == 1 && rx.payload.size() == sizeof(pbuf));
    CHECK(memcmp(rx.payload.data(), payload, sizeof(pbuf)) == 0);

    // payload acima do limite: rejeitado sem entregar
    n = build(frame, sizeof(frame), false, 0x07, payload, 4096);
    CHECK(n > 0);
    rx = parseAll(p1, frame, n);
    CHECK(rx.count == 0);
    CHECK(p1.takeReject() == FrameParser::REJ_TOO_BIG);

    // frame v2 com len 0 consome o CRC (nao deixa sujeira para o proximo)
    n = build(frame, sizeof(frame), true, 0x08, nullptr, 0);
    CHECK(n == 8);
    uint8_t two[16];
    size_t m = build(two, sizeof(two), true, 0x02, nullptr, 0);
    memcpy(frame + n, two, m);
    p2.setV2(true);
    rx = parseAll(p2, frame, n + m);
    CHECK(rx.count == 2);

    // ---- troca de buffer (HostLink sob demanda): pequeno ate o HELLO,
    // grande depois — inclusive trocando DENTRO do callback de entrega
    {
        uint8_t small[16];
        static uint8_t big[4096];
        FrameParser ps(small, sizeof(small));
        struct Swap { FrameParser* p; uint8_t* big; RxFrame rx; } sw{&ps, big, RxFrame()};
        auto swapFn = [](void* ctx, uint8_t cmd, const uint8_t* pl, uint16_t len) {
            Swap* s = (Swap*)ctx;
            onFrame(&s->rx, cmd, pl, len);
            if (cmd == 0x01) s->p->setBuffer(s->big, 4096);
        };
        n = build(frame, sizeof(frame), false, 0x07, payload, 1000);
        for (size_t i = 0; i < n; i++) ps.feed(frame[i], 0, swapFn, &sw);
        CHECK(sw.rx.count == 0 && ps.takeReject() == FrameParser::REJ_TOO_BIG);  // antes do HELLO
        memcpy(payload, "CELERCTL1", 9);
        n = build(frame, sizeof(frame), false, 0x01, payload, 9);
        size_t m2 = build(frame + n, sizeof(frame) - n, false, 0x07, payload, 1000);
        for (size_t i = 0; i < n + m2; i++) ps.feed(frame[i], 0, swapFn, &sw);
        CHECK(sw.rx.count == 2 && sw.rx.cmd == 0x07 && sw.rx.payload.size() == 1000);
        CHECK(memcmp(sw.rx.payload.data(), payload, 1000) == 0);
        ps.setBuffer(small, sizeof(small));  // fim de sessao: volta ao pequeno
        n = build(frame, sizeof(frame), false, 0x07, payload, 1000);
        sw.rx = RxFrame();
        for (size_t i = 0; i < n; i++) ps.feed(frame[i], 0, swapFn, &sw);
        CHECK(sw.rx.count == 0 && ps.takeReject() == FrameParser::REJ_TOO_BIG);
    }

    // ---- resync de silencio: frame cortado descartado, o proximo passa
    p2.setV2(true);
    n = build(frame, sizeof(frame), true, 0x05, payload, 16);
    // alimenta metade no instante 0...
    for (size_t i = 0; i < n / 2; i++) p2.feed(frame[i], 0, onFrame, &rx);
    rx = RxFrame();
    // ...outra metade 300ms depois: parser volta ao WANT_MAGIC
    for (size_t i = n / 2; i < n; i++) p2.feed(frame[i], 300000, onFrame, &rx);
    CHECK(rx.count == 0);

    // lixo antes do magic e ignorado (resync byte a byte)
    p2.setV2(true);
    n = build(frame, sizeof(frame), true, 0x03, payload, 8);
    uint8_t noisy[520];
    memset(noisy, 0x41, sizeof(noisy));
    memcpy(noisy + 500, frame, n);
    rx = parseAll(p2, noisy, 500 + n);
    CHECK(rx.count == 1 && rx.cmd == 0x03);

    // ---- fallback v2 -> v1: HELLO de host antigo numa sessao proto 2
    // host v1 manda [43 01 09 00]"CELERCTL1"; parser em v2 le "CELE" no
    // lugar do crc, devolve os bytes ao payload e cai para proto 1
    p2.setV2(true);
    uint8_t hello1[13] = {MAGIC, 0x01, 9, 0, 'C', 'E', 'L', 'E', 'R', 'C', 'T', 'L', '1'};
    rx = parseAll(p2, hello1, sizeof(hello1));
    CHECK(rx.count == 1 && rx.cmd == 0x01);
    CHECK(rx.payload.size() == 9 && memcmp(rx.payload.data(), "CELERCTL1", 9) == 0);
    CHECK(!p2.v2());  // sessao caiu para proto 1

    // frame v1 comum (nao-HELLO) numa sessao v2: o fallback "CELE" nao
    // vale — o frame fica pendente (4 bytes "roubados" pelo crc) e o
    // silencio de 250ms descarta; nada entregue, sessao intacta
    FrameParser p2b(pbuf, sizeof(pbuf));
    p2b.setV2(true);
    uint8_t stat1[13] = {MAGIC, 0x04, 9, 0, 'C', 'E', 'L', 'E', 'R', 'C', 'T', 'L', '1'};
    rx = parseAll(p2b, stat1, sizeof(stat1));
    CHECK(rx.count == 0);
    CHECK(p2b.v2());  // sessao permanece em proto 2
}

static void testFsJail() {
    s_perms = celer::PERM_FS;
    s_appPkg = "celeros.snake";
    // forma canonica: segmento inteiro, sem "//", "." ou ".."
    CHECK(fsPathCanonical("/local/x.txt"));
    CHECK(fsPathCanonical("/sd/"));
    CHECK(fsPathCanonical("/local"));
    CHECK(!fsPathCanonical("/local/./wifi.txt"));
    CHECK(!fsPathCanonical("/local//wifi.txt"));
    CHECK(!fsPathCanonical("/local/data/../wifi.txt"));
    CHECK(!fsPathCanonical("/local/.."));
    CHECK(!fsPathCanonical("/sdcard/x"));
    CHECK(!fsPathCanonical("/dev/uart/0"));
    // arquivos do sistema (e o .tmp da escrita atomica)
    CHECK(!fsPathAllowed("/local/wifi.txt"));
    CHECK(!fsPathAllowed("/local/settings_pin2.bin"));
    CHECK(!fsPathAllowed("/local/ota_url.txt.tmp"));
    CHECK(fsPathAllowed("/local/x.txt"));
    // pastas de apps: leitura sim, escrita so "system"
    CHECK(fsPathAllowed("/local/apps/Settings/main.js"));
    CHECK(!fsWriteAllowed("/local/apps/Settings/main.js"));
    CHECK(!fsWriteAllowed("/sd/apps"));
    CHECK(fsWriteAllowed("/sd/appsX/a"));
    // appData: so a do proprio pacote
    CHECK(fsWriteAllowed("/local/data/celeros.snake/s.txt"));
    CHECK(!fsPathAllowed("/local/data/celeros.snak/x"));
    CHECK(!fsPathAllowed("/local/data/other/x"));
    // arvores: raizes e pastas de apps
    CHECK(!fsTreeAllowed("/local"));
    CHECK(!fsTreeAllowed("/local/"));
    CHECK(!fsTreeAllowed("/local/data"));
    CHECK(fsTreeAllowed("/sd"));
    CHECK(!fsTreeWriteAllowed("/sd"));
    CHECK(!fsTreeWriteAllowed("/local/apps/X"));
    CHECK(fsTreeWriteAllowed("/sd/music"));
    // "system" passa nas regras de dono, nunca na forma canonica
    s_perms = celer::PERM_SYSTEM;
    CHECK(fsPathAllowed("/local/wifi.txt"));
    CHECK(fsWriteAllowed("/local/apps/Settings/main.js"));
    CHECK(fsTreeWriteAllowed("/local"));
    CHECK(!fsPathAllowed("/local/./wifi.txt"));
    s_perms = 0;
}

static void testAlarmCalc() {
    setenv("TZ", "UTC0", 1);
    tzset();
    // formato ida e volta
    AlarmSpec a;
    a.hour = 7; a.minute = 5; a.days = 0x3E; a.enabled = true; a.label = "Trab|alho\n";
    std::string f = formatAlarm(a);
    CHECK(f == "07:05|62|1|Trabalho");
    AlarmSpec b;
    CHECK(parseAlarm(f, b));
    CHECK(b.hour == 7 && b.minute == 5 && b.days == 0x3E && b.enabled && b.label == "Trabalho");
    CHECK(parseAlarm("23:59|0|0|", b) && !b.enabled && b.label.empty());
    CHECK(!parseAlarm("24:00|0|1|x", b));
    CHECK(!parseAlarm("lixo", b));
    CHECK(f.size() <= 63 && formatAlarm(AlarmSpec{1, 2, 3, true, std::string(200, 'x')}).size() <= 63);

    // 2026-10-02 (sexta) 10:00:00 UTC
    const time_t fri10 = 1790935200;
    struct tm chk;
    gmtime_r(&fri10, &chk);
    CHECK(chk.tm_wday == 5 && chk.tm_hour == 10);
    AlarmSpec once{9, 0, 0, true, ""};
    CHECK(nextAlarmAfter(once, fri10) == fri10 + 23 * 3600);       // amanha 09:00
    AlarmSpec later{10, 30, 0, true, ""};
    CHECK(nextAlarmAfter(later, fri10) == fri10 + 1800);            // hoje 10:30
    AlarmSpec exact{10, 0, 0, true, ""};
    CHECK(nextAlarmAfter(exact, fri10) == fri10 + 24 * 3600);       // estritamente depois
    AlarmSpec weekdays{8, 0, 0x3E, true, ""};                       // seg..sex
    CHECK(nextAlarmAfter(weekdays, fri10) == fri10 + 2 * 86400 + 22 * 3600);  // segunda 08:00
    AlarmSpec sunday{8, 0, 0x01, true, ""};
    CHECK(nextAlarmAfter(sunday, fri10) == fri10 + 86400 + 22 * 3600);        // domingo 08:00
    AlarmSpec bad{25, 0, 0, true, ""};
    CHECK(nextAlarmAfter(bad, fri10) == 0);
}

static void testGbProto() {
    using namespace celer::gb;
    // linhas quebradas em varios writes + DLE + \r
    LineAssembler la(64);
    std::vector<std::string> lines;
    auto sink = [&](const std::string& l) { lines.push_back(l); };
    const char* p1 = "\x10GB({\"t\":\"no";
    const char* p2 = "tify\",\"id\":7})\r\nsetTime(1790935200);E.setTimeZone(-3.0);(s=>{})(1)\n";
    la.feed((const uint8_t*)p1, strlen(p1), sink);
    CHECK(lines.empty());
    la.feed((const uint8_t*)p2, strlen(p2), sink);
    CHECK(lines.size() == 2);
    Line a = classify(lines[0]);
    CHECK(a.kind == Kind::Gb && a.json == "{\"t\":\"notify\",\"id\":7}");
    Line b = classify(lines[1]);
    CHECK(b.kind == Kind::SetTime && b.epoch == 1790935200LL && b.hasTz && b.tzHours == -3.0f);
    CHECK(classify("print(1)").kind == Kind::Unknown);
    CHECK(classify("GB(nada)").kind == Kind::Unknown);
    // linha longa demais e descartada inteira, a seguinte passa
    lines.clear();
    std::string big(100, 'x');
    big += "\nGB({})\n";
    la.feed((const uint8_t*)big.data(), big.size(), sink);
    CHECK(lines.size() == 1 && lines[0] == "GB({})");
    // fusos
    CHECK(tzPosix(-3.0f) == "<-03>3");
    CHECK(tzPosix(0.0f) == "<+00>0");
    CHECK(tzPosix(5.5f) == "<+0530>-5:30");
    CHECK(tzPosix(-9.5f) == "<-0930>9:30");
    // texto do Android -> Latin-1 desenhavel
    CHECK(latin1Safe("Olá, João!", 64) == "Olá, João!");
    CHECK(latin1Safe("oi \xF0\x9F\x98\x80 tudo", 64) == "oi tudo");           // emoji some
    CHECK(latin1Safe("\xE2\x80\x9C" "disse" "\xE2\x80\x9D \xE2\x80\x94 ok\xE2\x80\xA6", 64) == "\"disse\" - ok...");
    CHECK(latin1Safe("linha1\nlinha2", 64) == "linha1 linha2");
    CHECK(latin1Safe("ação", 4) == "aç");                                     // nao parte o UTF-8
    CHECK(latin1Safe("\xC3", 8).empty());                                      // sequencia truncada
    CHECK(latin1Safe("\x80" "abc", 8) == "abc");
    CHECK(jsonStr("a\"b\\c\n") == "\"a\\\"b\\\\c\\u000a\"");
}

static DeviceStats::Snapshot statsFixture() {
    DeviceStats::Snapshot s{};
    s.uptimeUs = 1234567890ULL;
    s.cpuFreqMHz = 240;
    s.heapFree = 100000; s.heapMin = 80000; s.heapLargest = 60000;
    s.intFree = 90000; s.intMin = 70000; s.intLargest = 50000;
    s.psramFree = 4000000; s.psramTotal = 8000000; s.psramMin = 3900000; s.psramLargest = 3000000;
    s.jsActive = true; s.jsLaunchFree = 180000; s.jsNowFree = 160000;
    s.jsAllocs = 500; s.jsAllocsPeak = 700;
    s.loopBusyUs = 1000; s.loopTotalUs = 10000;
    s.uiFrames = 20; s.uiPresents = 30; s.uiFrameUs = 40000; s.uiFrameUsMax = 8000;
    s.totalRunTimeUs = 999999;
    strcpy(s.tasks[0].name, "IDLE0");
    s.tasks[0].state = 'r'; s.tasks[0].prio = 0; s.tasks[0].stackFree = 900;
    s.tasks[0].runTimeUs = 500000;
    strcpy(s.tasks[1].name, "main");
    s.tasks[1].state = 'R'; s.tasks[1].prio = 1; s.tasks[1].stackFree = 12000;
    s.tasks[1].runTimeUs = 499999;
    s.taskCount = 2;
    return s;
}

// toJson do DeviceStats: a resposta KL_STATS inteira. Pura sobre a struct —
// aqui valida o formato e a degradacao graciosa quando o cap nao basta
// (task parcial volta para tras, "trunc":1, nunca JSON quebrado).
static void testDeviceStatsJson() {
    DeviceStats::Snapshot s = statsFixture();
    char buf[2048];
    size_t n = DeviceStats::toJson(s, buf, sizeof(buf));
    CHECK(n > 0 && n < sizeof(buf));
    std::string js(buf, n);
    CHECK(js.front() == '{' && js.back() == '}');
    CHECK(js.find("\"uptime_us\":1234567890") != std::string::npos);
    CHECK(js.find("\"cpu_mhz\":240") != std::string::npos);
    CHECK(js.find("\"psram_total\":8000000") != std::string::npos);
    CHECK(js.find("\"n\":\"IDLE0\"") != std::string::npos);
    CHECK(js.find("\"rt\":499999") != std::string::npos);
    CHECK(js.find("\"stk\":12000") != std::string::npos);
    CHECK(js.find("\"trunc\":0") != std::string::npos);
    CHECK(js.find("\"allocs_peak\":700") != std::string::npos);

    // caps cada vez maiores: JSON sempre fechado, nunca estoura o buffer
    for (size_t cap : {40u, 200u, 400u, 520u, 700u, 1000u}) {
        char small[1000];
        CHECK(cap <= sizeof(small));
        size_t m = DeviceStats::toJson(s, small, cap);
        CHECK(m > 0 && m < cap);
        CHECK(small[0] == '{' && small[m - 1] == '}');
    }
    // buffer impossivel: 0 (host descarta em vez de parsear lixo)
    char tiny[16];
    CHECK(DeviceStats::toJson(s, tiny, sizeof(tiny)) == 0);
    // sem tasks: lista vazia fecha certo
    DeviceStats::Snapshot e = statsFixture();
    e.taskCount = 0;
    char eb[600];
    size_t en = DeviceStats::toJson(e, eb, sizeof(eb));
    CHECK(en > 0 && std::string(eb, en).find("\"tasks\":[],\"trunc\":0") != std::string::npos);
}

// ---- MusicEngine (System.playMusic, API 25) ----
// O motor puro e o mesmo do device: compilacao (clamps + eventos em
// amostras) e renderizacao inteira/deterministica.
static void testMusicEngine() {
    using namespace MusicEngine;
    const uint32_t kRate = 16000;

    // musica valida: bateria + baixo, 8 semicolcheias por volta
    Song s;
    s.bpm = 120;   // semicolcheia = 125 ms = 2000 amostras
    s.loops = 2;
    s.nTracks = 2;
    s.tracks[0].drum = true;
    s.tracks[0].vol = 100;
    s.tracks[0].count = 4;
    s.tracks[0].notes[0] = {36, 2};
    s.tracks[0].notes[1] = {42, 2};
    s.tracks[0].notes[2] = {38, 2};
    s.tracks[0].notes[3] = {42, 2};
    s.tracks[1].wave = kWaveTri;
    s.tracks[1].vol = 90;
    s.tracks[1].count = 2;
    s.tracks[1].notes[0] = {40, 4};
    s.tracks[1].notes[1] = {47, 4};

    Compiled c;
    CHECK(compile(s, kRate, c));
    CHECK(c.nTracks == 2);
    CHECK(c.loopSamples == 8 * 2000);      // trilha mais longa manda
    CHECK(c.totalSamples == 2 * c.loopSamples);
    CHECK(c.loops == 2);
    // eventos do baixo: 2 notas, 4 semicolcheias cada
    CHECK(c.tracks[1].count == 2);
    CHECK(c.tracks[1].evs[0].start == 0);
    CHECK(c.tracks[1].evs[0].end == 4 * 2000);
    CHECK(c.tracks[1].evs[1].start == 4 * 2000);

    // clamps: bpm fora da faixa, loops altos demais (teto de 120 s corta),
    // midi > 96, duracao 0 -> 1
    Song bad = s;
    bad.bpm = 999;   // clampado a 200: semicolcheia = 75 ms = 1200 amostras
    bad.loops = 8;
    bad.tracks[1].notes[0] = {120, 0};
    Compiled cb;
    CHECK(compile(bad, kRate, cb));
    CHECK(cb.loops < 8 || cb.totalSamples <= kMaxTotalMs * kRate / 1000);
    CHECK(cb.tracks[1].evs[0].midi == 96);
    CHECK(cb.tracks[1].evs[0].end - cb.tracks[1].evs[0].start == 1200);  // len 0 -> 1

    // musica vazia (so pausas / sem trilhas): nao compila
    Song empty; empty.nTracks = 1; empty.tracks[0].count = 1; empty.tracks[0].notes[0] = {0, 4};
    Compiled ce;
    CHECK(!compile(empty, kRate, ce));

    // render: nota melodica produz som na regiao da nota
    Renderer r;
    r.reset(&c, 100);
    static int16_t buf[16000];
    r.render(buf, 16000);
    bool noteLoud = false;
    for (int i = 1000; i < 7000; i++)   // dentro da nota do baixo
        if (buf[i] > 500 || buf[i] < -500) noteLoud = true;
    CHECK(noteLoud);
    CHECK(r.pos == 16000);
    // mistura inteira/deterministica: dois renderers iguais, mesma saida
    Renderer r2;
    r2.reset(&c, 100);
    static int16_t buf2[16000];
    r2.render(buf2, 16000);
    CHECK(memcmp(buf, buf2, sizeof(buf)) == 0);

    // bateria: bumbo na 1a semicolcheia produz graves fortes
    r.reset(&c, 100);
    int16_t one[2000];
    r.render(one, 2000);
    bool kick = false;
    for (int i = 0; i < 2000; i++) if (one[i] > 1000 || one[i] < -1000) kick = true;
    CHECK(kick);

    // teto de amplitude: nunca estoura o int16
    Song loud = s;
    loud.nTracks = 1;
    loud.tracks[0].drum = false;
    loud.tracks[0].vol = 100;
    loud.tracks[0].count = 1;
    loud.tracks[0].notes[0] = {96, 8};
    Compiled cl;
    CHECK(compile(loud, kRate, cl));
    Renderer rl;
    rl.reset(&cl, 100);
    int16_t lb[8000];
    rl.render(lb, 8000);
    bool clipped = false;
    for (int i = 0; i < 8000; i++) {
        if (lb[i] > 24000 || lb[i] < -24000) clipped = true;
    }
    CHECK(!clipped);
}

// CelerNet (NetFrame.h): quadro da malha por flood de advertising —
// round-trip (com dst do unicast), fragmentacao/remontagem e dedup, no host.
static void testNetFrame() {
    using namespace netframe;

    // round-trip DATA com o payload maximo (16 B) e dst do unicast
    uint8_t buf[ADV_MAX];
    Frame f;
    f.type = TYPE_DATA;
    f.netId = 0xBEEF;
    f.src = 0x1234;
    f.dst = 0xA1B2;
    f.seq = 0x5678;
    f.ttl = 4;
    f.hops = 2;
    const uint8_t payload[DATA_MAX] = "0123456789ABCDE";  // 15 + NUL = 16
    f.dlen = DATA_MAX;
    f.data = payload;
    size_t n = encode(f, buf, sizeof(buf));
    CHECK(n == HDR + DATA_MAX);
    Frame g;
    CHECK(decode(buf, n, &g));
    CHECK(g.type == TYPE_DATA && g.netId == 0xBEEF && g.src == 0x1234 && g.seq == 0x5678);
    CHECK(g.dst == 0xA1B2);
    CHECK(g.ttl == 4 && g.hops == 2 && g.dlen == DATA_MAX);
    CHECK(memcmp(g.data, payload, DATA_MAX) == 0);
    CHECK(g.dst != DST_BROADCAST);

    // default do Frame e broadcast: vizinho nao filtrado entrega
    Frame bc;
    CHECK(bc.dst == DST_BROADCAST);
    bc.type = TYPE_DATA;
    bc.netId = 0xBEEF;
    bc.src = 0x1234;
    bc.seq = 1;
    bc.dlen = 2;
    const uint8_t two[2] = {0xAA, 0xBB};
    bc.data = two;
    n = encode(bc, buf, sizeof(buf));
    CHECK(decode(buf, n, &g) && g.dst == DST_BROADCAST);

    // rejeicoes: dados grandes demais, ttl fora da faixa, lixo no ar
    Frame bad = f;
    bad.dlen = DATA_MAX + 1;
    CHECK(encode(bad, buf, sizeof(buf)) == 0);
    bad = f; bad.ttl = 0;   CHECK(encode(bad, buf, sizeof(buf)) == 0);
    bad = f; bad.ttl = TTL_MAX + 1; CHECK(encode(bad, buf, sizeof(buf)) == 0);
    CHECK(!decode(buf, n - 1, &g));                 // tamanho colidindo
    buf[0] = 'X';            CHECK(!decode(buf, n, &g));  // magic errado
    buf[0] = 'C'; buf[2] = 1; CHECK(!decode(buf, n, &g));  // versao errada (v1)

    // BEAT v2: [caps(1)][nome] — o papel do no viaja na presenca
    f.type = TYPE_BEAT;
    f.dst = DST_BROADCAST;
    const char* nome = "Celer-Dog";
    uint8_t beat[DATA_MAX];
    beat[0] = CAPS_SPEAKER | CAPS_MOTORS | CAPS_LEDS;
    size_t nomeLen = strlen(nome);
    memcpy(beat + 1, nome, nomeLen);
    f.dlen = (uint8_t)(1 + nomeLen);
    f.data = beat;
    n = encode(f, buf, sizeof(buf));
    CHECK(n == HDR + 1 + nomeLen);
    CHECK(decode(buf, n, &g) && g.type == TYPE_BEAT);
    CHECK(g.data[0] == (CAPS_SPEAKER | CAPS_MOTORS | CAPS_LEDS));
    CHECK(memcmp(g.data + 1, nome, nomeLen) == 0);

    // fnv16: deterministico e distingue nomes de rede
    CHECK(fnv16("celer") == fnv16("celer"));
    CHECK(fnv16("celer") != fnv16("casa"));

    // dedup: (src, seq, idx) visto 2x; vizinhos distintos passam
    DedupRing<4> dd;
    CHECK(!dd.seen(1, 100, 0));   // 1a vez = novo
    CHECK(dd.seen(1, 100, 0));    // repetido
    CHECK(!dd.seen(1, 100, 1));   // outro fragmento
    CHECK(!dd.seen(1, 101, 0));   // outro seq
    CHECK(!dd.seen(2, 100, 0));   // outro no
    CHECK(dd.seen(1, 100, 1));    // frag repetido de antes
    dd.clear();
    CHECK(!dd.seen(1, 100, 0));   // clear: tudo novo de novo

    // remontagem: mensagem de 434 B = 31 fragmentos de 14 B, fora de ordem
    uint8_t msg[MSG_MAX];
    for (size_t i = 0; i < MSG_MAX; i++) msg[i] = (uint8_t)(i * 7);
    Reassembler ra;
    uint8_t out[MSG_MAX];
    size_t outLen = 0;
    const uint8_t total = (MSG_MAX + CHUNK_MAX - 1) / CHUNK_MAX;  // 31
    CHECK(total == MAX_FRAGS);
    bool done = false;
    for (int pass = 0; pass < 2 && !done; pass++) {
        for (int k = 0; k < total && !done; k++) {
            int idx = (k * 7) % total;  // ordem embaralhada (7 e coprimo de 31)
            size_t off = (size_t)idx * CHUNK_MAX;
            uint8_t len = idx + 1 == total ? (uint8_t)(MSG_MAX - off) : CHUNK_MAX;
            done = ra.feed(0xA1B2, 42, (uint8_t)idx, total, msg + off, len,
                           pass * 100 + k * 10, out, sizeof(out), &outLen);
        }
    }
    CHECK(done);
    CHECK(outLen == MSG_MAX);
    CHECK(memcmp(out, msg, MSG_MAX) == 0);

    // fragmento repetido nao entrega duas vezes nem corrompe o slot
    ra.reset();
    done = ra.feed(3, 7, 0, 2, msg, CHUNK_MAX, 0, out, sizeof(out), &outLen);
    CHECK(!done);
    done = ra.feed(3, 7, 0, 2, msg, CHUNK_MAX, 1, out, sizeof(out), &outLen);  // repetido
    CHECK(!done);
    done = ra.feed(3, 7, 1, 2, msg + CHUNK_MAX, 4, 2, out, sizeof(out), &outLen);
    CHECK(done && outLen == CHUNK_MAX + 4);

    // chunk curto no MEIO (nao-ultimo) e recusado: buraco nao cola
    ra.reset();
    CHECK(!ra.feed(3, 8, 0, 2, msg, CHUNK_MAX - 1, 0, out, sizeof(out), &outLen));

    // total acima do teto e recusado (31 e o maximo de fragmentos)
    ra.reset();
    CHECK(!ra.feed(3, 8, 30, 32, msg, CHUNK_MAX, 0, out, sizeof(out), &outLen));

    // slot vence: fragmento perdido libera para a proxima mensagem
    ra.reset();
    CHECK(!ra.feed(4, 9, 0, 2, msg, CHUNK_MAX, 0, out, sizeof(out), &outLen));
    ra.prune(3000, 2000);  // 3 s depois do lastMs=0
    done = ra.feed(4, 9, 1, 2, msg + CHUNK_MAX, 4, 4000, out, sizeof(out), &outLen);
    CHECK(!done);  // slot antigo foi podado: frag solto nao entrega
    done = ra.feed(4, 9, 0, 2, msg, CHUNK_MAX, 4100, out, sizeof(out), &outLen);
    CHECK(done);   // mensagem nova (mesmo src/seq) recomeca limpa
}

int main() {
    testSemVer();
    testFsJail();
    testPermissions();
    testJsStrip();
    testJsStripInPlaceApps();
    testHostFrame();
    testAlarmCalc();
    testGbProto();
    testDeviceStatsJson();
    testMusicEngine();
    testNetFrame();
    if (g_failed == 0) {
        printf("OK: %d checks passaram\n", g_total);
        return 0;
    }
    printf("%d/%d falharam\n", g_failed, g_total);
    return 1;
}
