#ifndef MY_KEYBOARD_H
#define MY_KEYBOARD_H

#include <Arduino.h>
#include <string>
#include "../Display/Display.h"

class MyKeyboard {
public:
    static void init(KryonDisplay *tft);
    static std::string getString(const std::string& initialText, const std::string& promptMsg, int maxLen = 30);

private:
    static KryonDisplay *tftInstance;
    static void drawKeyboard(const std::string& currentText, const std::string& promptMsg, bool caps, int selectedX, int selectedY);
    static void handleTouch(uint16_t x, uint16_t y, std::string &currentText, bool &caps, bool &done);
};

#endif // MY_KEYBOARD_H
