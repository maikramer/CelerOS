#pragma once

/**
 * @file Dialog.h
 * @brief Sistema de dialogs modais para LVGL
 * 
 * Fornece dialogs pré-configurados para diferentes situações:
 * - Info: Informação geral
 * - Warning: Avisos
 * - Error: Erros
 * - Confirm: Confirmação com OK/Cancelar
 * - Loading: Indicador de carregamento
 */

#include "lvgl.h"
#include <functional>

namespace ui {

/**
 * @class Dialog
 * @brief Classe estática para exibição de dialogs modais
 * 
 * Exemplos:
 * @code
 * // Dialog simples
 * ui::Dialog::show(ui::Dialog::Type::Info, "Título", "Mensagem");
 * 
 * // Dialog com callback
 * ui::Dialog::show(ui::Dialog::Type::Error, "Erro", "Falha na conexão",
 *                  []() { ESP_LOGI("APP", "Usuario fechou dialog"); });
 * 
 * // Dialog de confirmação
 * ui::Dialog::confirm("Confirmar", "Deseja continuar?",
 *                     []() { doAction(); },
 *                     []() { ESP_LOGI("APP", "Cancelado"); });
 * 
 * // Loading
 * ui::Dialog::showLoading("Carregando...");
 * // ... fazer operação ...
 * ui::Dialog::hideLoading();
 * @endcode
 */
class Dialog {
public:
    /**
     * @brief Tipos de dialog
     */
    enum class Type {
        Info,       ///< Informação geral (ícone azul)
        Warning,    ///< Aviso (ícone laranja)
        Error,      ///< Erro (ícone vermelho)
        Success     ///< Sucesso (ícone verde)
    };
    
    /**
     * @brief Exibe um dialog com uma única ação (OK)
     * 
     * @param type Tipo do dialog (afeta cor do título)
     * @param title Título do dialog
     * @param message Mensagem do dialog
     * @param onOk Callback ao pressionar OK (opcional)
     */
    static void show(Type type, const char* title, const char* message,
                     std::function<void()> onOk = nullptr);
    
    /**
     * @brief Exibe um dialog de confirmação (OK + Cancelar)
     * 
     * @param title Título do dialog
     * @param message Mensagem do dialog
     * @param onConfirm Callback ao confirmar
     * @param onCancel Callback ao cancelar (opcional)
     */
    static void confirm(const char* title, const char* message,
                       std::function<void()> onConfirm,
                       std::function<void()> onCancel = nullptr);
    
    /**
     * @brief Exibe um dialog de carregamento com spinner
     * 
     * @param message Mensagem a exibir
     */
    static void showLoading(const char* message);
    
    /**
     * @brief Esconde o dialog de carregamento
     */
    static void hideLoading();
    
    /**
     * @brief Fecha qualquer dialog aberto
     */
    static void close();
    
    /**
     * @brief Verifica se há um dialog aberto
     * @return true se há dialog visível
     */
    static bool isOpen();
    
private:
    static lv_obj_t* s_overlay;
    static lv_obj_t* s_dialog;
    static lv_obj_t* s_loadingDialog;
    static std::function<void()> s_onOk;
    static std::function<void()> s_onCancel;
    
    static void createOverlay();
    static void okButtonHandler(lv_event_t* e);
    static void cancelButtonHandler(lv_event_t* e);
};

} // namespace ui
