// Testes host da lógica C++ pura do firmware (F5) — rodam no CI e local:
//   g++ -std=c++17 -Wall -Wextra -o celeros_tests test/cpp/run_tests.cpp && ./celeros_tests
// Sem framework: asserts com contagem (falha = exit 1). O alvo aqui é o
// código header-only extraído para main/Utils (SemVer, AppPerms) — a barra
// é subir a cada PR o que antes só era validado no device.

#include "../../main/Utils/SemVer.h"
#include "../../main/Utils/AppPerms.h"

#include <cstdio>
#include <cstring>
#include <string>

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
    CHECK(parsePermissions("{\"permissions\":[]}") == PERM_ALL);                  // vazio = nao tranca
    CHECK(parsePermissions("{\"permissions\":[}") == PERM_ALL);                   // invalido = nao tranca
    // campo vizinho nao confunde (ordem no JSON nao importa)
    CHECK(parsePermissions("{\"api\":6,\"permissions\":[\"fs\"],\"topbar\":true}") == PERM_FS);
    // token desconhecido nao concede nada — e array sem concessao valida
    // volta PERM_ALL (nao tranca app por engano de formatacao)
    CHECK((parsePermissions("{\"permissions\":[\"fs\",\"netfs\"]}") & (PERM_NET | PERM_GPIO | PERM_SYSTEM)) == 0);
}

int main() {
    testSemVer();
    testPermissions();
    if (g_failed == 0) {
        printf("OK: %d checks passaram\n", g_total);
        return 0;
    }
    printf("%d/%d falharam\n", g_failed, g_total);
    return 1;
}
