#ifndef CELEROS_PHONE_SCREENS_H
#define CELEROS_PHONE_SCREENS_H

// Telas do Phone Link (API 15, CONFIG_CELEROS_PHONE_LINK):
//   - PairScreen: o codigo de 6 digitos GRANDE enquanto o celular pareia
//     (some sozinha quando o enlace criptografa ou o pareamento cai);
//   - CallScreen: chamada recebida com Recusar/Atender, tocando ate o
//     celular avisar o fim ou o usuario escolher.
// service() roda no loop da UI; com app aberto o PhoneLink::tick(true) pede
// a saida do app para a tela aparecer (como a AlarmScreen) — o inApp so
// informa o contexto e as telas nao empilham sobre um app.

namespace PhoneScreens {

void service(bool inApp);

}  // namespace PhoneScreens

#endif  // CELEROS_PHONE_SCREENS_H
