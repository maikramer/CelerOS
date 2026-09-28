#pragma once

#include <string>
#include "esp_http_server.h"

// Autenticacao do servidor web (HTTP Basic). Ate a v1.2 as 11 rotas —
// incluindo POST /update (flash de firmware) e /api/download — aceitavam
// qualquer cliente da LAN; o "CSRF guard" apenas exigia a presenca de um
// header customizado. Agora toda requisicao precisa da senha (usuario
// "admin"), gerada no primeiro boot e guardada em NVS.
//
// A senha e exibida ao dono no app Web Server e no `celerctl info` — o canal
// de recuperacao e o acesso fisico (tela/USB). Plain-text em NVS e um
// trade-off consciente enquanto nao existe NVS encryption (as credenciais
// WiFi estao igual); a ameaca fechada aqui e a remota (LAN): 5 falhas
// travam o acesso por 30 s.
class WebAuth {
public:
    // Carrega a senha do NVS ou gera uma nova no primeiro boot (idempotente).
    static void init();

    // Autoriza a requisicao? Retorna false ja tendo respondido 401/403.
    static bool check(httpd_req_t* req);

    // Senha atual para exibicao ao dono (app Web Server, celerctl info).
    static const char* password();

    // Redefine a senha (6..31 chars). Retorna false fora do intervalo ou se
    // o NVS rejeitar a escrita.
    static bool setPassword(const char* p);

    // Nova senha aleatoria (factory reset): grava no NVS e atualiza o cache.
    static void regenerate();
};
