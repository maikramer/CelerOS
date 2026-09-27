//
// Created by maikeu on 02/02/2020.
//

#ifndef UTILITY_H
#define UTILITY_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include "string"
#include <hal/gpio_types.h>
#include <driver/gpio.h>
#include <nlohmann/json.hpp>
#include "esp_log.h"
#include "sstream"
#include <list>

class Utility {


public:
    static auto split(const std::string &source, char delimiter) -> std::vector<std::string>;

    static auto CreateAndProfile(const char *taskName, TaskFunction_t function, uint32_t stack,
                                 UBaseType_t priority,
                                 int core,
                                 void *parameter) -> TaskHandle_t;

    static auto StringToByteArray(const std::string &input, uint8_t *output) -> uint32_t;

    static void
    SetInput(gpio_num_t gpioNum, gpio_pullup_t pullUp, gpio_int_type_t intType = GPIO_INTR_DISABLE);

    static void SetOutput(gpio_num_t gpioNum, bool openDrain, uint32_t initial_level = 0);

    static void ListJsonKeys(const nlohmann::json &j);

    static std::string CamelCaseToTitleCase(const std::string &toConvert);

    static uint32_t ReadOutput(gpio_num_t gpio);

    template<typename T>
    static auto GetConvertedFromString(const std::string &str) -> T {
        T out;
        if (str.empty()) {
            ESP_LOGE(__FUNCTION__, "String vazia");
        }
        ESP_LOGI(__FUNCTION__, "Tentando converter %s", str.c_str());
        std::stringstream convert(str);
        convert >> out;

        return out;
    }

    template<typename T>
    struct memfun_type {
        using type = void;
    };

    template<typename Ret, typename Class, typename... Args>
    struct memfun_type<Ret(Class::*)(Args...) const> {
        using type = std::function<Ret(Args...)>;
    };

    template<typename F>
    typename memfun_type<decltype(&F::operator())>::type
    static FFL(F const &func) { // Function from lambda !
        return func;
    }

    static std::string trim(const std::string &str);

    static std::list<uint8_t> StringToByteList(const std::string &input);
};


#endif //UTILITY_H
