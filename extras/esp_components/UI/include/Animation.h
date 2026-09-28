#pragma once

/**
 * @file Animation.h
 * @brief Sistema de animações para transições de tela e elementos LVGL
 * 
 * Fornece:
 * - Configuração global de animações
 * - Tipos de transição entre telas
 * - Helpers para animar elementos individuais
 */

#include "lvgl.h"
#include <cstdint>

namespace ui {
namespace anim {

/**
 * @brief Direção para animações de slide
 */
enum class Direction {
    Left,
    Right,
    Up,
    Down
};

/**
 * @brief Tipos de transição entre telas
 */
enum class ScreenTransition {
    None,           ///< Sem animação (troca instantânea)
    FadeIn,         ///< Fade in da nova tela
    FadeOut,        ///< Fade out da tela antiga
    SlideLeft,      ///< Desliza para a esquerda (nova tela vem da direita)
    SlideRight,     ///< Desliza para a direita (nova tela vem da esquerda)
    SlideUp,        ///< Desliza para cima (nova tela vem de baixo)
    SlideDown,      ///< Desliza para baixo (nova tela vem de cima)
    OverLeft,       ///< Nova tela desliza por cima (da direita)
    OverRight,      ///< Nova tela desliza por cima (da esquerda)
    OverUp,         ///< Nova tela desliza por cima (de baixo)
    OverDown        ///< Nova tela desliza por cima (de cima)
};

/**
 * @brief Configuração global de animações
 */
struct Config {
    ScreenTransition defaultEnter = ScreenTransition::FadeIn;   ///< Animação padrão ao entrar
    ScreenTransition defaultExit = ScreenTransition::FadeOut;   ///< Animação padrão ao sair
    uint32_t screenTransitionMs = 200;                          ///< Duração da transição de tela
    uint32_t elementAnimMs = 150;                               ///< Duração padrão para elementos
    bool enabled = true;                                        ///< Habilita/desabilita animações
};

/**
 * @brief Obtém a configuração global de animações
 * @return Referência para a configuração atual
 */
Config& config();

/**
 * @brief Define a configuração global de animações
 * @param cfg Nova configuração
 */
void setConfig(const Config& cfg);

/**
 * @brief Converte ScreenTransition para tipo LVGL
 * @param transition Tipo de transição
 * @return Valor lv_screen_load_anim_t correspondente
 */
lv_screen_load_anim_t toLvglAnim(ScreenTransition transition);

// ============================================================================
// Animações de Elementos Individuais
// ============================================================================

/**
 * @brief Aplica fade in em um elemento
 * @param obj Objeto LVGL
 * @param durationMs Duração em ms (0 = usar padrão)
 * @param delayMs Atraso antes de iniciar
 */
void fadeIn(lv_obj_t* obj, uint32_t durationMs = 0, uint32_t delayMs = 0);

/**
 * @brief Aplica fade out em um elemento
 * @param obj Objeto LVGL
 * @param durationMs Duração em ms (0 = usar padrão)
 * @param delayMs Atraso antes de iniciar
 */
void fadeOut(lv_obj_t* obj, uint32_t durationMs = 0, uint32_t delayMs = 0);

/**
 * @brief Desliza elemento para dentro da tela
 * @param obj Objeto LVGL
 * @param dir Direção de onde vem o elemento
 * @param durationMs Duração em ms (0 = usar padrão)
 * @param delayMs Atraso antes de iniciar
 */
void slideIn(lv_obj_t* obj, Direction dir, uint32_t durationMs = 0, uint32_t delayMs = 0);

/**
 * @brief Desliza elemento para fora da tela
 * @param obj Objeto LVGL
 * @param dir Direção para onde vai o elemento
 * @param durationMs Duração em ms (0 = usar padrão)
 * @param delayMs Atraso antes de iniciar
 */
void slideOut(lv_obj_t* obj, Direction dir, uint32_t durationMs = 0, uint32_t delayMs = 0);

/**
 * @brief Efeito de pulse (escala temporária)
 * @param obj Objeto LVGL
 * @param scale Escala máxima (ex: 1.1 = 110%)
 * @param durationMs Duração total do ciclo
 */
void pulse(lv_obj_t* obj, float scale = 1.1f, uint32_t durationMs = 0);

/**
 * @brief Aplica fade in sequencial aos filhos de um container
 * 
 * Cada filho aparece com um pequeno atraso após o anterior,
 * criando um efeito de "cascata".
 * 
 * @param parent Container pai
 * @param delayBetweenMs Atraso entre cada filho
 * @param durationMs Duração do fade de cada filho
 */
void staggerFadeIn(lv_obj_t* parent, uint32_t delayBetweenMs = 50, uint32_t durationMs = 0);

/**
 * @brief Para todas as animações em um objeto
 * @param obj Objeto LVGL
 */
void stop(lv_obj_t* obj);

} // namespace anim
} // namespace ui
