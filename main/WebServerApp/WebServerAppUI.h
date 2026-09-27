#pragma once

#include <Arduino.h>
#include "../Boards/Board.h"

class WebServerAppUI {
private:
    static KryonDisplay *tftInstance;

public:
    static void init(KryonDisplay *tft);
    static void draw();
    static void handleTouch(uint16_t x, uint16_t y);
};
