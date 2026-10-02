// Testes host da lógica C++ pura do firmware (F5) — rodam no CI e local:
//   g++ -std=c++17 -Wall -Wextra -o celeros_tests test/cpp/run_tests.cpp && ./celeros_tests
// Sem framework: asserts com contagem (falha = exit 1). O alvo aqui é o
// código header-only extraído para main/Utils (SemVer, AppPerms) — a barra
// é subir a cada PR o que antes só era validado no device.

#include "../../main/Utils/SemVer.h"
#include "../../main/Utils/AppPerms.h"
#include "../../main/Utils/JsStrip.h"
#include "../../main/USBDevice/HostFrame.h"

// Jail do FS dos apps JS: o teste faz o papel do runtime (perm/s_appPkg)
#include <cstdint>
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
    CHECK(parsePermissions("{\"permissions\":[\"fs\",\"gpio\",\"system\",\"net\"]}") ==
          (PERM_FS | PERM_NET | PERM_GPIO | PERM_SYSTEM));
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

static void testJsStrip() {
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

int main() {
    testSemVer();
    testFsJail();
    testPermissions();
    testJsStrip();
    testHostFrame();
    if (g_failed == 0) {
        printf("OK: %d checks passaram\n", g_total);
        return 0;
    }
    printf("%d/%d falharam\n", g_failed, g_total);
    return 1;
}
