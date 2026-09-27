#pragma once

/**
 * @file Layout.h
 * @brief Sistema de layout relativo e responsivo para UI
 * 
 * Fornece funções para calcular posições e tamanhos de forma relativa
 * ao tamanho da tela, facilitando a criação de layouts responsivos.
 * 
 * Exemplo:
 * @code
 * using namespace ui::layout;
 * 
 * // Configurar tamanho da tela (na inicialização)
 * setScreenSize(320, 240);
 * 
 * // Posicionar elemento em 10% do topo, 5% da esquerda
 * auto pos = position(0.05f, 0.10f);
 * lv_obj_set_pos(obj, pos.x, pos.y);
 * 
 * // Tamanho de 90% da largura, 50% da altura
 * auto sz = size(0.90f, 0.50f);
 * lv_obj_set_size(obj, sz.w, sz.h);
 * 
 * // Valores responsivos por breakpoint
 * int32_t padding = responsive(4, 8, 16);  // Small, Medium, Large
 * @endcode
 */

#include "lvgl.h"
#include "Theme.h"
#include <cstdint>

namespace ui {
namespace layout {

// ============================================================================
// Tamanho de Tela Dinâmico
// ============================================================================

/**
 * @brief Configura o tamanho da tela (chamar na inicialização)
 * @param width Largura em pixels
 * @param height Altura em pixels
 */
void setScreenSize(int32_t width, int32_t height);

/**
 * @brief Obtém a largura atual da tela
 */
int32_t screenWidth();

/**
 * @brief Obtém a altura atual da tela
 */
int32_t screenHeight();

// ============================================================================
// Breakpoints e Responsividade
// ============================================================================

/**
 * @brief Categorias de tamanho de tela
 */
enum class ScreenSize {
    Small,   ///< < 280px largura (dispositivos pequenos)
    Medium,  ///< 280-400px largura (dispositivos médios)
    Large    ///< > 400px largura (dispositivos grandes)
};

/**
 * @brief Obtém a categoria de tamanho atual
 */
ScreenSize currentScreenSize();

/**
 * @brief Retorna valor apropriado para o tamanho de tela atual
 * @param small Valor para telas pequenas
 * @param medium Valor para telas médias
 * @param large Valor para telas grandes
 * @return Valor correspondente ao breakpoint atual
 */
int32_t responsive(int32_t small, int32_t medium, int32_t large);

/**
 * @brief Retorna valor float apropriado para o tamanho de tela atual
 * @param small Valor para telas pequenas
 * @param medium Valor para telas médias
 * @param large Valor para telas grandes
 * @return Valor correspondente ao breakpoint atual
 */
float responsiveF(float small, float medium, float large);

/**
 * @brief Verifica se a tela é pequena
 */
inline bool isSmallScreen() { return currentScreenSize() == ScreenSize::Small; }

/**
 * @brief Verifica se a tela é média
 */
inline bool isMediumScreen() { return currentScreenSize() == ScreenSize::Medium; }

/**
 * @brief Verifica se a tela é grande
 */
inline bool isLargeScreen() { return currentScreenSize() == ScreenSize::Large; }

// ============================================================================
// Estruturas
// ============================================================================

/**
 * @brief Ponto 2D (posição)
 */
struct Point {
    int32_t x;
    int32_t y;
};

/**
 * @brief Tamanho 2D
 */
struct Size {
    int32_t w;
    int32_t h;
};

/**
 * @brief Retângulo (posição + tamanho)
 */
struct Rect {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    
    Point position() const { return {x, y}; }
    Size dimensions() const { return {w, h}; }
};

// ============================================================================
// Funções de Conversão Relativa
// ============================================================================

/**
 * @brief Calcula posição absoluta a partir de valores relativos
 * 
 * @param relX Posição X relativa (0.0 = esquerda, 1.0 = direita)
 * @param relY Posição Y relativa (0.0 = topo, 1.0 = base)
 * @return Point com coordenadas absolutas em pixels
 */
inline Point position(float relX, float relY) {
    return {
        static_cast<int32_t>(relX * screenWidth()),
        static_cast<int32_t>(relY * screenHeight())
    };
}

/**
 * @brief Calcula tamanho absoluto a partir de valores relativos
 * 
 * @param relW Largura relativa (0.0 a 1.0)
 * @param relH Altura relativa (0.0 a 1.0)
 * @return Size com dimensões absolutas em pixels
 */
inline Size size(float relW, float relH) {
    return {
        static_cast<int32_t>(relW * screenWidth()),
        static_cast<int32_t>(relH * screenHeight())
    };
}

/**
 * @brief Calcula largura absoluta a partir de valor relativo
 * @param relW Largura relativa (0.0 a 1.0)
 * @return Largura em pixels
 */
inline int32_t width(float relW) {
    return static_cast<int32_t>(relW * screenWidth());
}

/**
 * @brief Calcula altura absoluta a partir de valor relativo
 * @param relH Altura relativa (0.0 a 1.0)
 * @return Altura em pixels
 */
inline int32_t height(float relH) {
    return static_cast<int32_t>(relH * screenHeight());
}

// ============================================================================
// Áreas Padrão da Tela
// ============================================================================

/**
 * @brief Altura da área de título (topo da tela)
 * @return Altura em pixels
 */
inline int32_t titleHeight() {
    return theme::HEADER_H;
}

/**
 * @brief Altura da área de rodapé (botões na base)
 * @return Altura em pixels
 */
inline int32_t footerHeight() {
    return theme::BUTTON_H + theme::BUTTON_BOTTOM_OFFSET + theme::PADDING;
}

/**
 * @brief Altura disponível para conteúdo (entre título e rodapé)
 * 
 * @param hasTitle Se a tela tem área de título
 * @param hasFooter Se a tela tem área de rodapé
 * @return Altura em pixels
 */
inline int32_t contentHeight(bool hasTitle = true, bool hasFooter = true) {
    int32_t h = theme::SCREEN_H;
    if (hasTitle) h -= titleHeight();
    if (hasFooter) h -= footerHeight();
    return h;
}

/**
 * @brief Posição Y onde começa a área de conteúdo
 * @param hasTitle Se a tela tem área de título
 * @return Posição Y em pixels
 */
inline int32_t contentTop(bool hasTitle = true) {
    return hasTitle ? titleHeight() : 0;
}

/**
 * @brief Obtém o padding responsivo atual
 */
inline int32_t padding() {
    return responsive(4, 8, 12);
}

/**
 * @brief Retângulo da área de conteúdo padrão
 * 
 * Retorna a área entre o título e o rodapé, com padding horizontal.
 * 
 * @param hasTitle Se a tela tem área de título
 * @param hasFooter Se a tela tem área de rodapé
 * @return Rect com posição e tamanho da área de conteúdo
 */
inline Rect contentArea(bool hasTitle = true, bool hasFooter = true) {
    int32_t pad = padding();
    return {
        pad,
        contentTop(hasTitle),
        screenWidth() - (2 * pad),
        contentHeight(hasTitle, hasFooter)
    };
}

/**
 * @brief Retângulo da área de conteúdo centralizada (sem padding lateral)
 * 
 * @param hasTitle Se a tela tem área de título
 * @param hasFooter Se a tela tem área de rodapé
 * @return Rect com posição e tamanho da área de conteúdo
 */
inline Rect contentAreaFull(bool hasTitle = true, bool hasFooter = true) {
    return {
        0,
        contentTop(hasTitle),
        screenWidth(),
        contentHeight(hasTitle, hasFooter)
    };
}

// ============================================================================
// Helpers para Posicionamento
// ============================================================================

/**
 * @brief Calcula posição para centralizar horizontalmente
 * @param objWidth Largura do objeto
 * @return Posição X para centralizar
 */
inline int32_t centerX(int32_t objWidth) {
    return (screenWidth() - objWidth) / 2;
}

/**
 * @brief Calcula posição para centralizar verticalmente
 * @param objHeight Altura do objeto
 * @return Posição Y para centralizar
 */
inline int32_t centerY(int32_t objHeight) {
    return (screenHeight() - objHeight) / 2;
}

/**
 * @brief Calcula posição para alinhar à direita
 * @param objWidth Largura do objeto
 * @param margin Margem da borda direita (padrão: padding responsivo)
 * @return Posição X
 */
inline int32_t alignRight(int32_t objWidth, int32_t margin = -1) {
    if (margin < 0) margin = padding();
    return screenWidth() - objWidth - margin;
}

/**
 * @brief Calcula posição para alinhar à base
 * @param objHeight Altura do objeto
 * @param margin Margem da borda inferior
 * @return Posição Y
 */
inline int32_t alignBottom(int32_t objHeight, int32_t margin = theme::BUTTON_BOTTOM_OFFSET) {
    return screenHeight() - objHeight - margin;
}

// ============================================================================
// Helpers LVGL - Aplicação Direta
// ============================================================================

/**
 * @brief Aplica posição relativa a um objeto LVGL
 * 
 * @param obj Objeto LVGL
 * @param relX Posição X relativa (0.0 a 1.0)
 * @param relY Posição Y relativa (0.0 a 1.0)
 */
inline void setPosition(lv_obj_t* obj, float relX, float relY) {
    auto pos = position(relX, relY);
    lv_obj_set_pos(obj, pos.x, pos.y);
}

/**
 * @brief Aplica tamanho relativo a um objeto LVGL
 * 
 * @param obj Objeto LVGL
 * @param relW Largura relativa (0.0 a 1.0)
 * @param relH Altura relativa (0.0 a 1.0)
 */
inline void setSize(lv_obj_t* obj, float relW, float relH) {
    auto sz = size(relW, relH);
    lv_obj_set_size(obj, sz.w, sz.h);
}

/**
 * @brief Aplica largura relativa a um objeto LVGL
 * 
 * @param obj Objeto LVGL
 * @param relW Largura relativa (0.0 a 1.0)
 */
inline void setWidth(lv_obj_t* obj, float relW) {
    lv_obj_set_width(obj, width(relW));
}

/**
 * @brief Aplica altura relativa a um objeto LVGL
 * 
 * @param obj Objeto LVGL
 * @param relH Altura relativa (0.0 a 1.0)
 */
inline void setHeight(lv_obj_t* obj, float relH) {
    lv_obj_set_height(obj, height(relH));
}

/**
 * @brief Posiciona objeto na área de conteúdo
 * 
 * @param obj Objeto LVGL
 * @param hasTitle Se a tela tem área de título
 * @param hasFooter Se a tela tem área de rodapé
 */
inline void placeInContentArea(lv_obj_t* obj, bool hasTitle = true, bool hasFooter = true) {
    auto area = contentArea(hasTitle, hasFooter);
    lv_obj_set_pos(obj, area.x, area.y);
    lv_obj_set_size(obj, area.w, area.h);
}

/**
 * @brief Posiciona objeto abaixo de outro com gap relativo
 * 
 * @param obj Objeto a posicionar
 * @param reference Objeto de referência
 * @param relGap Gap relativo à altura da tela (padrão 2%)
 */
inline void placeBelow(lv_obj_t* obj, lv_obj_t* reference, float relGap = 0.02f) {
    int32_t refY = lv_obj_get_y(reference);
    int32_t refH = lv_obj_get_height(reference);
    int32_t gap = height(relGap);
    lv_obj_set_y(obj, refY + refH + gap);
}

/**
 * @brief Posiciona objeto à direita de outro com gap relativo
 * 
 * @param obj Objeto a posicionar
 * @param reference Objeto de referência
 * @param relGap Gap relativo à largura da tela (padrão 2%)
 */
inline void placeRightOf(lv_obj_t* obj, lv_obj_t* reference, float relGap = 0.02f) {
    int32_t refX = lv_obj_get_x(reference);
    int32_t refW = lv_obj_get_width(reference);
    int32_t gap = width(relGap);
    lv_obj_set_x(obj, refX + refW + gap);
}

} // namespace layout
} // namespace ui
