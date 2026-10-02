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
//                   Placas buttonToApp (devkit headless): o botao e input do
//                   app — curto/longo caem no latch (buttonEvents, consumido
//                   por System.button()) e o longo encerra o app; no launcher
//                   o curto relanca o homeApp.
//   botao 2:        acorda a tela (ScreenPower).
//
// tick() e barato e roda em DOIS contextos: celerLoop (UI do sistema) e
// present() dos apps JS — apps rodam sincronos no loop principal, entao o
// celerLoop congela com um app aberto e o present() e o unico bombeamento
// durante apps. O parametro inApp decide qual acao o curto do botao 1 dispara.

namespace Buttons {

void init();          // celerSetup (no-op sem pinos no perfil)
void tick(bool inApp);  // celerLoop -> false; present() dos apps -> true

// Evento pendente do botao 1 (placas buttonToApp) e consome: 0 nada,
// 1 curto, 2 longo. So e alimentado com um app aberto (present bombeia);
// em placas comuns (botao = home) nao ha latch e sempre devolve 0.
int buttonEvents();

}  // namespace Buttons

#endif  // CELER_HARDWARE_BUTTONS_H
