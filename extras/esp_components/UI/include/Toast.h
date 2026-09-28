#pragma once

/**
 * @file Toast.h
 * @brief Sistema de notificações temporárias (Toast/Snackbar)
 */

#include "lvgl.h"
#include <functional>

namespace ui {

/**
 * @brief Posição do toast na tela
 */
enum class ToastPosition {
    Top,
    Bottom,
    Center
};

/**
 * @brief Tipo de toast (afeta cor)
 */
enum class ToastType {
    Info,       ///< Informação (cinza)
    Success,    ///< Sucesso (verde)
    Warning,    ///< Aviso (laranja)
    Error       ///< Erro (vermelho)
};

/**
 * @brief Mostra um toast simples
 * @param message Mensagem a exibir
 * @param durationMs Duração em ms (padrão: 2000)
 * @param position Posição na tela (padrão: Bottom)
 * @param type Tipo do toast (padrão: Info)
 */
void showToast(const char* message, 
               uint32_t durationMs = 2000,
               ToastPosition position = ToastPosition::Bottom,
               ToastType type = ToastType::Info);

/**
 * @brief Mostra um snackbar com ação
 * @param message Mensagem a exibir
 * @param actionText Texto do botão de ação
 * @param onAction Callback quando ação é pressionada
 * @param durationMs Duração em ms (padrão: 4000)
 */
void showSnackbar(const char* message,
                  const char* actionText,
                  std::function<void()> onAction,
                  uint32_t durationMs = 4000);

/**
 * @brief Fecha toast/snackbar atual (se houver)
 */
void dismissToast();

/**
 * @brief Verifica se há toast visível
 */
bool isToastVisible();

// Conveniência: funções tipadas
inline void showInfoToast(const char* msg, uint32_t ms = 2000) {
    showToast(msg, ms, ToastPosition::Bottom, ToastType::Info);
}

inline void showSuccessToast(const char* msg, uint32_t ms = 2000) {
    showToast(msg, ms, ToastPosition::Bottom, ToastType::Success);
}

inline void showWarningToast(const char* msg, uint32_t ms = 3000) {
    showToast(msg, ms, ToastPosition::Bottom, ToastType::Warning);
}

inline void showErrorToast(const char* msg, uint32_t ms = 4000) {
    showToast(msg, ms, ToastPosition::Bottom, ToastType::Error);
}

} // namespace ui
