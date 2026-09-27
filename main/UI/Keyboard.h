#ifndef CELEROS_UI_KEYBOARD_H
#define CELEROS_UI_KEYBOARD_H

// ============================================================================
// Keyboard — teclado QWERTY on-screen do CelerOS.
//
// Layout padrao de teclado virtual (Android/iOS): linhas escalonadas, shift
// one-shot, duas paginas de simbolos, espaco/backspace/OK na linha de baixo
// e X (cancelar) no cabecalho. A geometria e calculada por tela (240x320 de
// design via UI::sx/sy no cabecalho; teclas preenchem o resto) e a MESMA
// lista de Rects desenha e faz hit-test.
//
// Duas formas de usar:
//   1. Modal sincrono: kui::getString() — bloqueia com loop proprio (Canvas
//      + TouchPump) SEM tocar na pilha do Navigator, para funcionar sobre
//      telas que nao sao Kui (apps JS, telas legacy). Ao sair, quem chamou
//      redesenha a propria tela (mesmo contrato do MyKeyboard antigo).
//   2. Screen Kui: Navigator::push(new KeyboardScreen(...)) com onResult;
//      o callback recebe (texto, ok) e deve chamar Navigator::pop().
// ============================================================================

#include <functional>
#include <string>
#include "Kui.h"

namespace kui {

class KeyboardScreen : public Screen {
public:
    KeyboardScreen(const std::string& prompt, const std::string& initial = "", int maxLen = 64);

    // Disparado uma vez no OK (ok=true, texto confirmado) ou no X (ok=false,
    // texto vazio). O teclado nao tira a si mesmo da pilha — quem empilhou da
    // o pop (modo 2) ou o loop da ponte sai (modo 1).
    std::function<void(const std::string& text, bool ok)> onResult;

    // Modo acoplado (System.keypad* dos apps JS): OK nao encerra — dispara
    // onEnter com o texto, limpa o buffer e segue aberto. Mudancas no buffer
    // (tecla/backspace/espaco) disparam onChange para o app re-ecoar a linha.
    std::function<void(const std::string& text)> onEnter;
    std::function<void()> onChange;
    void setPersistent(bool p) { m_persistent = p; }

    // Teclado puro (System.keypadOpen({field:false})): sem cabecalho, campo
    // nem X — as teclas comecam no topo e o cancelamento e por keypadClose().
    void setShowField(bool s) { m_showField = s; }

    // Topo da area das teclas (fisico): quem acopla o teclado nao desenha
    // abaixo desta linha (System.keypadRect devolve o mesmo espaco em 240x320)
    int keysTop() const { return m_keysTop; }
    const std::string& text() const { return m_text; }

    void draw(Canvas& c) override;
    bool onTouch(const TouchEvent& ev) override;
    void onTick(uint32_t dtMs) override;
    // Cancelar e so pelo X: o swipe de borda esbarraria na coluna "q/a/z"
    bool allowsBackGesture() const override { return false; }

private:
    enum Mode { Lower, Upper, Sym1, Sym2 };
    enum Kind { KChar, KShift, KSymPage, KBksp, KMode, KSpace, KOk };
    struct Key {
        Rect r;
        Kind kind;
        char ch;  // só KChar
    };

    void rebuild();  // (re)calcula a geometria dos keys do modo atual
    static bool isLetters(Mode m) { return m == Lower || m == Upper; }
    void handleKey(int idx);
    void finish(bool ok);
    char labelChar(char ch) const;
    Rect fieldRect() const;
    Rect cancelRect() const;

    std::string m_prompt;
    std::string m_text;
    int m_maxLen;
    Mode m_mode = Lower;
    std::vector<Key> m_keys;
    bool m_persistent = false;
    bool m_showField = true;
    int m_keysTop = 0;
    int m_flashKey = -1;       // feedback visual do ultimo toque
    uint32_t m_flashMs = 0;
    uint32_t m_blinkMs = 0;    // cursor piscando
    bool m_cursorOn = true;
    bool m_done = false;
};

// Ponte sincrona: exibe o teclado modal e devolve o texto digitado
// (string vazia se cancelado ou confirmado vazio).
std::string getString(const std::string& initialText, const std::string& promptMsg, int maxLen = 64);

}  // namespace kui

#endif  // CELEROS_UI_KEYBOARD_H
