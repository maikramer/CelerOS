#ifndef BUTTON_H
#define BUTTON_H

#include <cstdint>
#include "driver/gpio.h"
#include "iot_button.h"
#include "Event.h"

/**
 * @file Button.h
 * @brief C++ wrapper for the ESP-IoT-Solution iot_button component.
 * 
 * This class provides a modern C++ event-driven interface for handling
 * button inputs using the iot_button component from ESP-IoT-Solution.
 * 
 * Features:
 * - Event-driven architecture using Event<Args...>
 * - Support for all iot_button events (click, double-click, long press, etc.)
 * - Automatic debouncing handled by iot_button
 * - Configurable timing parameters
 * 
 * @see https://docs.espressif.com/projects/esp-iot-solution/en/latest/input_device/button.html
 */

/**
 * @enum ButtonEvent
 * @brief Enumeration of all possible button events.
 * 
 * Maps directly to iot_button's button_event_t values.
 */
enum class ButtonEvent {
    PressDown = BUTTON_PRESS_DOWN,          /**< Button pressed down */
    PressUp = BUTTON_PRESS_UP,              /**< Button released */
    PressRepeat = BUTTON_PRESS_REPEAT,      /**< Button pressed repeatedly */
    PressRepeatDone = BUTTON_PRESS_REPEAT_DONE, /**< Repeat pressing finished */
    SingleClick = BUTTON_SINGLE_CLICK,      /**< Single click detected */
    DoubleClick = BUTTON_DOUBLE_CLICK,      /**< Double click detected */
    MultipleClick = BUTTON_MULTIPLE_CLICK,  /**< Multiple clicks detected */
    LongPressStart = BUTTON_LONG_PRESS_START, /**< Long press started */
    LongPressHold = BUTTON_LONG_PRESS_HOLD, /**< Long press being held */
    LongPressUp = BUTTON_LONG_PRESS_UP,     /**< Long press released */
    PressEnd = BUTTON_PRESS_END             /**< Any press event ended */
};

/**
 * @struct ButtonConfig
 * @brief Configuration options for Button creation.
 */
struct ButtonConfig {
    gpio_num_t gpio;            /**< GPIO pin number */
    bool activeLow;             /**< True if button is active low (pressed = LOW) */
    bool enablePullUp;          /**< Enable internal pull-up resistor */
    bool enablePullDown;        /**< Enable internal pull-down resistor */
    uint16_t longPressTimeMs;   /**< Time in ms to trigger long press (default: 1500) */
    uint16_t shortPressTimeMs;  /**< Time in ms for short press detection (default: 180) */
    
    /**
     * @brief Default constructor with sensible defaults.
     */
    ButtonConfig()
        : gpio(GPIO_NUM_NC)
        , activeLow(true)
        , enablePullUp(true)
        , enablePullDown(false)
        , longPressTimeMs(1500)
        , shortPressTimeMs(180) {}
    
    /**
     * @brief Constructor with GPIO and active level.
     * @param gpio GPIO pin number.
     * @param activeLow True if active low (default).
     */
    ButtonConfig(gpio_num_t gpio, bool activeLow = true)
        : gpio(gpio)
        , activeLow(activeLow)
        , enablePullUp(activeLow)     // Pull-up for active low
        , enablePullDown(!activeLow)  // Pull-down for active high
        , longPressTimeMs(1500)
        , shortPressTimeMs(180) {}
};

/**
 * @class Button
 * @brief C++ wrapper for iot_button providing event-driven button handling.
 * 
 * This class wraps the iot_button C API and exposes button events through
 * the Event<Args...> system used in this project.
 * 
 * Example usage:
 * @code
 * Button btn(GPIO_NUM_0, true);  // GPIO 0, active low
 * 
 * btn.onClick.addHandler([](Button* b) {
 *     ESP_LOGI("App", "Button clicked!");
 * });
 * 
 * btn.onLongPressStart.addHandler([](Button* b) {
 *     ESP_LOGI("App", "Long press started!");
 * });
 * @endcode
 */
class Button {
public:
    /**
     * @brief Constructor with GPIO and active level.
     * @param gpio GPIO pin number for the button.
     * @param activeLow True if button is active low (pressed = LOW), default true.
     */
    Button(gpio_num_t gpio, bool activeLow = true);
    
    /**
     * @brief Constructor with full configuration.
     * @param config ButtonConfig structure with all settings.
     */
    explicit Button(const ButtonConfig& config);
    
    /**
     * @brief Destructor. Releases iot_button resources.
     */
    ~Button();
    
    // Prevent copying
    Button(const Button&) = delete;
    Button& operator=(const Button&) = delete;
    
    // ========== Configuration ==========
    
    /**
     * @brief Set the long press time threshold.
     * @param ms Time in milliseconds.
     * @return True if successful.
     */
    bool setLongPressTime(uint16_t ms);
    
    /**
     * @brief Set the short press time threshold.
     * @param ms Time in milliseconds.
     * @return True if successful.
     */
    bool setShortPressTime(uint16_t ms);
    
    /**
     * @brief Register a callback for a specific number of clicks.
     * 
     * This allows detecting triple-click, quadruple-click, etc.
     * 
     * @param clickCount Number of clicks to detect (e.g., 3 for triple-click).
     * @return True if successful.
     */
    bool registerMultipleClick(uint8_t clickCount);
    
    /**
     * @brief Register a callback for a specific long press duration.
     * @param durationMs Duration in milliseconds.
     * @return True if successful.
     */
    bool registerLongPressDuration(uint32_t durationMs);
    
    // ========== State Queries ==========
    
    /**
     * @brief Check if the button is currently pressed.
     * @return True if pressed.
     */
    bool isPressed() const;
    
    /**
     * @brief Get the current repeat count during press-repeat events.
     * @return Number of repeated presses detected.
     */
    uint8_t getRepeatCount() const;
    
    /**
     * @brief Get the elapsed time since the button was pressed.
     * @return Time in milliseconds.
     */
    uint32_t getTicksTime() const;
    
    /**
     * @brief Get the count of long press hold events.
     * @return Number of hold events triggered.
     */
    uint16_t getLongPressHoldCount() const;
    
    /**
     * @brief Get the GPIO pin number.
     * @return GPIO number.
     */
    gpio_num_t getGpio() const { return _config.gpio; }
    
    /**
     * @brief Check if the button was successfully initialized.
     * @return True if valid.
     */
    bool isValid() const { return _handle != nullptr; }
    
    /**
     * @brief Get the underlying iot_button handle (advanced use).
     * @return button_handle_t handle.
     */
    button_handle_t getHandle() const { return _handle; }
    
    // ========== Power Management ==========
    
    /**
     * @brief Stop button processing (for power saving).
     * @return True if successful.
     */
    bool stop();
    
    /**
     * @brief Resume button processing after stop().
     * @return True if successful.
     */
    bool resume();
    
    // ========== Events ==========
    
    /**
     * @brief Event triggered when button is pressed down.
     * Parameter: Button* (this button instance)
     */
    Event<Button*> onPress;
    
    /**
     * @brief Event triggered when button is released.
     * Parameter: Button* (this button instance)
     */
    Event<Button*> onRelease;
    
    /**
     * @brief Event triggered on single click.
     * Parameter: Button* (this button instance)
     */
    Event<Button*> onClick;
    
    /**
     * @brief Event triggered on double click.
     * Parameter: Button* (this button instance)
     */
    Event<Button*> onDoubleClick;
    
    /**
     * @brief Event triggered on multiple clicks.
     * Parameters: Button* (this), uint8_t (click count)
     */
    Event<Button*, uint8_t> onMultipleClick;
    
    /**
     * @brief Event triggered during repeated pressing.
     * Parameters: Button* (this), uint8_t (repeat count)
     */
    Event<Button*, uint8_t> onPressRepeat;
    
    /**
     * @brief Event triggered when repeat pressing ends.
     * Parameters: Button* (this), uint8_t (total repeat count)
     */
    Event<Button*, uint8_t> onPressRepeatDone;
    
    /**
     * @brief Event triggered when long press starts.
     * Parameter: Button* (this button instance)
     */
    Event<Button*> onLongPressStart;
    
    /**
     * @brief Event triggered periodically during long press hold.
     * Parameters: Button* (this), uint32_t (duration in ms)
     */
    Event<Button*, uint32_t> onLongPressHold;
    
    /**
     * @brief Event triggered when long press ends (button released).
     * Parameters: Button* (this), uint32_t (total duration in ms)
     */
    Event<Button*, uint32_t> onLongPressUp;
    
    /**
     * @brief Event triggered at the end of any press sequence.
     * Parameter: Button* (this button instance)
     */
    Event<Button*> onPressEnd;
    
    /**
     * @brief Generic event triggered for any button event.
     * Parameters: Button* (this), ButtonEvent (event type)
     * 
     * Useful for debugging or logging all events.
     */
    Event<Button*, ButtonEvent> onAnyEvent;

private:
    /**
     * @brief Initialize the iot_button instance.
     * @return True if successful.
     */
    bool init();
    
    /**
     * @brief Register all event callbacks with iot_button.
     */
    void registerCallbacks();
    
    /**
     * @brief Static callback function for iot_button.
     * @param arg button_handle_t
     * @param data User data (Button* instance)
     */
    static void buttonCallback(void* arg, void* data);
    
    /**
     * @brief Handle a button event internally.
     * @param event The button event type.
     */
    void handleEvent(button_event_t event);
    
    button_handle_t _handle;    /**< iot_button handle */
    ButtonConfig _config;       /**< Button configuration */
    
    static constexpr const char* TAG = "Button";
};

#endif // BUTTON_H
