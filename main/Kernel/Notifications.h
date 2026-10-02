#ifndef CELER_KERNEL_NOTIFICATIONS_H
#define CELER_KERNEL_NOTIFICATIONS_H

// Central de notificacoes do sistema (API 15). Antes o historico vivia
// espalhado no binding System.notify; agora o kernel e o dono e o
// consomem: System.notify/notifications (JS), a NotificationCenter nativa,
// o AOD (ponto de nao lidas) e o PhoneLink (notificacoes do celular).
//
// Arquivo /local/notifications.txt, uma por linha (mais antiga primeiro):
//   epoch|titulo|msg|origem|id|lida     (formato da API 12: epoch|titulo|msg)
// Ate MAX entradas. Thread-safe (o PhoneLink chama da task do BLE).

#include <stdint.h>
#include <string>
#include <vector>

namespace Notifications {

constexpr int MAX = 20;

struct Note {
    int64_t epoch = 0;
    std::string title;
    std::string msg;
    std::string src;     // "" = app local; "phone:<app>" = celular
    uint32_t id = 0;     // id externo (Gadgetbridge); 0 = nenhum
    bool read = false;
};

// Nova notificacao: grava, toast, bipe curto e glance da tela (exceto em
// Nao Perturbe). extId != 0 substitui a entrada com o mesmo id.
void push(const std::string& title, const std::string& msg, const std::string& src = "",
          uint32_t extId = 0);

std::vector<Note> list();       // mais recente primeiro
bool removeAt(int index);       // indice de list()
bool removeById(uint32_t extId);
void clear();
int unread();
void markAllRead();

// Usuario dispensou no relogio (removeAt/clear) uma notificacao que veio
// de fora com id (celular): o Phone Link avisa o Gadgetbridge para sumir
// com ela no Android tambem. all = "limpar tudo". Chamado fora do lock.
void setOnDismiss(void (*cb)(uint32_t id, bool all));

// Nao Perturbe (NVS "dnd"): sem som nem glance; o historico segue.
bool dnd();
void setDnd(bool on);

}  // namespace Notifications

#endif  // CELER_KERNEL_NOTIFICATIONS_H
