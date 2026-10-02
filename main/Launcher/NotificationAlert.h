#ifndef CELER_LAUNCHER_NOTIFICATIONALERT_H
#define CELER_LAUNCHER_NOTIFICATIONALERT_H

// Alerta de notificacao em tela cheia (estilo smartwatch, API 15, watch):
// notificacao nova com a tela dormindo (apagada/dim/AOD) acorda o vidro no
// brilho normal e mostra origem, titulo, corpo e hora. Sem toque em ~8 s
// volta a dormir pelo mesmo caminho do glance que expira; toque abre a
// central de notificacoes (WatchPanels). Enquanto o alerta esta na tela,
// notificacao nova so recarrega o conteudo e renova o prazo.
//
// Nas placas sem estados de tela (ScreenPower inativo) e no-op: fica o toast
// de sempre. Com app JS aberto e tela acesa (usuario usando) tambem nao
// interrompe: so o toast. A decisao roda em service(), bombeado pelo
// celerLoop (inApp=false) e pelo present() dos apps (inApp=true).

namespace NotificationAlert {

// Qualquer task (Notifications::push vem da task do BLE).
void request();

void service(bool inApp);

}  // namespace NotificationAlert

#endif  // CELER_LAUNCHER_NOTIFICATIONALERT_H
