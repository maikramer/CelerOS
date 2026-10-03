#ifndef CELER_BOARDS_SPOTPEAR_DOG_DOGULP_H
#define CELER_BOARDS_SPOTPEAR_DOG_DOGULP_H

// Watchdog de bateria por ULP no deep sleep (programa em ulp/ulp_main.c):
// com o cao dormindo (System.deepSleep), o coprocessador vigia o divisor
// do ADC1 e acorda os nucleos se a celula cair do limiar — o timer do JS
// segue valendo como wake normal.

namespace DogUlp {

/// Hook BoardProfile::ulpArm — chamado pelo ritual do System.deepSleep.
/// Configura o ADC para o ULP, calibra o limiar cru e sobe o programa.
int arm();

/// Motivo do wake (1 = bateria fraca), consumido uma vez por boot no
/// Board::init da placa (o cao nao tem ScreenPower — o log fica para o
/// celerctl logcat e a cara do cao mostra a bateria).
int wake();

}  // namespace DogUlp

#endif  // CELER_BOARDS_SPOTPEAR_DOG_DOGULP_H
