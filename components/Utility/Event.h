#ifndef EVENT_H
#define EVENT_H

#include <functional>
#include <vector>
#include <algorithm>

#ifdef STM32L1
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#elif defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#endif

/**
 * @class Event
 * @brief Manages events and their handlers.
 *
 * This class allows you to register handlers (callbacks) that are called when the event is triggered.
 * Handlers can be added or removed dynamically. The class is thread-safe.
 */
template <typename... Args>
class Event {
public:
    /**
     * @brief Constructor.
     */
    Event();

    /**
     * @brief Destructor.
     */
    ~Event();

    /**
     * @brief Adds a handler to the event.
     * @param handler The function to be called when the event is triggered.
     */
    void addHandler(std::function<void(Args...)> handler);

    /**
     * @brief Removes a handler from the event.
     * @param handler The function to be removed.
     */
    void removeHandler(std::function<void(Args...)> handler);

    /**
     * @brief Triggers the event, calling all registered handlers.
     * @param args Arguments to pass to the handlers.
     */
    void trigger(Args... args);

private:
    std::vector<std::function<void(Args...)>> handlers; /**< List of event handlers */

#ifdef STM32L1
    SemaphoreHandle_t mutex; /**< Mutex to make the class thread-safe */
#elif defined(ESP_PLATFORM)
    SemaphoreHandle_t mutex; /**< Mutex to make the class thread-safe */
#endif
};

#ifdef STM32L1
#define CREATE_MUTEX() xSemaphoreCreateMutex()
#define DELETE_MUTEX(mutex) vSemaphoreDelete(mutex)
#define TAKE_MUTEX(mutex) xSemaphoreTake(mutex, portMAX_DELAY)
#define GIVE_MUTEX(mutex) xSemaphoreGive(mutex)
#elif defined(ESP_PLATFORM)
#define CREATE_MUTEX() xSemaphoreCreateMutex()
#define DELETE_MUTEX(mutex) vSemaphoreDelete(mutex)
#define TAKE_MUTEX(mutex) xSemaphoreTake(mutex, portMAX_DELAY)
#define GIVE_MUTEX(mutex) xSemaphoreGive(mutex)
#endif

/**
 * @brief Constructor.
 */
template <typename... Args>
Event<Args...>::Event() {
    mutex = CREATE_MUTEX();
}

/**
 * @brief Destructor.
 */
template <typename... Args>
Event<Args...>::~Event() {
    if (mutex != NULL) {
        DELETE_MUTEX(mutex);
    }
}

/**
 * @brief Adds a handler to the event.
 * @param handler The function to be called when the event is triggered.
 */
template <typename... Args>
void Event<Args...>::addHandler(std::function<void(Args...)> handler) {
    TAKE_MUTEX(mutex);
    handlers.push_back(handler);
    GIVE_MUTEX(mutex);
}

/**
 * @brief Removes a handler from the event.
 * @param handler The function to be removed.
 */
template <typename... Args>
void Event<Args...>::removeHandler(std::function<void(Args...)> handler) {
    TAKE_MUTEX(mutex);
    handlers.erase(std::remove(handlers.begin(), handlers.end(), handler), handlers.end());
    GIVE_MUTEX(mutex);
}

/**
 * @brief Triggers the event, calling all registered handlers.
 * @param args Arguments to pass to the handlers.
 */
template <typename... Args>
void Event<Args...>::trigger(Args... args) {
    TAKE_MUTEX(mutex);
    for (auto& handler : handlers) {
        handler(args...);
    }
    GIVE_MUTEX(mutex);
}

#endif // EVENT_H
