#include "Boards/Board.h"

// Perfil da Cheap Yellow Display (ESP32-2432S028R).

namespace {
BoardDisplay s_display;
}  // namespace

namespace Board {

static const BoardProfile s_profile = {
    .id = "cyd",
    .otaChannel = "esp32",
    .name = "cyd (ESP32-2432S028R)",
    .sd = {.cs = 15, .sck = 14, .miso = 26, .mosi = 13, .freqKhz = 0},  // HSPI dedicado (0 = default do driver)
    .hasPsram = false,
    .backlightPwm = true,   // GPIO22 via Light_PWM (antes era fixo 100%)
    .capacitiveTouch = false,
};

void init() {
    s_display.init();
}

KryonDisplay& display() {
    return s_display;
}

const BoardProfile& profile() {
    return s_profile;
}

}  // namespace Board
