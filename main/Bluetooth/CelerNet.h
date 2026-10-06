#ifndef CELEROS_BT_CELER_NET_H
#define CELEROS_BT_CELER_NET_H

#include <stddef.h>
#include <stdint.h>

// CelerNet: malha BLE entre CelerOS por flood de advertising (API 26).
//
// CelerOS proximos formam UMA rede: cada no anuncia quadros ADV_NONCONN de
// 31 bytes (NetFrame.h) e escuta o ar; quem ouve um quadro novo entrega ao
// app e REPETE com ttl-1 apos um jitter — os dados cruzam a area por
// saltos, sem nenhuma conexao GATT. O Celer Link par-a-par (pareamento,
// selos, 240 B por pacote) segue intocado para sessoes de alta vazao.
//
// A malha e INFRAESTRUTURA, nao sessao de app: sobrevive a troca de app
// (nao passa pelo appReset do CelerLink) e o liga/desliga persiste no
// setting "celernet" (CelerSettings). O servico ALWAYS (Services.cpp)
// bombeia o tick nos dois contextos (launcher e app).
//
// Filtro de rede: so entram quadros com o MESMO netId (FNV-1a 16 bits do
// nome da rede, setting "celernet_net", default "celer"). v1 ABERTA: os
// pacotes vai em claro no ar — criptografia com chave compartilhada fica
// para a v2. Identidade do no: 2 ultimos bytes da MAC BT (zero config).
//
// Concorrencia: os callbacks rodam na task do host NimBLE (so enfileiram
// adv reports); todo o resto (dedup, relay, presenca, bursts) roda no
// tick sob mutex — o tick e chamado pelos DOIS pumps (celerLoop e
// present()), que sao tasks diferentes.
class CelerNet {
public:
    static constexpr size_t MAX_MSG = 240;   // teto da mensagem (15 frag x 16 B)
    static constexpr size_t MAX_NAME = 18;   // nome no BEAT (31 - 13 do header)
    static constexpr int RX_DEPTH = 8;       // mensagens entregues ao JS
    static constexpr int NODES_MAX = 16;     // presenca ouvida
    static constexpr uint8_t TTL_DEFAULT = 4;

    // No ouvido (BEAT recente). lastSeenMs e a idade bruta (millis).
    struct Node {
        uint16_t id;
        char name[MAX_NAME + 1];
        int8_t rssi;
        uint8_t hops;
        uint32_t lastSeenMs;
    };

    struct Msg {
        uint16_t from;                  // id do no de ORIGEM (nao do repetidor)
        char fromName[MAX_NAME + 1];    // "" se ainda nao ouvido o BEAT dele
        uint8_t hops;
        int8_t rssi;                    // do ULTIMO salto ouvido
        uint16_t len;
        uint8_t data[MAX_MSG];
    };

    struct Info {
        bool active;
        bool relay;                     // repete quadros de outros (roteador)
        uint16_t node;                  // nosso id (fim da MAC BT)
        uint16_t netId;                 // FNV do nome da rede
        char name[MAX_NAME + 1];
        char net[16];                   // nome da rede (setting)
        uint16_t txQueued;              // quadros proprios na fila
        uint32_t txDropped;             // broadcasts recusados (fila cheia/sem ficha)
        uint32_t rxDropped;             // entregas perdidas (fila RX cheia)
        uint32_t relayed;               // quadros de outros repetidos
        uint16_t heard;                 // nos na tabela de presenca
    };

    // Liga o no (name/net nullptr = mantem o setting atual). relay=false:
    // so escuta/anuncia presenca, nao repete (economia). Persiste
    // "celernet"="1" e, dados, "celernet_name"/"celernet_net".
    static bool start(const char* name, const char* net, bool relay);
    // Desliga e persiste "celernet"="0".
    static bool stop();
    static bool active();

    // Mensagem para TODA a rede (flood com ttl saltos; clamp 1..8). Entra na
    // fila do TX (nao bloqueia); false = malha desligada/fila cheia.
    static bool broadcast(const void* data, size_t len, uint8_t ttl);

    // Mensagem recebida (fila RX_DEPTH, cheia descarta a mais antiga).
    static bool poll(Msg* out);

    // Presenca: nos ouvidos nos ultimos 15 s (mais forte primeiro).
    static int nodes(Node* out, int max);

    static void info(Info* out);

    // Servico ALWAYS (Services.cpp): scanner, bursts, dedup, presenca.
    static void tick();

    // Adv report cru da task do host (CelerLink::onGapEvent e o scanner da
    // malha): barato — testa o magic e enfileira; quase tudo volta no 1o if.
    static void onAdvReport(const uint8_t* data, size_t len, int8_t rssi);

    // O setting pede malha ligada no boot? ("celernet" == "1")
    static bool enabledSetting();
};

#endif  // CELEROS_BT_CELER_NET_H
