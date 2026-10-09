#ifndef NETWORK_CLOCK_H
#define NETWORK_CLOCK_H

#include <cstdint>
#include "esp_timer.h"

/**
 * @file NetworkClock.h
 * @brief Relogio interno do componente Network (uptime em segundos).
 *
 * Header privado do componente: NAO use SystemInfo para nao criar
 * dependencia Network -> System.
 */

/**
 * @brief Uptime do sistema em segundos (esp_timer). Base dos timestamps
 *        internos do componente (lastConnected, intervalo de scan,
 *        estatisticas de conexao) — todos eram esp_timer/1000000 direto.
 * @return Segundos desde o boot.
 */
inline uint32_t nowSeconds() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000);
}

#endif // NETWORK_CLOCK_H
