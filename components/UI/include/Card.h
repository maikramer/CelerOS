#pragma once

/**
 * @file Card.h
 * @brief Componente Card para exibição de conteúdo em container elevado
 */

#include "lvgl.h"
#include <functional>

namespace ui {

/**
 * @class Card
 * @brief Container com visual de card (sombra, borda arredondada)
 * 
 * Exemplo:
 * @code
 * Card card(parent);
 * card.setTitle("Título");
 * card.setContent("Conteúdo do card");
 * card.setIcon(LV_SYMBOL_SETTINGS);
 * @endcode
 */
class Card {
public:
    /**
     * @brief Cria um card
     * @param parent Objeto pai LVGL
     * @param width Largura (0 = 100% do pai)
     * @param height Altura (0 = auto)
     */
    Card(lv_obj_t* parent, int32_t width = 0, int32_t height = 0);
    ~Card();
    
    /**
     * @brief Define o título do card
     */
    void setTitle(const char* title);
    
    /**
     * @brief Define o conteúdo textual
     */
    void setContent(const char* content);
    
    /**
     * @brief Define um ícone (símbolo LVGL)
     */
    void setIcon(const char* symbol);
    
    /**
     * @brief Define callback de clique
     */
    void setOnClick(std::function<void()> callback);
    
    /**
     * @brief Obtém o container principal
     */
    lv_obj_t* container() const { return m_container; }
    
    /**
     * @brief Obtém o label de título
     */
    lv_obj_t* titleLabel() const { return m_title; }
    
    /**
     * @brief Obtém o label de conteúdo
     */
    lv_obj_t* contentLabel() const { return m_content; }
    
    /**
     * @brief Define cor de fundo
     */
    void setBackgroundColor(lv_color_t color);
    
    /**
     * @brief Habilita/desabilita sombra
     */
    void setShadowEnabled(bool enabled);
    
private:
    lv_obj_t* m_container = nullptr;
    lv_obj_t* m_icon = nullptr;
    lv_obj_t* m_title = nullptr;
    lv_obj_t* m_content = nullptr;
    std::function<void()>* m_clickCallback = nullptr;
    
    void applyStyle();
    static void clickHandler(lv_event_t* e);
};

} // namespace ui
