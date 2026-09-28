#include "Button.h"
#include "esp_log.h"

Button::Button(gpio_num_t gpio, bool activeLow)
    : _handle(nullptr)
    , _config(gpio, activeLow) {
    init();
}

Button::Button(const ButtonConfig& config)
    : _handle(nullptr)
    , _config(config) {
    init();
}

Button::~Button() {
    if (_handle != nullptr) {
        iot_button_delete(_handle);
        _handle = nullptr;
    }
}

bool Button::init() {
    if (_config.gpio == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Invalid GPIO number");
        return false;
    }

    // Configure button
    button_config_t btn_cfg = {};
    btn_cfg.type = BUTTON_TYPE_GPIO;
    btn_cfg.long_press_time = _config.longPressTimeMs;
    btn_cfg.short_press_time = _config.shortPressTimeMs;
    btn_cfg.gpio_button_config.gpio_num = static_cast<int32_t>(_config.gpio);
    btn_cfg.gpio_button_config.active_level = _config.activeLow ? 0 : 1;
    
    // Create button
    _handle = iot_button_create(&btn_cfg);
    if (_handle == nullptr) {
        ESP_LOGE(TAG, "Failed to create button on GPIO %d", _config.gpio);
        return false;
    }

    ESP_LOGI(TAG, "Button created on GPIO %d (active %s)", 
             _config.gpio, _config.activeLow ? "LOW" : "HIGH");

    // Register all callbacks
    registerCallbacks();

    return true;
}

void Button::registerCallbacks() {
    if (_handle == nullptr) {
        return;
    }

    // Register callback for each event type
    // The callback receives (button_handle_t arg, void* usr_data)
    // We pass 'this' as usr_data and encode the event type in the registration

    // Press down
    iot_button_register_cb(_handle, BUTTON_PRESS_DOWN, buttonCallback, this);
    
    // Press up
    iot_button_register_cb(_handle, BUTTON_PRESS_UP, buttonCallback, this);
    
    // Single click
    iot_button_register_cb(_handle, BUTTON_SINGLE_CLICK, buttonCallback, this);
    
    // Double click
    iot_button_register_cb(_handle, BUTTON_DOUBLE_CLICK, buttonCallback, this);
    
    // Press repeat
    iot_button_register_cb(_handle, BUTTON_PRESS_REPEAT, buttonCallback, this);
    
    // Press repeat done
    iot_button_register_cb(_handle, BUTTON_PRESS_REPEAT_DONE, buttonCallback, this);
    
    // Long press start
    iot_button_register_cb(_handle, BUTTON_LONG_PRESS_START, buttonCallback, this);
    
    // Long press hold
    iot_button_register_cb(_handle, BUTTON_LONG_PRESS_HOLD, buttonCallback, this);
    
    // Long press up
    iot_button_register_cb(_handle, BUTTON_LONG_PRESS_UP, buttonCallback, this);
    
    // Press end
    iot_button_register_cb(_handle, BUTTON_PRESS_END, buttonCallback, this);
}

void Button::buttonCallback(void* arg, void* data) {
    auto* button = static_cast<Button*>(data);
    if (button == nullptr || button->_handle == nullptr) {
        return;
    }

    // Get the current event from the button handle
    button_event_t event = iot_button_get_event(static_cast<button_handle_t>(arg));
    button->handleEvent(event);
}

void Button::handleEvent(button_event_t event) {
    // Trigger the generic event first
    onAnyEvent.trigger(this, static_cast<ButtonEvent>(event));

    // Trigger specific events
    switch (event) {
        case BUTTON_PRESS_DOWN:
            ESP_LOGD(TAG, "GPIO %d: PRESS_DOWN", _config.gpio);
            onPress.trigger(this);
            break;

        case BUTTON_PRESS_UP:
            ESP_LOGD(TAG, "GPIO %d: PRESS_UP", _config.gpio);
            onRelease.trigger(this);
            break;

        case BUTTON_SINGLE_CLICK:
            ESP_LOGD(TAG, "GPIO %d: SINGLE_CLICK", _config.gpio);
            onClick.trigger(this);
            break;

        case BUTTON_DOUBLE_CLICK:
            ESP_LOGD(TAG, "GPIO %d: DOUBLE_CLICK", _config.gpio);
            onDoubleClick.trigger(this);
            break;

        case BUTTON_MULTIPLE_CLICK: {
            uint8_t count = getRepeatCount();
            ESP_LOGD(TAG, "GPIO %d: MULTIPLE_CLICK (%d)", _config.gpio, count);
            onMultipleClick.trigger(this, count);
            break;
        }

        case BUTTON_PRESS_REPEAT: {
            uint8_t count = getRepeatCount();
            ESP_LOGD(TAG, "GPIO %d: PRESS_REPEAT (%d)", _config.gpio, count);
            onPressRepeat.trigger(this, count);
            break;
        }

        case BUTTON_PRESS_REPEAT_DONE: {
            uint8_t count = getRepeatCount();
            ESP_LOGD(TAG, "GPIO %d: PRESS_REPEAT_DONE (%d)", _config.gpio, count);
            onPressRepeatDone.trigger(this, count);
            break;
        }

        case BUTTON_LONG_PRESS_START:
            ESP_LOGD(TAG, "GPIO %d: LONG_PRESS_START", _config.gpio);
            onLongPressStart.trigger(this);
            break;

        case BUTTON_LONG_PRESS_HOLD: {
            uint32_t duration = getTicksTime();
            ESP_LOGD(TAG, "GPIO %d: LONG_PRESS_HOLD (%lu ms)", _config.gpio, 
                     static_cast<unsigned long>(duration));
            onLongPressHold.trigger(this, duration);
            break;
        }

        case BUTTON_LONG_PRESS_UP: {
            uint32_t duration = getTicksTime();
            ESP_LOGD(TAG, "GPIO %d: LONG_PRESS_UP (%lu ms)", _config.gpio, 
                     static_cast<unsigned long>(duration));
            onLongPressUp.trigger(this, duration);
            break;
        }

        case BUTTON_PRESS_END:
            ESP_LOGD(TAG, "GPIO %d: PRESS_END", _config.gpio);
            onPressEnd.trigger(this);
            break;

        default:
            ESP_LOGW(TAG, "GPIO %d: Unknown event %d", _config.gpio, event);
            break;
    }
}

// ========== Configuration ==========

bool Button::setLongPressTime(uint16_t ms) {
    if (_handle == nullptr) {
        return false;
    }
    
    esp_err_t err = iot_button_set_param(_handle, BUTTON_LONG_PRESS_TIME_MS, reinterpret_cast<void*>(ms));
    if (err == ESP_OK) {
        _config.longPressTimeMs = ms;
        return true;
    }
    
    ESP_LOGE(TAG, "Failed to set long press time: %s", esp_err_to_name(err));
    return false;
}

bool Button::setShortPressTime(uint16_t ms) {
    if (_handle == nullptr) {
        return false;
    }
    
    esp_err_t err = iot_button_set_param(_handle, BUTTON_SHORT_PRESS_TIME_MS, reinterpret_cast<void*>(ms));
    if (err == ESP_OK) {
        _config.shortPressTimeMs = ms;
        return true;
    }
    
    ESP_LOGE(TAG, "Failed to set short press time: %s", esp_err_to_name(err));
    return false;
}

bool Button::registerMultipleClick(uint8_t clickCount) {
    if (_handle == nullptr || clickCount < 2) {
        return false;
    }

    // Use iot_button_register_event for custom click counts
    button_event_config_t event_cfg = {};
    event_cfg.event = BUTTON_MULTIPLE_CLICK;
    event_cfg.event_data.multiple_clicks.clicks = clickCount;

    esp_err_t err = iot_button_register_event_cb(_handle, event_cfg, buttonCallback, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register %d-click event: %s", clickCount, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Registered %d-click event on GPIO %d", clickCount, _config.gpio);
    return true;
}

bool Button::registerLongPressDuration(uint32_t durationMs) {
    if (_handle == nullptr) {
        return false;
    }

    // Use iot_button_register_event for custom long press duration
    button_event_config_t event_cfg = {};
    event_cfg.event = BUTTON_LONG_PRESS_START;
    event_cfg.event_data.long_press.press_time = durationMs;

    esp_err_t err = iot_button_register_event_cb(_handle, event_cfg, buttonCallback, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register long press duration %lu ms: %s", 
                 static_cast<unsigned long>(durationMs), esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Registered long press duration %lu ms on GPIO %d", 
             static_cast<unsigned long>(durationMs), _config.gpio);
    return true;
}

// ========== State Queries ==========

bool Button::isPressed() const {
    if (_handle == nullptr) {
        return false;
    }
    
    // Get button level and compare with active level
    uint8_t level = iot_button_get_key_level(_handle);
    return (_config.activeLow ? (level == 0) : (level == 1));
}

uint8_t Button::getRepeatCount() const {
    if (_handle == nullptr) {
        return 0;
    }
    return iot_button_get_repeat(_handle);
}

uint32_t Button::getTicksTime() const {
    if (_handle == nullptr) {
        return 0;
    }
    return iot_button_get_ticks_time(_handle);
}

uint16_t Button::getLongPressHoldCount() const {
    if (_handle == nullptr) {
        return 0;
    }
    return iot_button_get_long_press_hold_cnt(_handle);
}

// ========== Power Management ==========

bool Button::stop() {
    if (_handle == nullptr) {
        return false;
    }
    
    esp_err_t err = iot_button_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop button: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Button stopped");
    return true;
}

bool Button::resume() {
    if (_handle == nullptr) {
        return false;
    }
    
    esp_err_t err = iot_button_resume();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resume button: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Button resumed");
    return true;
}
