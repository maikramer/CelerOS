//
// Created by maikeu on 22/10/22.
//

#ifndef PROJETO_BETA_LEDBLINKER_H
#define PROJETO_BETA_LEDBLINKER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "Utility.h"

class LedBlinker {
public:
    LedBlinker(int interval_ms, gpio_num_t gpio) {
        LastBlink = xTaskGetTickCount();
        Interval = pdMS_TO_TICKS(interval_ms);
        Utility::SetOutput(gpio, false);
        Gpio = gpio;
    }

    void Update() {
        auto ticks = xTaskGetTickCount();
        if (ticks - LastBlink > Interval) {
            Level = Level == 0x1 ? 0x0 : 0x1;
            gpio_set_level(Gpio, Level);
            LastBlink = ticks;
        }
    }

    int Level = 0x0;
    TickType_t LastBlink;
    TickType_t Interval;
    gpio_num_t Gpio;
};


#endif //PROJETO_BETA_LEDBLINKER_H
