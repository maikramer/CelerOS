#pragma once

#include <stdint.h>

// ============================================================================
// BoardIO — perifericos simples da placa alem da tela: LED RGB de status,
// sensor de luz (LDR), alto-falante e reles. Os pinos vem do BoardProfile
// (Boards/<placa>/Board.cpp); placa sem o periferico = chamadas viram no-op.
//
// Mapa de canais LEDC (baixa velocidade), unico no firmware:
//   0..2  analogWrite dos apps (Compat)       timers 0/1
//   0..2+7+3  servos do System.gpio.servo     timer 0 (50 Hz; 5 canais — o
//            5o (LEDC 3) conflita com o R do LED RGB na CYD se ambos usados)
//   4..5  LED RGB                              timer 1 (5 kHz, 8 bits)
//   6     tom (System.beep na CYD, LEDC)       timer 2
//   7     backlight (LovyanGFX Light_PWM)      timer 3 (no ESP32 fica no
//                                              bloco de alta velocidade)
//   servos: os canais acima que a placa NAO usa (7, 6, 5..3), depois 0..2;
//           timer 2 (timer 0 em placa com buzzer LEDC). Ver servoWrite.
// A SmartDisplay nao usa LEDC para som: o amplificador NS4168 e digital,
// alimentado por I2S (canal alocado sob demanda durante o tom).
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

// ---- reles ----
// Configura as saidas em nivel baixo (desligadas) no boot; no-op sem reles.
void initRelays();
// Quantas linhas de rele a placa tem (0..3).
int relayCount();
// n e 1-based (1 = L1). Liga/desliga; devolve false sem rele ou indice errado.
bool setRelay(int n, bool on);
// Estado atual (ultima escrita); -1 sem rele ou indice errado.
int relayState(int n);

// ---- bateria ----
// Tensao da BATERIA em mV (leitura do divisor ja escalada pelo
// batteryScalePct do perfil; media de leituras, cache ~2 s).
// -1 = placa sem divisor (batteryPin < 0) ou ADC sem calibracao.
int batteryMv();

// ---- servos (PWM 50 Hz, LEDC) ----
// Pulso em us (clamp 400..2600) no pino; o canal e alocado na primeira
// escrita. false = pino invalido ou canais esgotados (ate 5).
bool servoWrite(int pin, int us);
// Para o PWM e libera o canal (servo sem forca); false se o pino nao e servo.
bool servoOff(int pin);
// Solta todos (fim de app JS: servo nao fica segurando forca sem dono).
void servosOff();

// ---- fitas WS2812 (RMT, sem DMA) ----
bool hasStrips();
// strip e 0-based; cores em 0x00RRGGBB, n <= 8 por fita. false sem fitas
// ou parametros invalidos. Atualizacao bloqueante curta (~1 ms).
bool neopixelSet(int strip, const uint32_t* colors, int n);
// Apaga as fitas se algum app acendeu (fim de app JS, como ledOff).
void stripsOff();

// ---- microfone I2S ----
// Nivel de som 0..100 (RMS de uma captura curta, slot esquerdo).
// -1 = placa sem microfone (mic.ws < 0). Primeira chamada inicializa o
// canal I2S1 (~300 ms); as seguintes sao rapidas (~100 ms de audio).
int micLevel();

// ---- pad capacitivo avulso ----
// 1 = tocado, 0 = solto; -1 = placa sem pad (touchPad < 0). A referencia
// e calibrada na primeira chamada (manter o pad solto nesse instante).
int touchPad();

}  // namespace BoardIO
