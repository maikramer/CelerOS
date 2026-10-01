#ifndef CELER_BOARDS_WAVESHARE_WATCH_MOTION_H
#define CELER_BOARDS_WAVESHARE_WATCH_MOTION_H

// Servico de movimento do watch (QMI8658): task a ~30 Hz alimentando
// pedometro (roll diario, persiste no NVS) e detector de raise. Implementado
// em Motion.cpp (compilado so para waveshare-watch); a face generica acessa
// pelos hooks do BoardProfile.

#include <stdint.h>

namespace Motion {

// Sobe a task (idempotente). Chamado pelo Board::init do watch.
void start();

// Hook BoardProfile::raisePoll — true uma vez por gesto de levantar o pulso.
bool raisePoll();

// Passos do dia corrente (hook p/ Sensors JS do F4 / watchface).
int32_t steps();

// Hook BoardProfile::imuAccel — ultima amostra da task (cache, sem I2C).
bool accel(float* x, float* y, float* z);

// Hook BoardProfile::imuTemp — leitura on-demand do die (°C).
bool temp(float* c);

// Hook BoardProfile::sleepPrep: persiste os passos AGORA e desliga o IMU
// (deep sleep derruba o bus I2C; sem isso os passos desde a ultima gravacao
// se perdem e o QMI8658 fica queimando corrente dormido).
void prepareSleep();

}  // namespace Motion

#endif  // CELER_BOARDS_WAVESHARE_WATCH_MOTION_H
