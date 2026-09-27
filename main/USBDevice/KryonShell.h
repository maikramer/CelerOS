#ifndef KRYON_SHELL_H
#define KRYON_SHELL_H

#include <string>

// Dispatcher de comandos de texto do KryonOS, estilo "adb shell". A mesma
// tabela serve aos dois consumidores:
//   - CDC0: task do shell com eco, digitando num terminal serial
//   - KryonLink EXEC: saida capturada em buffer e devolvida ao kryonctl
// Saidas sempre via callback printf-like, para nao amarrar o destino.

class KryonShell {
public:
    typedef void (*PrintFn)(void* ctx, const char* fmt, ...);

    // Executa uma linha de comando. Retorna o "exit code" do comando
    // (0 = ok, 1 = erro de uso/execucao, -1 = comando inexistente).
    static int execute(const char* line, PrintFn print, void* ctx);

    // Mesma politica de caminhos do WebManager: so /local e /sd.
    static bool pathAllowed(const std::string& path);

    static constexpr int MAX_LINE = 256;
};

// Editor de linha minimalista compartilhado pelos consoles (UART e CDC):
// eco, backspace e devolve a linha completa no Enter.
class LineEditor {
public:
    typedef KryonShell::PrintFn PrintFn;

    explicit LineEditor(PrintFn echo, void* ctx) : m_echo(echo), m_ctx(ctx) {}

    // Alimenta um byte; retorna true quando uma linha foi completada
    // (disponivel em line, ja NUL-terminada). Bytes nao-imprimiveis que
    // nao sao Enter/Backspace sao ignorados (permite coexistir com
    // protocolos binarios no mesmo canal).
    bool feed(uint8_t byte, char* line, size_t maxLen);

    void reset() { m_len = 0; }

private:
    PrintFn m_echo;
    void* m_ctx;
    char m_buf[KryonShell::MAX_LINE];
    size_t m_len = 0;
};

#endif // KRYON_SHELL_H
