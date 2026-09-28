#pragma once

/**
 * @file InputField.h
 * @brief Campos de entrada de texto
 * 
 * Componentes para entrada de dados:
 * - Campo de texto com textarea LVGL
 * - Botão que abre teclado virtual
 */

#include "lvgl.h"
#include <functional>

namespace ui {

/**
 * @class InputField
 * @brief Componentes de entrada de texto
 * 
 * Exemplo - Campo de texto direto:
 * @code
 * auto* input = ui::InputField::create(screen, ui::InputField::Type::Text, "Digite aqui");
 * lv_obj_align(input, LV_ALIGN_TOP_MID, 0, 50);
 * @endcode
 * 
 * Exemplo - Botão que abre editor:
 * @code
 * auto* btn = ui::InputField::createButton(screen, "valor inicial",
 *     [](const char* newValue) {
 *         ESP_LOGI("APP", "Novo valor: %s", newValue);
 *     });
 * @endcode
 */
class InputField {
public:
    /**
     * @brief Tipos de campo de entrada
     */
    enum class Type {
        Text,       ///< Texto normal
        Password,   ///< Senha (caracteres ocultos)
        Number      ///< Apenas números
    };
    
    /**
     * @brief Cria um campo de entrada (textarea)
     * 
     * @param parent Objeto pai
     * @param type Tipo do campo
     * @param placeholder Texto placeholder (opcional)
     * @param width Largura (padrão: 240)
     * @return Ponteiro para o textarea criado
     */
    static lv_obj_t* create(lv_obj_t* parent, Type type,
                           const char* placeholder = nullptr,
                           int32_t width = 240);
    
    /**
     * @brief Cria um botão estilizado como input
     * 
     * Útil quando não quer usar textarea diretamente,
     * mas abrir uma tela de edição separada.
     * 
     * @param parent Objeto pai
     * @param initialValue Valor inicial a exibir
     * @param onEdit Callback quando usuário quer editar
     * @param width Largura (padrão: 240)
     * @return Ponteiro para o botão criado
     */
    static lv_obj_t* createButton(lv_obj_t* parent,
                                  const char* initialValue,
                                  std::function<void()> onEdit,
                                  int32_t width = 240);
    
    /**
     * @brief Atualiza o texto de um botão de input
     * 
     * @param button Botão criado por createButton()
     * @param text Novo texto
     * @param isPassword Se true, exibe asteriscos
     */
    static void setButtonText(lv_obj_t* button, const char* text, bool isPassword = false);
    
    /**
     * @brief Obtém o texto de um textarea
     * 
     * @param textarea Campo criado por create()
     * @return Texto atual
     */
    static const char* getText(lv_obj_t* textarea);
    
    /**
     * @brief Define o texto de um textarea
     * 
     * @param textarea Campo criado por create()
     * @param text Novo texto
     */
    static void setText(lv_obj_t* textarea, const char* text);
    
private:
    static void inputButtonHandler(lv_event_t* e);
};

} // namespace ui
