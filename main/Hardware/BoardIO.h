#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// BoardIO — perifericos simples da placa alem da tela: LED RGB de status,
// sensor de luz (LDR), alto-falante e reles. Os pinos vem do BoardProfile
// (Boards/<placa>/Board.cpp); placa sem o periferico = chamadas viram no-op.
//
// Mapa de canais LEDC (baixa velocidade), unico no firmware:
//   0..2  analogWrite dos apps (Compat)       timers 0/1
//   servos do System.gpio.servo: ver abaixo (50 Hz; ate 5 canais)
//   4..5  LED RGB                              timer 1 (5 kHz, 8 bits)
//   6     tom (System.beep na CYD, LEDC)       timer 2
//   7     backlight (LovyanGFX Light_PWM)      timer 3 (no ESP32 fica no
//                                              bloco de alta velocidade)
//   servos: os canais acima que a placa NAO usa (7, 6, 5..3), depois 0..2
//           (reservados no Compat: celerLedcClaim); timer 2, ou timer 3 em
//           placa com buzzer LEDC (ESP32: o backlight fica na alta
//           velocidade). Ver servoWrite/servoTimer.
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
// Carga em % (0..100): fuel gauge do PMU quando a placa tem; senao estimada
// pela curva de descarga LiPo a partir de batteryMv(). -1 = sem bateria.
int batteryPct();
// Estado de energia (cache ~2 s): bit0 carregando, bit1 USB/VBUS presente,
// bit2 carga completa. 0 em placa sem PMU; -1 = leitura falhou.
int chargeState();

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
// Placa com codec (ES8311 do watch): o ADC do codec e religado a cada
// leitura (ele dorme apos cada beep) — clocks vem do proprio I2S1 (MCLK).
int micLevel();

// ---- gravacao de microfone (Mic.* do runtime, API 19) ----
// Captura 16 kHz mono 16-bit (slot L do mesmo canal do micLevel) numa task
// propria: o app segue desenhando enquanto grava. Sem microfone na placa
// (mic.ws < 0) ou sem RAM: start devolve false. maxMs: 200..10000 (10 s
// = 320 KB de PCM, que caem na PSRAM das placas com mic).
bool micRecStart(int maxMs);
// true enquanto a task captura (false no teto de maxMs, com buffer intacto)
bool micRecActive();
// Marca que o I2S0 (tom/playWav) emprestou os pinos bclk/ws/mclk do canal do
// mic e os devolveu soltos no GPIO matrix — o proximo uso recria o I2S1 para
// re-rotear (placas com codec compartilham os clocks entre os dois canais).
void micPinsDirty();
// RMS 0..100 do ultimo chunk (mesma escala do micLevel); -1 = nao gravando
int micRecLevel();
// Descarta a gravacao em curso (exit do app inclusive)
void micRecCancel();
// Encerra e devolve o WAV (header de 44 bytes + PCM) ou o base64 dele;
// buffer malloc do chamador (free). *msOut = duracao gravada. null = nada.
char* micRecStop(bool base64, size_t* lenOut, uint32_t* msOut);

// ---- volume (System.setVolume, API 13) ----
// 0..100 persistido em "volume" (default 100). I2S: escala digital da
// senoide (NS4168/ES8311); codec do watch tambem recebe o registrador de
// volume. Buzzer LEDC: no-op (ganho fixo).
int volumePct();
void setVolumePct(int pct, bool persist = true);

// ---- pad capacitivo avulso ----
// 1 = tocado, 0 = solto; -1 = placa sem pad (touchPad < 0). A referencia
// e calibrada na primeira chamada (manter o pad solto nesse instante).
int touchPad();

}  // namespace BoardIO
