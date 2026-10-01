#ifndef CELER_BOARDS_BOARD_H
#define CELER_BOARDS_BOARD_H

#include <stdint.h>
#include <time.h>  // struct tm dos hooks readRtc/writeRtc
#include "../Display/Display.h"
#include "BoardDisplay.h"  // resolve para main/Boards/<placa>/ (include path do CMake)

// ============================================================================
// HAL de placas do CelerOS.
//
// O build seleciona exatamente uma placa (-DCELEROS_BOARD=<smartdisplay|cyd>);
// o CMake compila Boards/<placa>/Board.cpp e coloca o diretorio no include
// path. Nenhum outro arquivo do firmware pode conter #ifdef de placa —
// caracteristicas diferentes vivem no BoardProfile abaixo.
// ============================================================================

struct SdConfig {
    int cs, sck, miso, mosi;
    int freqKhz;
    // Host SPI do slot (-1 = SPI2, o classico das placas RGB). Placas cujo
    // display toma o SPI2 (ex.: QSPI do watch) apontam o SPI3 aqui.
    int spiHost = -1;
};

// LED RGB de status (PWM). r < 0 = placa sem LED.
struct RgbLedPins {
    int r, g, b;
    bool activeLow;  // catodo no GPIO (acende em nivel baixo)
};

// Saida de audio digital I2S para amplificador na placa (NS4168 na
// SmartDisplay 4848S040: 16-bit stereo, sem MCLK; ES8311 do watch: 16 kHz
// com MCLK 256x). dout < 0 = sem I2S.
struct AudioI2sPins {
    int dout = -1;
    int bclk = -1;
    int lrc = -1;
    int mclk = -1;   // codec que precisa de MCLK do pino (ES8311: 4,096 MHz)
};

// Rele(s) da placa (linhas L1..L3 da SmartDisplay 4848S040 variante Y,
// que substituem o I2S: mesmos pinos). relayCount = 0 = sem reles.
struct RelayConfig {
    int count = 0;
    int pins[3] = {-1, -1, -1};  // L1, L2, L3 (ativa em nivel alto)
};

// Servos PWM 50 Hz (System.servo mapeia pin->canal LEDC). count = 0 = nenhum;
// pins < 0 = canal do perfil nao soldado (ex.: cauda opcional do cao robotico).
struct ServoPins {
    int count = 0;
    int pins[5] = {-1, -1, -1, -1, -1};
};

// Microfone digital I2S (RX, slot esquerdo). ws < 0 = sem microfone.
// ATENCAO (cao ZZPET): o pino do clock NUNCA pode ser usado como ADC —
// reconfigura-lo como canal analogico desconecta a matriz GPIO e mata o mic.
struct MicI2sPins {
    int ws = -1;
    int bck = -1;
    int din = -1;
};

// Fitas WS2812 (RMT, sem DMA). count = 0 = sem fitas.
struct LedStrips {
    int count = 0;
    int pins[2] = {-1, -1};
    int ledsPerStrip = 0;  // max 8 (RMT sem DMA: 24 simbolos por LED)
};

struct BoardProfile {
    const char* id;          // "smartdisplay_4in" / "cyd"
    const char* otaChannel;  // canal de updates (updates/<canal>/)
    const char* name;        // nome amigavel (shell/info)
    SdConfig sd;
    bool hasPsram;
    bool backlightPwm;       // brilho controlavel
    bool capacitiveTouch;    // sem calibracao interativa
    int speakerPin;          // buzzer passivo p/ System.beep (LEDC); -1 = nao ha
    int rotation;            // rotação fixa do painel no boot (0-3; CYD clássica = 3, landscape)
    RgbLedPins led;          // LED RGB (System.led); r < 0 = nao ha
    int lightSensorPin;      // LDR no ADC (System.lightLevel, brilho automatico); -1 = nao ha
    AudioI2sPins i2s;        // amplificador I2S (System.beep); dout < 0 = nao ha
    RelayConfig relay;       // reles da placa (System.relay); count = 0 = nao ha
    ServoPins servo;         // servos da placa (perfil p/ apps de robo); count = 0 = nao ha
    MicI2sPins mic;          // microfone I2S (System.micLevel); ws < 0 = nao ha
    LedStrips strips;        // fitas WS2812 (System.neopixel); count = 0 = nao ha
    int batteryPin = -1;     // divisor de bateria no ADC (System.battery); -1 = nao ha
    int batteryScalePct = 100;  // mV da bateria = mV no pino * scale / 100 (divisor 2:1 = 200)
    int touchPad = -1;       // pad capacitivo avulso (System.touchPad); -1 = nao ha
    // Hooks de hardware opcional da placa (nullptr = nao ha). Padrao usado
    // pelos campos acima: diferenca de placa vive no perfil, nunca em #ifdef.
    bool (*readRtc)(struct tm&) = nullptr;        // RTC externo (PCF85063)
    bool (*writeRtc)(const struct tm&) = nullptr; // gravar apos NTP/manual
    bool (*readBatteryMv)(int* mv) = nullptr;     // bateria por PMU (AXP2101),
                                                  // antes do caminho ADC
    // Botoes fisicos (ativo-baixo, pull-up; -1 = nao ha). Botao 1: curto =
    // home/encerra app, segurar ~1,2 s = screenshot. Botao 2: acorda a tela.
    int buttonPin = -1;
    int buttonPin2 = -1;
    bool (*raisePoll)() = nullptr;                // gesto "levantar o pulso"
                                                  // (consumivel: true 1x/gesto)
    // Sono REAL do painel (ScreenPower): SLPIN/SLPOUT do AMOLED do watch.
    // screenSleep != nullptr liga a maquina de estados de tela (dim/AOD/off).
    void (*screenSleep)() = nullptr;
    void (*screenWake)() = nullptr;
    // IMU (Sensors.* do runtime JS, API 13). imuAccel != nullptr marca
    // "tem IMU" no getInfo().
    bool (*imuAccel)(float* x, float* y, float* z) = nullptr;  // cache, em g
    int32_t (*imuSteps)() = nullptr;                           // passos do dia
    bool (*imuTemp)(float* c) = nullptr;                       // die, °C
    // Audio com codec I2C (ES8311 do watch): wake/sleep em volta do tom
    // (o codec dorme de verdade entre beeps) + PA do amp.
    int audioPaPin = -1;
    bool (*audioCodecWake)() = nullptr;
    void (*audioCodecSleep)() = nullptr;
    // Ritual pre-deep-sleep da placa (persistir estado, desligar IMU...).
    void (*sleepPrep)() = nullptr;
    // App que abre sozinho no boot (ex.: a cara do cao robotico). nullptr =
    // launcher normal. /local/autostart.txt tem precedencia sobre este campo.
    const char* homeApp = nullptr;
};

// Display concreto da placa (BoardDisplay, de Boards/<placa>/BoardDisplay.h).
using CelerDisplay = BoardDisplay;

namespace Board {

// Inicializa o display da placa (chamar uma vez no boot, antes da UI).
void init();

// Display global (valido apos init(); a instancia vive no Board.cpp da placa)
CelerDisplay& display();

const BoardProfile& profile();

}  // namespace Board

#endif  // CELER_BOARDS_BOARD_H
