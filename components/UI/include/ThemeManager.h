#pragma once

/**
 * @file ThemeManager.h
 * @brief Gerenciador de temas com suporte a modo claro/escuro
 * 
 * Permite trocar dinamicamente entre temas e persistir preferência.
 */

#include "lvgl.h"
#include <functional>
#include <cstdint>

namespace ui {

/**
 * @brief Estrutura com todas as cores de um tema
 */
struct ThemeColors {
    uint32_t background;      ///< Cor de fundo principal
    uint32_t surface;         ///< Cor de superfícies/cards
    uint32_t primary;         ///< Cor primária de destaque
    uint32_t primaryVariant;  ///< Variante da cor primária
    uint32_t secondary;       ///< Cor secundária
    uint32_t text;            ///< Cor de texto principal
    uint32_t textSecondary;   ///< Cor de texto secundário
    uint32_t textOnPrimary;   ///< Cor de texto sobre primary
    uint32_t success;         ///< Cor de sucesso
    uint32_t error;           ///< Cor de erro
    uint32_t warning;         ///< Cor de aviso
    uint32_t border;          ///< Cor de bordas
    uint32_t divider;         ///< Cor de divisores
    uint32_t disabled;        ///< Cor de elementos desabilitados
};

/**
 * @brief Temas predefinidos
 */
enum class ThemePreset {
    Light,
    Dark,
    Custom
};

/**
 * @class ThemeManager
 * @brief Singleton para gerenciamento de temas
 * 
 * Exemplo:
 * @code
 * // Alternar tema
 * ThemeManager::instance().toggle();
 * 
 * // Definir tema escuro
 * ThemeManager::instance().setDarkTheme();
 * 
 * // Registrar callback de mudança
 * ThemeManager::instance().onThemeChange([]() {
 *     // Atualizar UI
 * });
 * @endcode
 */
class ThemeManager {
public:
    using ThemeChangeCallback = std::function<void()>;
    
    /**
     * @brief Obtém instância singleton
     */
    static ThemeManager& instance();
    
    // Desabilitar cópia
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;
    
    // ========================================================================
    // Seleção de Tema
    // ========================================================================
    
    /**
     * @brief Define tema claro
     */
    void setLightTheme();
    
    /**
     * @brief Define tema escuro
     */
    void setDarkTheme();
    
    /**
     * @brief Define tema customizado
     * @param colors Estrutura com cores do tema
     */
    void setCustomTheme(const ThemeColors& colors);
    
    /**
     * @brief Alterna entre claro e escuro
     */
    void toggle();
    
    // ========================================================================
    // Estado
    // ========================================================================
    
    /**
     * @brief Verifica se está em modo escuro
     */
    bool isDarkMode() const;
    
    /**
     * @brief Obtém o preset atual
     */
    ThemePreset currentPreset() const;
    
    /**
     * @brief Obtém as cores atuais
     */
    const ThemeColors& colors() const;
    
    // ========================================================================
    // Acesso às Cores (conveniência)
    // ========================================================================
    
    lv_color_t background() const;
    lv_color_t surface() const;
    lv_color_t primary() const;
    lv_color_t primaryVariant() const;
    lv_color_t secondary() const;
    lv_color_t text() const;
    lv_color_t textSecondary() const;
    lv_color_t textOnPrimary() const;
    lv_color_t success() const;
    lv_color_t error() const;
    lv_color_t warning() const;
    lv_color_t border() const;
    lv_color_t divider() const;
    lv_color_t disabled() const;
    
    // ========================================================================
    // Persistência
    // ========================================================================
    
    /**
     * @brief Salva preferência de tema no NVS
     * @param nvsNamespace Namespace do NVS (padrão: "ui_config")
     */
    void savePreference(const char* nvsNamespace = "ui_config");
    
    /**
     * @brief Carrega preferência do NVS
     * @param nvsNamespace Namespace do NVS (padrão: "ui_config")
     * @return true se carregou com sucesso
     */
    bool loadPreference(const char* nvsNamespace = "ui_config");
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    /**
     * @brief Registra callback chamado quando tema muda
     * @param callback Função a ser chamada
     */
    void onThemeChange(ThemeChangeCallback callback);
    
    /**
     * @brief Remove todos os callbacks
     */
    void clearCallbacks();
    
private:
    ThemeManager();
    ~ThemeManager() = default;
    
    ThemeColors m_colors;
    ThemePreset m_preset = ThemePreset::Light;
    std::vector<ThemeChangeCallback> m_callbacks;
    
    void applyTheme();
    void notifyChange();
    
    static ThemeColors lightThemeColors();
    static ThemeColors darkThemeColors();
};

} // namespace ui
