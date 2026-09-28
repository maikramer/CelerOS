#include "LvglUtils.h"
#include "esp_log.h"

static const char* TAG = "LvglUtils";

namespace ui {

// Variáveis estáticas para o mutex e task
static SemaphoreHandle_t s_lvglMutex = nullptr;
static TaskHandle_t s_lvglTask = nullptr;

void initLvglMutex(SemaphoreHandle_t mutex, TaskHandle_t task) {
    s_lvglMutex = mutex;
    s_lvglTask = task;
    ESP_LOGI(TAG, "LVGL mutex initialized");
}

void lvgl_lock() {
    if (s_lvglMutex == nullptr) {
        return;
    }
    
    // Se estamos na task do LVGL, não precisamos do lock
    if (xTaskGetCurrentTaskHandle() == s_lvglTask) {
        return;
    }
    
    xSemaphoreTake(s_lvglMutex, portMAX_DELAY);
}

void lvgl_unlock() {
    if (s_lvglMutex == nullptr) {
        return;
    }
    
    // Se estamos na task do LVGL, não fizemos lock
    if (xTaskGetCurrentTaskHandle() == s_lvglTask) {
        return;
    }
    
    xSemaphoreGive(s_lvglMutex);
}

// LvglGuard implementation
LvglGuard::LvglGuard() : m_locked(false) {
    if (s_lvglMutex != nullptr && xTaskGetCurrentTaskHandle() != s_lvglTask) {
        xSemaphoreTake(s_lvglMutex, portMAX_DELAY);
        m_locked = true;
    }
}

LvglGuard::~LvglGuard() {
    if (m_locked && s_lvglMutex != nullptr) {
        xSemaphoreGive(s_lvglMutex);
    }
}

} // namespace ui
