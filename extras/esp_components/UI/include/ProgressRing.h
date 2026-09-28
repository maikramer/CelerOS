#pragma once

/**
 * @file ProgressRing.h
 * @brief Anel de progresso circular com suporte a modo indeterminado
 */

#include "lvgl.h"

namespace ui {

/**
 * @class ProgressRing
 * @brief Indicador de progresso circular
 * 
 * Exemplo:
 * @code
 * ProgressRing ring(parent, 60);  // 60px de diâmetro
 * ring.setValue(75);              // 75%
 * 
 * // Modo loading
 * ring.setIndeterminate(true);
 * @endcode
 */
class ProgressRing {
public:
    /**
     * @brief Cria um anel de progresso
     * @param parent Objeto pai LVGL
     * @param diameter Diâmetro em pixels (padrão: 60)
     */
    ProgressRing(lv_obj_t* parent, int32_t diameter = 60);
    ~ProgressRing();
    
    /**
     * @brief Define o valor do progresso
     * @param percent Valor de 0 a 100
     */
    void setValue(int32_t percent);
    
    /**
     * @brief Obtém o valor atual
     */
    int32_t getValue() const { return m_value; }
    
    /**
     * @brief Define a cor do anel
     */
    void setColor(lv_color_t color);
    
    /**
     * @brief Define a cor de fundo do anel
     */
    void setBackgroundColor(lv_color_t color);
    
    /**
     * @brief Define a espessura do anel
     * @param width Espessura em pixels
     */
    void setLineWidth(int32_t width);
    
    /**
     * @brief Habilita/desabilita modo indeterminado (loading)
     */
    void setIndeterminate(bool enabled);
    
    /**
     * @brief Verifica se está em modo indeterminado
     */
    bool isIndeterminate() const { return m_indeterminate; }
    
    /**
     * @brief Mostra/esconde label de porcentagem no centro
     */
    void showPercentLabel(bool show);
    
    /**
     * @brief Define texto customizado no centro
     */
    void setCenterText(const char* text);
    
    /**
     * @brief Obtém o objeto principal
     */
    lv_obj_t* object() const { return m_container; }
    
private:
    lv_obj_t* m_container = nullptr;
    lv_obj_t* m_bar = nullptr;
    lv_obj_t* m_label = nullptr;
    lv_anim_t m_anim;
    int32_t m_value = 0;
    int32_t m_diameter = 60;
    bool m_indeterminate = false;
    bool m_showPercent = false;
    
    void createBar();
    void updateLabel();
    void startIndeterminateAnimation();
    void stopIndeterminateAnimation();
};

} // namespace ui
