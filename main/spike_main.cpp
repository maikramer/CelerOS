// Spike de portabilidade: valida LovyanGFX + Display.h (ST7701 RGB + GT911)
// compilando e rodando sobre ESP-IDF 6.1 puro, antes do porte dos demais
// subsistemas. Substituido pelo app_main() real em main.cpp quando o porte
// terminar.

#include "Display/Display.h"

#include "driver/i2c_master.h"
#include <cstdio>
#include "esp_log.h"

// Diagnostico: scan do barramento I2C do touch (SDA=19, SCL=45) com o driver
// oficial — independe da camada LL do LovyanGFX e prova fio/pullup/endereco.
static void i2cScan() {
    i2c_master_bus_config_t bcfg = {};
    bcfg.i2c_port = 0;
    bcfg.sda_io_num = (gpio_num_t)19;
    bcfg.scl_io_num = (gpio_num_t)45;
    bcfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bcfg.glitch_ignore_cnt = 7;
    bcfg.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t err = i2c_new_master_bus(&bcfg, &bus);
    printf("[scan] i2c_new_master_bus: %s\n", esp_err_to_name(err));
    if (err != ESP_OK) return;
    int found = 0;
    for (int addr = 1; addr < 0x7F; addr++) {
        if (i2c_master_probe(bus, addr, 60) == ESP_OK) {
            printf("[scan] ACK 0x%02X%s%s\n", addr,
                   addr == 0x5D ? "  <- GT911" : "",
                   addr == 0x14 ? "  <- GT911 (alt)" : "");
            found++;
        }
    }
    printf("[scan] %d dispositivo(s)\n", found);
    i2c_del_master_bus(bus);
    vTaskDelay(pdMS_TO_TICKS(20));
}

extern "C" void app_main(void) {
    esp_log_level_set("LGFX", ESP_LOG_VERBOSE);
    printf("--- KryonOS ESP-IDF spike ---\n");
    vTaskDelay(pdMS_TO_TICKS(50));  // power-on do GT911

    i2cScan();

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
    int beat = 0;
    while (true) {
        uint16_t x = 0, y = 0;
        if (tft.getTouch(&x, &y)) {
            tft.fillCircle(x, y, 3, KryonColorRGB(0, 255, 0));
            if ((++count % 32) == 0) {
                printf("touch %d: %u,%u\n", count, x, y);
            }
        }
        if ((++beat % 200) == 0) {
            printf("[alive] tick=%d touches=%d\n", beat, count);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
