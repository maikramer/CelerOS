#pragma once

/**
 * @file GestureDetector.h
 * @brief Detector de gestos de touch (swipe, long press, double tap)
 * 
 * Fornece detecção de gestos para objetos LVGL, facilitando
 * interações naturais como swipe para voltar.
 */

#include "lvgl.h"
#include <functional>
#include <cstdint>

namespace ui {

/**
 * @brief Tipos de gesto detectáveis
 */
enum class GestureType {
    SwipeLeft,      ///< Deslizar para a esquerda
    SwipeRight,     ///< Deslizar para a direita
    SwipeUp,        ///< Deslizar para cima
    SwipeDown,      ///< Deslizar para baixo
    LongPress,      ///< Pressão longa
    DoubleTap,      ///< Toque duplo
    Tap             ///< Toque simples (para referência)
};

/**
 * @brief Informações detalhadas do gesto
 */
struct GestureInfo {
    GestureType type;       ///< Tipo do gesto
    int32_t startX;         ///< Posição X inicial
    int32_t startY;         ///< Posição Y inicial
    int32_t endX;           ///< Posição X final
    int32_t endY;           ///< Posição Y final
    int32_t deltaX;         ///< Deslocamento em X
    int32_t deltaY;         ///< Deslocamento em Y
    uint32_t durationMs;    ///< Duração em ms
};

/**
 * @brief Callback para gestos
 */
using GestureCallback = std::function<void(GestureType)>;

/**
 * @brief Callback detalhado para gestos
 */
using DetailedGestureCallback = std::function<void(const GestureInfo&)>;

/**
 * @class GestureDetector
 * @brief Classe para detecção de gestos em objetos LVGL
 * 
 * Exemplo:
 * @code
 * // Detectar swipe em um objeto
 * GestureDetector::attach(myPanel, [](GestureType type) {
 *     if (type == GestureType::SwipeRight) {
 *         Navigator::instance().pop();
 *     }
 * });
 * 
 * // Configurar sensibilidade
 * GestureDetector::setSwipeThreshold(60);  // 60 pixels
 * @endcode
 */
class GestureDetector {
public:
    // ========================================================================
    // Configuração Global
    // ========================================================================
    
    /**
     * @brief Define o limiar de distância para detectar swipe
     * @param pixels Distância mínima em pixels (padrão: 50)
     */
    static void setSwipeThreshold(int32_t pixels);
    
    /**
     * @brief Obtém o limiar de swipe atual
     */
    static int32_t swipeThreshold();
    
    /**
     * @brief Define o tempo mínimo para long press
     * @param ms Tempo em milissegundos (padrão: 500)
     */
    static void setLongPressTime(uint32_t ms);
    
    /**
     * @brief Obtém o tempo de long press atual
     */
    static uint32_t longPressTime();
    
    /**
     * @brief Define o tempo máximo entre toques para double tap
     * @param ms Tempo em milissegundos (padrão: 300)
     */
    static void setDoubleTapTime(uint32_t ms);
    
    /**
     * @brief Obtém o tempo de double tap atual
     */
    static uint32_t doubleTapTime();
    
    // ========================================================================
    // Anexar a Objetos
    // ========================================================================
    
    /**
     * @brief Anexa detector de gestos a um objeto LVGL
     * @param obj Objeto LVGL
     * @param callback Função chamada quando gesto é detectado
     */
    static void attach(lv_obj_t* obj, GestureCallback callback);
    
    /**
     * @brief Anexa detector com informações detalhadas
     * @param obj Objeto LVGL
     * @param callback Função chamada com detalhes do gesto
     */
    static void attachDetailed(lv_obj_t* obj, DetailedGestureCallback callback);
    
    /**
     * @brief Remove detector de gestos de um objeto
     * @param obj Objeto LVGL
     */
    static void detach(lv_obj_t* obj);
    
    // ========================================================================
    // Detecção em Tela Inteira (para swipe-back)
    // ========================================================================
    
    /**
     * @brief Habilita detecção de swipe na borda esquerda da tela
     * 
     * Útil para implementar "swipe to go back".
     * 
     * @param screen Objeto da tela
     * @param edgeWidth Largura da área sensível na borda (padrão: 30px)
     * @param callback Callback quando swipe-right é detectado na borda
     */
    static void enableEdgeSwipe(lv_obj_t* screen, int32_t edgeWidth, GestureCallback callback);
    
    /**
     * @brief Desabilita detecção de swipe na borda
     * @param screen Objeto da tela
     */
    static void disableEdgeSwipe(lv_obj_t* screen);
    
private:
    GestureDetector() = delete;  // Classe estática
};

// ============================================================================
// Conveniência: Converter tipo para string
// ============================================================================

/**
 * @brief Converte tipo de gesto para string legível
 */
inline const char* gestureTypeToString(GestureType type) {
    switch (type) {
        case GestureType::SwipeLeft:  return "SwipeLeft";
        case GestureType::SwipeRight: return "SwipeRight";
        case GestureType::SwipeUp:    return "SwipeUp";
        case GestureType::SwipeDown:  return "SwipeDown";
        case GestureType::LongPress:  return "LongPress";
        case GestureType::DoubleTap:  return "DoubleTap";
        case GestureType::Tap:        return "Tap";
        default:                      return "Unknown";
    }
}

} // namespace ui
