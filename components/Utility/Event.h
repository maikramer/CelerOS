#ifndef EVENT_H
#define EVENT_H

#include <functional>
#include <vector>
#include <algorithm>
#include <cstdint>

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
 *
 * Contrato (convencao do CelerOS): handlers sao RAPIDOS — so setam flags /
 * copiam valores. O trigger() tira um SNAPSHOT da lista sob o mutex e invoca
 * os handlers FORA do lock: handler que chame addHandler/removeHandler (ou
 * dispare o mesmo evento de volta) nao deadlocka mais. Custo: uma copia do
 * vector por trigger (eventos de rede sao raros; nada disso roda em loop
 * apertado).
 *
 * removeHandler(id) usa o id devolvido pelo addHandler — std::function nao
 * tem operator== em C++17, e a variante antiga "por valor" nunca compilou
 * (ninguem a instanciou). Um handler removido DURANTE um trigger em curso
 * ainda roda NAQUELE disparo (o snapshot ja foi tirado).
 */
template <typename... Args>
class Event {
public:
    using Handler = std::function<void(Args...)>;
    using HandlerId = uint32_t;  // 0 = invalido

    Event();
    ~Event();

    /**
     * @brief Adds a handler to the event.
     * @param handler The function to be called when the event is triggered.
     * @return HandlerId to use with removeHandler (0 se o registro falhou).
     */
    HandlerId addHandler(Handler handler);

    /**
     * @brief Removes a handler by the id returned by addHandler.
     */
    void removeHandler(HandlerId id);

    /**
     * @brief Triggers the event, calling all registered handlers.
     * @param args Arguments to pass to the handlers.
     */
    void trigger(Args... args);

private:
    struct Entry {
        HandlerId id;
        Handler fn;
    };
    std::vector<Entry> handlers;  /**< List of event handlers */
    HandlerId nextId = 1;

#if defined(STM32L1) || defined(ESP_PLATFORM)
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
 */
template <typename... Args>
typename Event<Args...>::HandlerId Event<Args...>::addHandler(Handler handler) {
    if (!handler) return 0;
    HandlerId id = 0;
    TAKE_MUTEX(mutex);
    // teto defensivo: registro em rajada nao cresce sem controle
    if (handlers.size() < 16) {
        id = nextId++;
        handlers.push_back({id, std::move(handler)});
    }
    GIVE_MUTEX(mutex);
    return id;
}

/**
 * @brief Removes a handler by id (ignora id desconhecido — id 0 e no-op).
 */
template <typename... Args>
void Event<Args...>::removeHandler(HandlerId id) {
    if (id == 0) return;
    TAKE_MUTEX(mutex);
    handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                  [id](const Entry& e) { return e.id == id; }),
                   handlers.end());
    GIVE_MUTEX(mutex);
}

/**
 * @brief Triggers the event: snapshot sob lock, invocacao FORA do lock.
 */
template <typename... Args>
void Event<Args...>::trigger(Args... args) {
    // copia local: o vector pode mudar (add/remove) enquanto os handlers rodam
    std::vector<Handler> snapshot;
    snapshot.reserve(handlers.size());
    TAKE_MUTEX(mutex);
    for (const Entry& e : handlers) snapshot.push_back(e.fn);
    GIVE_MUTEX(mutex);
    for (Handler& h : snapshot) {
        h(args...);
    }
}

#endif // EVENT_H
