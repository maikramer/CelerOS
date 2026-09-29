#ifndef CELEROS_BACKLIGHT_H
#define CELEROS_BACKLIGHT_H

#include "Boards/Board.h"

// Controle de brilho do backlight (porte do BrightnessScreen do
// satisfaction-hub, adaptado ao CelerOS). Nivel 5-100 persistido em
// /local/brightness.txt.
//
//   SmartDisplay 4" (S3): tft.setBrightness() do LovyanGFX, que aciona o
//   Light_PWM do GPIO38 — sem tocar em canal LEDC na mao.
//   Placa sem PWM no backlight: no-op; isSupported() = false
class Backlight {
public:
    static void init(CelerDisplay* tft);       // aplica o valor salvo no boot
    static void set(int level, bool persist = true);
    static int get();
    static bool isSupported();

    // Brilho automatico pelo sensor de luz (placa com LDR, ex.: CYD): o nivel
    // do usuario vira o maximo e cai ate 30% no escuro. Persistido em
    // "auto_brightness". tick() e barato (le o sensor 1x/s): celerLoop e
    // present() dos apps chamam.
    static bool autoSupported();
    static bool isAuto();
    static void setAuto(bool on, bool persist = true);
    static void tick();
};

#endif // CELEROS_BACKLIGHT_H
