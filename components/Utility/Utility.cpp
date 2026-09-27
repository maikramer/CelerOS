//
// Created by maikeu on 02/02/2020.
//

#include <vector>
#include <sstream>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>
#include "driver/gpio.h"
#include "Utility.h"


//#define PROFILE_STACK

auto Utility::split(const std::string &source, char delimiter) -> std::vector<std::string> {
    std::vector<std::string> strings;
    std::istringstream iss(source);
    std::string s;

    if (iss.peek() == '{') {
        std::string json;
        std::getline(iss, json);
        strings.push_back(json);
        return strings;
    }

    while (std::getline(iss, s, delimiter)) {
        strings.push_back(trim(s));
    }

    return strings;
} // split

auto Utility::StringToByteArray(const std::string &input, uint8_t *output) -> uint32_t {
    std::copy(input.begin(), input.end(), output);
    return input.size();
}

std::list<uint8_t> Utility::StringToByteList(const std::string &input) {
    unsigned char bytes[input.length()];
    std::list<uint8_t> list;
    auto size = StringToByteArray(input, bytes);
    for (int i = 0; i < size; ++i) {
        list.push_back(bytes[i]);
    }
    return list;
}

auto Utility::CreateAndProfile(const char *taskName, TaskFunction_t function, const uint32_t stack,
                               UBaseType_t priority,
                               int core, void *parameter) -> TaskHandle_t {
    TaskHandle_t handle = nullptr;
    auto res = xTaskCreatePinnedToCore(function, taskName, stack, parameter, priority, &handle,
                                       core);
    configASSERT(handle);
    if (res != pdPASS || handle == nullptr) {
        ESP_LOGE(__FUNCTION__, "Falha ao criar a tarefa \"%s\"", taskName);
    } else {
        ESP_LOGI(__FUNCTION__, "Tarefa \"%s\" criada com sucesso", taskName);
    }

#ifdef PROFILE_STACK
    vTaskDelay(5);
    auto TaskStackSize = uxTaskGetStackHighWaterMark(handle);
    ESP_LOGI(__FUNCTION__, "%s stack: %d\n", taskName, TaskStackSize);
    vTaskDelay(5);
#endif

    return handle;
}

void Utility::SetOutput(gpio_num_t gpioNum, bool openDrain, uint32_t initial_level) {
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    if (openDrain) {
        io_conf.mode = GPIO_MODE_OUTPUT_OD;
    } else {
        io_conf.mode = GPIO_MODE_OUTPUT;
    }
    //bit mask of the pins that you want to set.
    io_conf.pin_bit_mask = ((uint64_t) 1) << gpioNum;
    //disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //disable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_set_level(gpioNum, initial_level);
}

void Utility::SetInput(gpio_num_t gpioNum, gpio_pullup_t pullUp, gpio_int_type_t intType) {
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = intType;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set.
    io_conf.pin_bit_mask = ((uint64_t) 1) << gpioNum;
    //disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = pullUp;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
}

std::string Utility::CamelCaseToTitleCase(const std::string &toConvert) {
    std::stringstream str{};
    for (auto ch: toConvert) {
        if (ch >= 'A' && ch <= 'Z') {
            str << " " << ch;
        } else {
            str << ch;
        }
    }
    return str.str();
}

void Utility::ListJsonKeys(const nlohmann::json &j) {
    int count = 0;
    for ([[maybe_unused]] auto item: j.items()) {
        count++;
    }
    ESP_LOGI(__FUNCTION__, "Numero de Items:%d", count);
    for (const auto &item: j.items()) {
        ESP_LOGI(__FUNCTION__, "Item:%s", item.key().c_str());
    }
}

uint32_t Utility::ReadOutput(gpio_num_t gpio) {
    return gpio_get_level(gpio);
}


/**
 * @brief Remove white space from a string.
 */
std::string Utility::trim(const std::string &str) {
    size_t first = str.find_first_not_of(' ');
    if (std::string::npos == first) return str;
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
} // trim
//auto Utility::CreateAndProfileStatic(const char *taskName, TaskFunction_t function, const uint32_t stack,
//                                     UBaseType_t priority, StackType_t *const pxStackBuffer,
//                                     StaticTask_t *const pxTaskBuffer,
//                                     BaseType_t core, void *parameter) -> TaskHandle_t {
//    xTaskHandle handle;
//    handle = xTaskCreateStaticPinnedToCore(function, taskName, stack, parameter, priority, pxStackBuffer,
//                                           pxTaskBuffer, core);
//    ESP_LOGI(__FUNCTION__, "Tarefa \"%s\" criada com sucesso", taskName);
//
//#ifdef PROFILE_STACK
//    vTaskDelay(5);
//    auto TaskStackSize = uxTaskGetStackHighWaterMark(handle);
//    ESP_LOGI(__FUNCTION__, "%s stack: %d\n", taskName, TaskStackSize);
//    vTaskDelay(5);
//#endif
//
//    return handle;
//}