#ifndef CELEROS_KERNEL_SERVICES_H
#define CELEROS_KERNEL_SERVICES_H

#include <stdint.h>

// Registro UNICO dos servicos que o firmware bombeia a cada volta.
//
// Ate 2026-10 havia DOIS pumps que precisavam ficar em sincronia a mao: o
// celerLoop (UI do sistema, inApp=false) e o JSBindings::present() (pontos
// onde o app JS cede, inApp=true). Adicionar um servico novo exigia lembrar
// dos dois lugares — o NotificationAlert, por exemplo, teve que ser
// bombeado nos dois. Agora a lista ordenada vive em Services.cpp e os dois
// pumps percorrem a mesma tabela com o seu contexto:
//
//   LOOP    — celerLoop: UI do sistema, launcher, telas nativas
//   PRESENT — present(): app JS vivo (delay/getTouch/net/fs... cederam)
//   ALWAYS  — nos dois (recebem inApp para ajustar o comportamento)
//
// Ordem da lista = ordem de execucao nos DOIS contextos (a caminhada so
// pula quem nao tem a flag do contexto). Servicos exclusivos de um lado
// (Navigator, frame do app) ficam fora ou com a flag certa.
namespace CelerServices {

enum Ctx : uint8_t {
    LOOP = 1,
    PRESENT = 2,
    ALWAYS = LOOP | PRESENT,
};

struct Service {
    const char* name;          // diagnostico (log/boot)
    void (*tick)(bool inApp);  // chamado no contexto registrado
    uint8_t ctx;               // LOOP / PRESENT / ALWAYS
};

// celerLoop: inApp=false. Pula servicos PRESENT-only.
void tickLoop();

// JSBindings::present(): inApp=true. Pula servicos LOOP-only.
void tickPresent();

}  // namespace CelerServices

#endif  // CELEROS_KERNEL_SERVICES_H
