#include "Boards/Board.h"

// Perfil da Guition ESP32-S3-4848S040 (SmartDisplay 4").

namespace {
BoardDisplay s_display;
}  // namespace

namespace Board {

static const BoardProfile s_profile = {
    .id = "smartdisplay_4in",
    .otaChannel = "smartdisplay_4848S040",
    .name = "smartdisplay_4in (ESP32-S3-4848S040)",
    .sd = {.cs = 42, .sck = 48, .miso = 41, .mosi = 47, .freqKhz = 4000},  // bus compartilhado com o init do painel
    .hasPsram = true,
    .backlightPwm = true,
    .capacitiveTouch = true,
};

void init() {
    s_display.init();
    // Arrays RGB565 padrao (icones, BMPs de apps JS) e o readRect (screencap)
    // exigem a conversao de ordem de bytes: o framebuffer nativo do LCD_CAM
    // e 565 com bytes trocados (rgb565_2Byte do LovyanGFX). Sem isso o
    // pushImage copia cru e as cores saem trocadas no vidro.
    s_display.setSwapBytes(true);
}

CelerDisplay& display() {
    return s_display;
}

const BoardProfile& profile() {
    return s_profile;
}

}  // namespace Board
