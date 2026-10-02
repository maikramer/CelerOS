#ifndef CELER_HARDWARE_BUTTONS_H
#define CELER_HARDWARE_BUTTONS_H

// Botoes fisicos da placa (BoardProfile::buttonPin/buttonPin2; -1 = nao ha,
// tudo abaixo vira no-op), ativo-baixo com pull-up. No watch Waveshare so
// o BOOT (GPIO0) e GPIO; o PWR fala com o AXP2101 (pmuKeyPoll/ScreenPower).
//
//   botao 1 (BOOT): toque curto = home (no app: encerra para o launcher —
//                   pelo mecanismo limpo do X da topbar; no sistema:
//                   Navigator::home; ja na raiz: abre a casa da placa).
//                   segurar ~1,2 s = screenshot BMP.
//   botao 2:        acorda a tela (ScreenPower).
//
// tick() e barato e roda em DOIS contextos: celerLoop (UI do sistema) e
// present() dos apps JS — apps rodam sincronos no loop principal, entao o
// celerLoop congela com um app aberto e o present() e o unico bombeamento
// durante apps. O parametro inApp decide qual acao o curto do botao 1 dispara.

namespace Buttons {

void init();          // celerSetup (no-op sem pinos no perfil)
void tick(bool inApp);  // celerLoop -> false; present() dos apps -> true

}  // namespace Buttons

#endif  // CELER_HARDWARE_BUTTONS_H
