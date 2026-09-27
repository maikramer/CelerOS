#pragma once

/**
 * @file Theme.h
 * @brief Sistema de temas centralizado para UI
 * 
 * Define cores, constantes de layout e fontes padrão para
 * manter consistência visual em toda a aplicação.
 */

#include "lvgl.h"

namespace ui {
namespace theme {

// ============================================================================
// Cores do Tema
// ============================================================================

/** @brief Cor de fundo principal (branco) */
inline lv_color_t background() { return lv_color_hex(0xFFFFFF); }

/** @brief Cor de superfícies elevadas (branco) */
inline lv_color_t surface() { return lv_color_hex(0xFFFFFF); }

/** @brief Cor primária de destaque (azul Material) */
inline lv_color_t primary() { return lv_color_hex(0x2196F3); }

/** @brief Cor secundária (cinza azulado) */
inline lv_color_t secondary() { return lv_color_hex(0x607D8B); }

/** @brief Cor de texto principal (preto) */
inline lv_color_t text() { return lv_color_hex(0x000000); }

/** @brief Cor de texto secundário (cinza) */
inline lv_color_t textSecondary() { return lv_color_hex(0x757575); }

/** @brief Cor de sucesso (verde) */
inline lv_color_t success() { return lv_color_hex(0x4CAF50); }

/** @brief Cor de erro (vermelho) */
inline lv_color_t error() { return lv_color_hex(0xF44336); }

/** @brief Cor de aviso (laranja) */
inline lv_color_t warning() { return lv_color_hex(0xFF9800); }

/** @brief Cor de bordas (cinza claro) */
inline lv_color_t border() { return lv_color_hex(0xCCCCCC); }

/** @brief Cor de botão cinza/neutro */
inline lv_color_t buttonGray() { return lv_color_hex(0x757575); }

// ============================================================================
// Constantes de Layout
// ============================================================================

/** @brief Largura da tela em pixels */
constexpr int32_t SCREEN_W = 320;

/** @brief Altura da tela em pixels */
constexpr int32_t SCREEN_H = 240;

/** @brief Padding padrão */
constexpr int32_t PADDING = 8;

/** @brief Padding horizontal */
constexpr int32_t PADDING_H = 16;

/** @brief Altura do header/barra de status */
constexpr int32_t HEADER_H = 40;

/** @brief Altura padrão de botões */
constexpr int32_t BUTTON_H = 38;

/** @brief Altura de campos de entrada */
constexpr int32_t INPUT_H = 40;

/** @brief Raio de borda pequeno */
constexpr int32_t RADIUS_SM = 4;

/** @brief Raio de borda médio */
constexpr int32_t RADIUS_MD = 8;

/** @brief Raio de borda grande (botões arredondados) */
constexpr int32_t RADIUS_LG = 18;

/** @brief Largura padrão de botões */
constexpr int32_t BUTTON_W = 120;

/** @brief Offset inferior padrão para botões */
constexpr int32_t BUTTON_BOTTOM_OFFSET = 10;

// ============================================================================
// Fontes
// ============================================================================

/**
 * @brief Registra uma fonte customizada para títulos
 * @param font Ponteiro para a fonte LVGL
 */
void setTitleFont(const lv_font_t* font);

/**
 * @brief Registra uma fonte customizada para texto
 * @param font Ponteiro para a fonte LVGL
 */
void setTextFont(const lv_font_t* font);

/**
 * @brief Registra uma fonte customizada para captions
 * @param font Ponteiro para a fonte LVGL
 */
void setCaptionFont(const lv_font_t* font);

/**
 * @brief Obtém a fonte para títulos
 * @return Ponteiro para a fonte, ou fallback padrão
 */
const lv_font_t* fontTitle();

/**
 * @brief Obtém a fonte para texto normal
 * @return Ponteiro para a fonte, ou fallback padrão
 */
const lv_font_t* fontText();

/**
 * @brief Obtém a fonte para captions/legendas
 * @return Ponteiro para a fonte, ou fallback padrão
 */
const lv_font_t* fontCaption();

/**
 * @brief Obtém a fonte para ícones (Montserrat com símbolos)
 * @return Ponteiro para lv_font_montserrat_20
 */
const lv_font_t* fontIcon();

// ============================================================================
// Estilos Helpers
// ============================================================================

/**
 * @brief Aplica estilo padrão de tela
 * @param screen Objeto da tela
 */
void applyScreenStyle(lv_obj_t* screen);

/**
 * @brief Aplica estilo padrão de label
 * @param label Objeto do label
 */
void applyLabelStyle(lv_obj_t* label);

/**
 * @brief Aplica estilo padrão de botão
 * @param button Objeto do botão
 * @param color Cor de fundo do botão
 */
void applyButtonStyle(lv_obj_t* button, lv_color_t color);

} // namespace theme
} // namespace ui
