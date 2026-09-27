#pragma once

/**
 * @file Gauge.h
 * @brief Medidor/gauge para exibição de valores numéricos
 */

#include "lvgl.h"

namespace ui {

/**
 * @class Gauge
 * @brief Medidor visual com valor, label e unidade
 * 
 * Exemplo:
 * @code
 * Gauge temp(parent);
 * temp.setRange(0, 100);
 * temp.setValue(42);
 * temp.setLabel("Temperatura");
 * temp.setUnit("°C");
 * @endcode
 */
class Gauge {
public:
    /**
     * @brief Cria um gauge
     * @param parent Objeto pai LVGL
     * @param width Largura (0 = auto)
     */
    Gauge(lv_obj_t* parent, int32_t width = 0);
    ~Gauge() = default;
    
    /**
     * @brief Define o range de valores
     * @param min Valor mínimo
     * @param max Valor máximo
     */
    void setRange(int32_t min, int32_t max);
    
    /**
     * @brief Define o valor atual
     * @param value Valor a exibir
     */
    void setValue(int32_t value);
    
    /**
     * @brief Obtém o valor atual
     */
    int32_t getValue() const { return m_value; }
    
    /**
     * @brief Define o label/título
     */
    void setLabel(const char* label);
    
    /**
     * @brief Define a unidade de medida
     */
    void setUnit(const char* unit);
    
    /**
     * @brief Define a cor da barra de progresso
     */
    void setColor(lv_color_t color);
    
    /**
     * @brief Define cor com base no valor (verde/amarelo/vermelho)
     * @param warningThreshold Percentual para amarelo (padrão: 70)
     * @param criticalThreshold Percentual para vermelho (padrão: 90)
     */
    void setAutoColor(int32_t warningThreshold = 70, int32_t criticalThreshold = 90);
    
    /**
     * @brief Obtém o container principal
     */
    lv_obj_t* container() const { return m_container; }
    
private:
    lv_obj_t* m_container = nullptr;
    lv_obj_t* m_labelObj = nullptr;
    lv_obj_t* m_valueLabel = nullptr;
    lv_obj_t* m_bar = nullptr;
    
    int32_t m_value = 0;
    int32_t m_min = 0;
    int32_t m_max = 100;
    const char* m_unit = nullptr;
    bool m_autoColor = false;
    int32_t m_warnThreshold = 70;
    int32_t m_critThreshold = 90;
    
    void updateValueLabel();
    void updateBarColor();
    int32_t getPercentage() const;
};

} // namespace ui
