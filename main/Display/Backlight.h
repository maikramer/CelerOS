#ifndef KRYONOS_BACKLIGHT_H
#define KRYONOS_BACKLIGHT_H

#include "Boards/Board.h"

// Controle de brilho do backlight (porte do BrightnessScreen do
// satisfaction-hub, adaptado ao KryonOS). Nivel 5-100 persistido em
// /local/brightness.txt.
//
//   SmartDisplay 4" (S3): tft.setBrightness() do LovyanGFX, que aciona o
//   Light_PWM do GPIO38 — sem tocar em canal LEDC na mao.
//   Placa classica: no-op (backlight nao e PWM por la); isSupported() = false
class Backlight {
public:
    static void init(KryonDisplay* tft);       // aplica o valor salvo no boot
    static void set(int level, bool persist = true);
    static int get();
    static bool isSupported();
};

#endif // KRYONOS_BACKLIGHT_H
