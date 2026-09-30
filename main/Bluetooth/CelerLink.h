#ifndef CELEROS_BT_CELER_LINK_H
#define CELEROS_BT_CELER_LINK_H

#include <stddef.h>
#include <stdint.h>

// Celer Link: link Bluetooth LE entre CelerOS proximos (API 9).
//
// Um lado vira "controlavel" com start(): servidor GATT proprio +
// advertising com o nome "Celer-XXXX". O outro lado scan()-neia, connect()
// e troca mensagens de ate MAX_MSG bytes com send()/poll(). Simples e sem
// seguranca na v1: sem pareamento nem criptografia — qualquer device
// proximo pode conectar e escrever (documentado como tal nos guias).
//
// O nome CelerLink e deste link; o protocola do celerctl e o HostLink
// (main/USBDevice/HostLink.h).
class CelerLink {
public:
    static constexpr size_t MAX_MSG = 240;  // payload por mensagem (cabe no MTU 256 - 3)

    // Par visto num scan. id = "AA:BB:CC:DD:EE:FF" (17 chars), name = advertising.
    struct Peer {
        char id[18];
        char name[24];
        int8_t rssi;
    };

    // Inicializacao lazy do NimBLE (idempotente; ~300ms na 1a chamada).
    static bool ensureStarted();

    // Papel peripheral: advertising + servidor GATT. name nullptr mantem o
    // atual (default "Celer-XXXX", XXXX = fim da MAC BT).
    static bool start(const char* name);
    static bool stop();
    static bool listening();

    // Papel central: scan bloqueante filtrando o servico Celer Link.
    // Preenche ate max entradas de out; devolve quantas preencheu.
    static int scan(uint32_t ms, Peer* out, int max);

    // Conecta por "AA:BB:..." ou pelo nome do ultimo scan (bloqueante).
    static bool connect(const char* idOrName, uint32_t ms);
    static bool disconnect();
    static bool connected();

    // Mensagens (melhor esforco, single peer, fila RX de 8 entradas).
    static bool send(const void* data, size_t len);
    // Mensagem recebida; false = fila vazia. len recebe os bytes copiados.
    static bool poll(void* buf, size_t cap, size_t* len);

    // Id do peer conectado ("" sem conexao).
    static void peerId(char* out, size_t cap);

    // Reset de sessao entre apps (JSBindings::init): desconecta, para o
    // advertising e limpa a fila. O NimBLE segue inicializado.
    static void appReset();
};

#endif // CELEROS_BT_CELER_LINK_H
