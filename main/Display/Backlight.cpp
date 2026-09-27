#include "Backlight.h"
#include <string>
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"
#include "../Boards/Board.h"

static KryonDisplay* blTft = nullptr;
static const char* BRIGHTNESS_FILE = "/local/brightness.txt";

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
        FileSystem::writeTextFile(BRIGHTNESS_FILE, std::to_string(level).c_str());
    }
}

void Backlight::init(KryonDisplay* tft) {
    blTft = tft;
    std::string s = kstr::trim(FileSystem::readTextFile(BRIGHTNESS_FILE));
    int lvl = (int)kstr::toInt(s);
    if (lvl < 5 || lvl > 100) lvl = 100;
    set(lvl, false);
}
