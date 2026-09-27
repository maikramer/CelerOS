// Spike de portabilidade: valida LovyanGFX + Display.h (ST7701 RGB + GT911)
// compilando e rodando sobre ESP-IDF 6.1 puro, antes do porte dos demais
// subsistemas. Substituido pelo app_main() real em main.cpp quando o porte
// terminar.

#include "Display/Display.h"

#include <cstdio>

extern "C" void app_main(void) {
    printf("--- KryonOS ESP-IDF spike ---\n");

    KryonDisplay tft;
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("KryonOS", 16, 16, 4);
    tft.drawString("ESP-IDF 6.1 + LovyanGFX", 16, 60, 2);
    tft.drawString("Touch = pintar de verde", 16, 84, 2);

    int count = 0;
    while (true) {
        uint16_t x = 0, y = 0;
        if (tft.getTouch(&x, &y)) {
            tft.fillCircle(x, y, 3, KryonColorRGB(0, 255, 0));
            if ((++count % 32) == 0) {
                printf("touch %d: %u,%u\n", count, x, y);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
