//
// Created by maikeu on 18/08/2021.
//

#include <cstdarg>
#include <cstdio>
#include <esp_log.h>
#include "CrossPlatformUtility.h"


void log_device(bool isError, const char *origin, const char *format, ...) {
#ifdef STM32L1
    if (isError) {
        //		__asm__("BKPT");
    }

#elif defined(ESP_PLATFORM)
    va_list args;
    va_start(args, format);
    static char buf[128];
    vsnprintf(buf, sizeof(buf), format, args);
    if (isError) {
        ESP_LOGE(origin, "%s", buf);
    } else {
        ESP_LOGI(origin, "%s", buf);
    }
    va_end(args);
#endif
}