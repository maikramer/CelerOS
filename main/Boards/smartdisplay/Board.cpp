#include "Boards/Board.h"
#include "../../Hardware/BoardIO.h"

// Perfil da Guition ESP32-S3-4848S040 (SmartDisplay 4").
//
// Material do fabricante (4.0inch_ESP32-4848S040): alto-falante via
// amplificador Nsiway NS4168 em I2S (DOUT=40, BCLK=1, LRC=2, sem MCLK). As
// SKUs "Y" (caixa 86 switch) usam os MESMOS pinos como linhas de rele
// L1/L2/L3 — CELEROS_SMARTDISPLAY_RELAYS troca o alto-falante por N reles.

#define CELEROS_STR2(x) #x
#define CELEROS_STR(x) CELEROS_STR2(x)

namespace {
BoardDisplay s_display;
}  // namespace

namespace Board {

#if defined(CONFIG_CELEROS_SMARTDISPLAY_RELAYS) && CONFIG_CELEROS_SMARTDISPLAY_RELAYS > 0
static const BoardProfile s_profile = {
    .id = "smartdisplay_4in",
    .otaChannel = "smartdisplay_4848S040",
    .name = "smartdisplay_4in (4848S040 +"
        " " CELEROS_STR(CONFIG_CELEROS_SMARTDISPLAY_RELAYS) " reles)",
    .sd = {.cs = 42, .sck = 48, .miso = 41, .mosi = 47, .freqKhz = 4000},  // bus compartilhado com o init do painel
    .hasPsram = true,
    .backlightPwm = true,
    .capacitiveTouch = true,
    .speakerPin = -1,  // pinos 40/2/1 sao rele nesta variante
    .rotation = 0,     // painel quadrado 480x480, sem rotação
    .led = {-1, -1, -1, false},
    .lightSensorPin = -1,
    .i2s = {-1, -1, -1},
    .relay = {.count = CONFIG_CELEROS_SMARTDISPLAY_RELAYS, .pins = {40, 2, 1}},  // L1, L2, L3
};
#else
static const BoardProfile s_profile = {
    .id = "smartdisplay_4in",
    .otaChannel = "smartdisplay_4848S040",
    .name = "smartdisplay_4in (ESP32-S3-4848S040)",
    .sd = {.cs = 42, .sck = 48, .miso = 41, .mosi = 47, .freqKhz = 4000},  // bus compartilhado com o init do painel
    .hasPsram = true,
    .backlightPwm = true,
    .capacitiveTouch = true,
    .speakerPin = -1,  // sem buzzer; o alto-falante e digital (I2S, abaixo)
    .rotation = 0,     // painel quadrado 480x480, sem rotação
    .led = {-1, -1, -1, false},
    .lightSensorPin = -1,
    .i2s = {.dout = 40, .bclk = 1, .lrc = 2},  // NS4168
    .relay = {},
    .servo = {},
    .mic = {},
    .strips = {},  // variante sem rele (padrao)
};
#endif

void init() {
    s_display.init();
    // Arrays RGB565 padrao (icones, BMPs de apps JS) e o readRect (screencap)
    // exigem a conversao de ordem de bytes: o framebuffer nativo do LCD_CAM
    // e 565 com bytes trocados (rgb565_2Byte do LovyanGFX). Sem isso o
    // pushImage copia cru e as cores saem trocadas no vidro.
    s_display.setSwapBytes(true);
    // Variante Y: rele desligado no boot (nivel baixo), como no demo do fab.
    BoardIO::initRelays();
}

CelerDisplay& display() {
    return s_display;
}

const BoardProfile& profile() {
    return s_profile;
}

}  // namespace Board
