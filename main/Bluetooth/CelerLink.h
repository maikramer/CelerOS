#ifndef CELEROS_BT_CELER_LINK_H
#define CELEROS_BT_CELER_LINK_H

#include <stddef.h>
#include <stdint.h>

// Celer Link: link Bluetooth LE entre CelerOS proximos (API 9).
//
// Um lado vira "controlavel" com start(): servidor GATT proprio +
// advertising com o nome "Celer-XXXX". O outro lado scan()-neia, connect()
// e troca mensagens de ate MAX_MSG bytes com send()/poll().
//
// Pareamento por codigo (API 11): start(nome, true) exige que o central
// digite um codigo de 6 digitos gerado por conexao (exposto so ao app
// LOCAL via status().code) antes de liberar o canal de dados. Acertos
// ficam memorizados no NVS (bond por MAC, ate 4 controles) e as proximas
// conexoes do mesmo controle entram direto. Nao ha criptografia no ar:
// o codigo protege contra o vizinho casual, nao contra sniffer.
//
// O nome CelerLink e deste link; o protocola do celerctl e o HostLink
// (main/USBDevice/HostLink.h).
class CelerLink {
public:
    static constexpr size_t MAX_MSG = 240;   // payload por mensagem (cabe no MTU 256 - 3)
    static constexpr size_t MAX_NAME = 29;   // nome no scan response (31 - 2 de cabecalho)
    static constexpr int RX_DEPTH = 8;       // fila de mensagens recebidas

    // Par visto num scan. id = "AA:BB:CC:DD:EE:FF" (17 chars), name = advertising.
    struct Peer {
        char id[18];
        char name[MAX_NAME + 1];
        int8_t rssi;
    };

    // Estado da sessao para o status() do JS.
    struct Info {
        bool connected;     // pronto para send() (central: ja inscrito)
        bool listening;     // advertising pedido via start()
        bool central;       // nos conectamos no peer (vs peer conectou em nos)
        bool pairing;       // conexao ativa aguardando o codigo (API 11)
        bool verified;      // canal de dados autorizado (sem pareamento = true)
        char peer[18];      // "" sem conexao
        char name[MAX_NAME + 1];  // nosso nome de advertising
        char code[7];       // codigo do pareamento (so no peripheral pendente)
        uint16_t mtu;       // MTU ATT negociado (0 sem conexao)
        int8_t rssi;        // RSSI da conexao (0 sem conexao/leitura)
        uint16_t pending;   // mensagens na fila RX
        uint32_t dropped;   // mensagens descartadas por fila cheia
    };

    // Inicializacao lazy do NimBLE (idempotente; ~300ms na 1a chamada).
    static bool ensureStarted();

    // Papel peripheral: advertising + servidor GATT. name nullptr mantem o
    // atual (default "Celer-XXXX", XXXX = fim da MAC BT). requirePairing
    // liga o gate de codigo (API 11); vale para as conexoes seguintes.
    static bool start(const char* name, bool requirePairing = false);
    static bool stop();
    static bool listening();

    // Papel central: scan bloqueante filtrando o servico Celer Link.
    // Preenche ate max entradas de out (RSSI mais forte primeiro).
    static int scan(uint32_t ms, Peer* out, int max);

    // Conecta por "AA:BB:..." ou pelo nome do ultimo scan (bloqueante;
    // ms e o prazo TOTAL: conexao + MTU + descoberta + inscricao).
    static bool connect(const char* idOrName, uint32_t ms);
    static bool disconnect();
    static bool connected();

    // Pareamento (API 11). verify: lado central, envia o codigo de 6
    // digitos ao peer conectado (bloqueante ~3 s; true = canal liberado,
    // idempotente se ja verificado). unpair: lado peripheral, esquece os
    // bonds gravados no NVS (id "AA:BB:..." apaga um; nullptr apaga todos).
    static bool verify(const char* code);
    static bool unpair(const char* id);

    // Mensagens (single peer, fila RX de RX_DEPTH; cheia descarta a MAIS
    // ANTIGA — num controle remoto o comando novo vale mais).
    static bool send(const void* data, size_t len);
    // Mensagem recebida; false = fila vazia. len recebe os bytes copiados.
    static bool poll(void* buf, size_t cap, size_t* len);

    // Id do peer conectado ("" sem conexao).
    static void peerId(char* out, size_t cap);
    static void info(Info* out);

    // Reset de sessao entre apps (JSBindings::init): desconecta, para o
    // advertising, volta o nome default e limpa a fila. O NimBLE segue
    // inicializado.
    static void appReset();
};

#endif // CELEROS_BT_CELER_LINK_H
