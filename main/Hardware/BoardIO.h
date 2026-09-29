#pragma once

#include <stdint.h>

// ============================================================================
// BoardIO — perifericos simples da placa alem da tela: LED RGB de status,
// sensor de luz (LDR) e tom no alto-falante. Os pinos vem do BoardProfile
// (Boards/<placa>/Board.cpp); placa sem o periferico = chamadas viram no-op.
//
// Mapa de canais LEDC (baixa velocidade), unico no firmware:
//   0..2  analogWrite dos apps (Compat)       timers 0/1
//   3..5  LED RGB                              timer 1 (5 kHz, 8 bits)
//   6     tom (System.beep)                    timer 2
//   7     backlight (LovyanGFX Light_PWM)      timer 3 (no ESP32 fica no
//                                              bloco de alta velocidade)
// ============================================================================
namespace BoardIO {

// ---- LED RGB ----
bool hasLed();
// 0..255 por canal; (0,0,0) apaga. No-op sem LED.
void setLed(uint8_t r, uint8_t g, uint8_t b);
// Apaga se alguem acendeu (sem inicializar o LEDC a toa): fim de app JS
void ledOff();

// ---- sensor de luz ----
bool hasLightSensor();
// 0 (escuro) .. 100 (claro); -1 sem sensor. Media de algumas leituras.
int lightLevel();
// Leitura crua do ADC (diagnostico/calibracao); -1 sem sensor.
int lightRaw();

// ---- alto-falante ----
bool hasSpeaker();
// Tom bloqueante (alimenta o watchdog). false sem alto-falante ou fora da faixa.
bool tone(int freqHz, int ms);

}  // namespace BoardIO
