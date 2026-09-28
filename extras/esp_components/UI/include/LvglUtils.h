#pragma once

/**
 * @file LvglUtils.h
 * @brief Utilitários centralizados para operações LVGL thread-safe
 * 
 * Este módulo fornece funções de lock/unlock para acesso seguro ao LVGL
 * em ambientes multi-thread, além de um guard RAII para garantir
 * que o mutex seja sempre liberado.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace ui {

/**
 * @brief Inicializa o mutex e task handle do LVGL
 * 
 * Deve ser chamado uma vez durante a inicialização do sistema,
 * após criar o mutex e a task do LVGL.
 * 
 * @param mutex Handle do mutex LVGL
 * @param task Handle da task LVGL (para evitar deadlock)
 */
void initLvglMutex(SemaphoreHandle_t mutex, TaskHandle_t task);

/**
 * @brief Adquire o mutex do LVGL
 * 
 * Se chamado da própria task LVGL, não faz nada (evita deadlock).
 * Bloqueia indefinidamente até conseguir o mutex.
 */
void lvgl_lock();

/**
 * @brief Libera o mutex do LVGL
 * 
 * Se chamado da própria task LVGL, não faz nada.
 */
void lvgl_unlock();

/**
 * @brief Guard RAII para lock automático do LVGL
 * 
 * Uso:
 * @code
 * {
 *     ui::LvglGuard guard;
 *     // operações LVGL seguras
 * } // unlock automático
 * @endcode
 */
class LvglGuard {
public:
    LvglGuard();
    ~LvglGuard();
    
    // Não copiável nem movível
    LvglGuard(const LvglGuard&) = delete;
    LvglGuard& operator=(const LvglGuard&) = delete;
    LvglGuard(LvglGuard&&) = delete;
    LvglGuard& operator=(LvglGuard&&) = delete;
    
private:
    bool m_locked;
};

/**
 * @brief Macro para criar um guard com nome único
 * 
 * Uso:
 * @code
 * LVGL_SCOPED_LOCK();
 * // operações LVGL seguras
 * @endcode
 */
#define LVGL_SCOPED_LOCK() ui::LvglGuard _lvgl_guard_##__LINE__

} // namespace ui
