#include "Backlight.h"
#include <string>
#include "../Utils/CelerSettings.h"
#include "../Utils/StrUtils.h"
#include "../Boards/Board.h"

static CelerDisplay* blTft = nullptr;

static int currentLevel = 100;

bool Backlight::isSupported() { return Board::profile().backlightPwm; }

int Backlight::get() { return currentLevel; }

void Backlight::set(int level, bool persist) {
    if (level < 5) level = 5;
    if (level > 100) level = 100;
    currentLevel = level;
    if (blTft && Board::profile().backlightPwm) {
        blTft->setBrightness((uint8_t)(level * 255 / 100));
    }
    if (persist) {
        CelerSettings::set("brightness", std::to_string(level).c_str());
    }
}

void Backlight::init(CelerDisplay* tft) {
    blTft = tft;
    int lvl = (int)kstr::toInt(CelerSettings::get("brightness", "100"));
    if (lvl < 5 || lvl > 100) lvl = 100;
    set(lvl, false);
}
