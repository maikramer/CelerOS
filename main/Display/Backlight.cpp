#include "Backlight.h"
#include <string>
#include <Arduino.h>
#include "../Utils/CelerSettings.h"
#include "../Utils/StrUtils.h"
#include "../Boards/Board.h"
#include "../Hardware/BoardIO.h"

static CelerDisplay* blTft = nullptr;

static int currentLevel = 100;   // nivel escolhido pelo usuario (persistido)
static int appliedLevel = -1;    // nivel no vidro (auto: fracao do escolhido)
static bool autoOn = false;
static float luxAvg = -1;        // media movel do sensor (0..100)
static uint32_t lastTickMs = 0;

bool Backlight::isSupported() { return Board::profile().backlightPwm; }

int Backlight::get() { return currentLevel; }

static void apply(int level) {
    if (level < 5) level = 5;
    if (level > 100) level = 100;
    if (level == appliedLevel) return;
    appliedLevel = level;
    if (blTft && Board::profile().backlightPwm) {
        blTft->setBrightness((uint8_t)(level * 255 / 100));
    }
}

// Auto: o nivel do usuario e o MAXIMO; no escuro cai ate 30% dele
static int autoTarget() {
    float f = 0.3f + 0.7f * (luxAvg < 0 ? 100 : luxAvg) / 100.0f;
    return (int)(currentLevel * f + 0.5f);
}

void Backlight::set(int level, bool persist) {
    if (level < 5) level = 5;
    if (level > 100) level = 100;
    currentLevel = level;
    apply(autoOn ? autoTarget() : level);
    if (persist) {
        CelerSettings::set("brightness", std::to_string(level).c_str());
    }
}

bool Backlight::autoSupported() { return isSupported() && BoardIO::hasLightSensor(); }

bool Backlight::isAuto() { return autoOn; }

void Backlight::setAuto(bool on, bool persist) {
    autoOn = on && autoSupported();
    luxAvg = -1;
    lastTickMs = 0;
    if (persist) CelerSettings::set("auto_brightness", autoOn ? "1" : "0");
    if (autoOn) {
        tick();  // aplica ja, sem esperar o proximo segundo
    } else {
        apply(currentLevel);
    }
}

void Backlight::tick() {
    if (!autoOn) return;
    uint32_t now = millis();
    if (lastTickMs != 0 && now - lastTickMs < 1000) return;  // 1x/s: ADC barato
    lastTickMs = now;
    int lux = BoardIO::lightLevel();
    if (lux < 0) return;
    // media movel: sombra passageira (mao sobre a tela) nao faz a tela piscar
    luxAvg = luxAvg < 0 ? lux : luxAvg * 0.7f + lux * 0.3f;
    int target = autoTarget();
    // histerese: so muda com diferenca perceptivel
    if (appliedLevel < 0 || abs(target - appliedLevel) >= 4) apply(target);
}

void Backlight::init(CelerDisplay* tft) {
    blTft = tft;
    int lvl = (int)kstr::toInt(CelerSettings::get("brightness", "100"));
    if (lvl < 5 || lvl > 100) lvl = 100;
    set(lvl, false);
    setAuto(CelerSettings::get("auto_brightness", "0") == "1", false);
}
