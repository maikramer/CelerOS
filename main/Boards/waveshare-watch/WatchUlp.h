#ifndef CELER_BOARDS_WAVESHARE_WATCH_WATCHULP_H
#define CELER_BOARDS_WAVESHARE_WATCH_WATCHULP_H

// Sentinela ULP do deep sleep (programa em ulp/ulp_main.c): liga/acorda o
// coprocessador RISC-V que vigia a tecla PWR do AXP2101, o gesto de raise
// (INT1 do QMI8658 com filtro de janela) e a tensao da celula enquanto os
// nucleos principais dormem. Entrada pelos hooks ulpArm/ulpWake do
// BoardProfile (ScreenPower), sem #ifdef de placa no codigo generico.

namespace WatchUlp {

/// Hook BoardProfile::ulpArm — chamado no ritual do deep sleep, apos o
/// sleepPrep (que desliga o IMU). Retorna 0 se a sentinela nao armou; bit0 =
/// armada; bit1 = o ULP cuida do wake por movimento (a INT1 sai do EXT1).
int arm();

/// Hook BoardProfile::ulpWake — consome o motivo do wake (ULP_WAKE_*)
/// escrito na mailbox pelo ULP. Idempotente: so devolve != 0 uma vez por
/// boot, apenas quando o reset veio de deep sleep com wake do ULP.
int wake();

}  // namespace WatchUlp

#endif  // CELER_BOARDS_WAVESHARE_WATCH_WATCHULP_H
