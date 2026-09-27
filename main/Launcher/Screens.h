#ifndef KRYONOS_SCREENS_H
#define KRYONOS_SCREENS_H

// Telas Kui do KryonOS (W7c). O LauncherScreen e a base da pilha do
// Navigator; as telas antigas (ainda nao portadas) rodam embrulhadas em
// LegacyScreen ate sua migracao.

#include "../UI/Kui.h"
#include "../UI/Keyboard.h"

// ---------------------------------------------------------------- Launcher --
class LauncherScreen : public kui::Screen {
public:
    void onEnter() override;
    void draw(kui::Canvas& c) override;
    bool onTouch(const kui::TouchEvent& ev) override;
    void onTick(uint32_t dtMs) override;

private:
    kui::Rect cellRect(int entryIndex) const;
    int page = 0;
    std::string lastClock;
    bool lastWifi = false;
    uint32_t offlineSinceMs = 0;
    bool hasEverConnected = false;
};

// ------------------------------------------------------------ Tela de app JS --
// Executa o app sincronamente (congela o SO ate o app sair — OS_EXIT; a
// migracao para task propria e o W7d) e volta ao launcher.
class AppHostScreen : public kui::Screen {
public:
    static AppHostScreen* instance(int appIndex);
    explicit AppHostScreen(int appIndex);
    void onEnter() override;
    void draw(kui::Canvas& c) override;
    void onTick(uint32_t dtMs) override;

private:
    int m_appIndex;
    bool m_started = false;
};

// -------------------------------------------------------------- WiFi setup --
// Configurador de WiFi (W8). Fluxo primario LOCAL: scan assincrono, lista
// rolavel de redes (abre teclado de senha nas protegidas), entrada de rede
// oculta e conexao via WebManager. A opcao "Via web" (botao do rodape) sobe
// o AP + captive portal por conexao direta (WifiSetupPortal) sem sair da
// tela — o modo web e secundario, sempre explicito.
class WifiSetupScreen : public kui::Screen {
public:
    static WifiSetupScreen* instance();
    void onEnter() override;
    void onExit() override;
    void draw(kui::Canvas& c) override;
    bool onTouch(const kui::TouchEvent& ev) override;
    void onTick(uint32_t dtMs) override;

private:
    enum Phase { LocalList, WebPortal };
    struct NetEntry {
        std::string ssid;
        int32_t rssi = 0;
        bool secure = false;
    };

    void startScan();      // dispara scan assincrono e aguarda no onTick
    void rebuildList();    // resultados do evento -> itens da lista
    void updateStatus();   // linha de estado sob o header
    void askPassword(const std::string& ssid);  // teclado de senha
    void askHiddenSsid();                       // teclado de SSID oculto
    void tryConnect(const std::string& ssid, const std::string& password, bool secure);
    void drawConnecting(const std::string& ssid);  // direto no display (connect bloqueia)
    void enterWebPortal();
    void leaveWebPortal();

    Phase m_phase = LocalList;
    bool m_scanStarted = false;   // ja disparou um scan nesta sessao
    bool m_scanning = false;      // aguardando o evento de scan
    uint32_t m_scanStartMs = 0;   // guarda de scan perdido (>20s)
    std::vector<NetEntry> m_nets;
    std::string m_status;

    kui::KeyboardScreen* m_kb = nullptr;        // teclado empilhado agora
    kui::KeyboardScreen* m_kbTrash = nullptr;   // fora da cadeia de chamadas; delete no onTick

    bool m_portalStarted = false;
    int m_portalState = 0;   // 0 esperando, 1 conectado, 2 falhou
    std::string m_portalDetail;
    uint32_t m_portalDoneAtMs = 0;

    kui::List m_list;
    kui::Button m_btnBack, m_btnScan, m_btnWeb;
};

// ------------------------------------------------------------- Legacy wrap --
// Embrulho de uma tela antiga (draw/handleTouch estaticos) como Screen.
// Desenha direto no display (sem sprite) e repete o toque enquanto
// pressionado (as telas antigas esperam polling continuo).
class LegacyScreen : public kui::Screen {
public:
    using DrawFn = void (*)();
    using TouchFn = void (*)(uint16_t, uint16_t);

    LegacyScreen(const char* name, DrawFn draw, TouchFn touch)
        : m_name(name), m_drawFn(draw), m_touchFn(touch) {}

    bool wantsDirectDraw() const override { return true; }
    const char* name() const { return m_name; }

    void draw(kui::Canvas& c) override { (void)c; m_drawFn(); }
    void onTick(uint32_t dtMs) override;
    bool onTouch(const kui::TouchEvent& ev) override;

    // Cria o wrapper do estado antigo correspondente (currentState)
    static LegacyScreen* forState(int legacyState);

    // Transicao para telas antigas a partir de codigo novo
    static void openLegacy(int legacyState) { LegacyScreen* s = forState(legacyState); if (s) kui::Navigator::push(s); }

private:
    const char* m_name;
    DrawFn m_drawFn;
    TouchFn m_touchFn;
    int m_lastX = 0, m_lastY = 0;
    bool m_pressed = false;
    uint32_t m_repeatAccum = 0;
};

#endif  // KRYONOS_SCREENS_H
