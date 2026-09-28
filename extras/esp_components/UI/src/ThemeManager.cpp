#include "ThemeManager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "ThemeManager";

namespace ui {

// ============================================================================
// Singleton
// ============================================================================

ThemeManager& ThemeManager::instance() {
    static ThemeManager s_instance;
    return s_instance;
}

ThemeManager::ThemeManager() {
    // Inicializa com tema claro
    m_colors = lightThemeColors();
    m_preset = ThemePreset::Light;
}

// ============================================================================
// Cores Predefinidas
// ============================================================================

ThemeColors ThemeManager::lightThemeColors() {
    return ThemeColors{
        .background     = 0xFFFFFF,  // Branco
        .surface        = 0xFFFFFF,  // Branco
        .primary        = 0x2196F3,  // Azul Material
        .primaryVariant = 0x1976D2,  // Azul mais escuro
        .secondary      = 0x607D8B,  // Cinza azulado
        .text           = 0x000000,  // Preto
        .textSecondary  = 0x757575,  // Cinza
        .textOnPrimary  = 0xFFFFFF,  // Branco
        .success        = 0x4CAF50,  // Verde
        .error          = 0xF44336,  // Vermelho
        .warning        = 0xFF9800,  // Laranja
        .border         = 0xCCCCCC,  // Cinza claro
        .divider        = 0xE0E0E0,  // Cinza bem claro
        .disabled       = 0xBDBDBD,  // Cinza médio
    };
}

ThemeColors ThemeManager::darkThemeColors() {
    return ThemeColors{
        .background     = 0x121212,  // Quase preto
        .surface        = 0x1E1E1E,  // Cinza escuro
        .primary        = 0xBB86FC,  // Roxo claro (Material Dark)
        .primaryVariant = 0x3700B3,  // Roxo escuro
        .secondary      = 0x03DAC6,  // Ciano
        .text           = 0xFFFFFF,  // Branco
        .textSecondary  = 0xB0B0B0,  // Cinza claro
        .textOnPrimary  = 0x000000,  // Preto
        .success        = 0x81C784,  // Verde claro
        .error          = 0xCF6679,  // Vermelho rosado
        .warning        = 0xFFB74D,  // Laranja claro
        .border         = 0x424242,  // Cinza escuro
        .divider        = 0x373737,  // Cinza bem escuro
        .disabled       = 0x616161,  // Cinza médio escuro
    };
}

// ============================================================================
// Seleção de Tema
// ============================================================================

void ThemeManager::setLightTheme() {
    m_colors = lightThemeColors();
    m_preset = ThemePreset::Light;
    applyTheme();
    notifyChange();
    ESP_LOGI(TAG, "Light theme applied");
}

void ThemeManager::setDarkTheme() {
    m_colors = darkThemeColors();
    m_preset = ThemePreset::Dark;
    applyTheme();
    notifyChange();
    ESP_LOGI(TAG, "Dark theme applied");
}

void ThemeManager::setCustomTheme(const ThemeColors& colors) {
    m_colors = colors;
    m_preset = ThemePreset::Custom;
    applyTheme();
    notifyChange();
    ESP_LOGI(TAG, "Custom theme applied");
}

void ThemeManager::toggle() {
    if (m_preset == ThemePreset::Dark) {
        setLightTheme();
    } else {
        setDarkTheme();
    }
}

// ============================================================================
// Estado
// ============================================================================

bool ThemeManager::isDarkMode() const {
    return m_preset == ThemePreset::Dark;
}

ThemePreset ThemeManager::currentPreset() const {
    return m_preset;
}

const ThemeColors& ThemeManager::colors() const {
    return m_colors;
}

// ============================================================================
// Acesso às Cores
// ============================================================================

lv_color_t ThemeManager::background() const {
    return lv_color_hex(m_colors.background);
}

lv_color_t ThemeManager::surface() const {
    return lv_color_hex(m_colors.surface);
}

lv_color_t ThemeManager::primary() const {
    return lv_color_hex(m_colors.primary);
}

lv_color_t ThemeManager::primaryVariant() const {
    return lv_color_hex(m_colors.primaryVariant);
}

lv_color_t ThemeManager::secondary() const {
    return lv_color_hex(m_colors.secondary);
}

lv_color_t ThemeManager::text() const {
    return lv_color_hex(m_colors.text);
}

lv_color_t ThemeManager::textSecondary() const {
    return lv_color_hex(m_colors.textSecondary);
}

lv_color_t ThemeManager::textOnPrimary() const {
    return lv_color_hex(m_colors.textOnPrimary);
}

lv_color_t ThemeManager::success() const {
    return lv_color_hex(m_colors.success);
}

lv_color_t ThemeManager::error() const {
    return lv_color_hex(m_colors.error);
}

lv_color_t ThemeManager::warning() const {
    return lv_color_hex(m_colors.warning);
}

lv_color_t ThemeManager::border() const {
    return lv_color_hex(m_colors.border);
}

lv_color_t ThemeManager::divider() const {
    return lv_color_hex(m_colors.divider);
}

lv_color_t ThemeManager::disabled() const {
    return lv_color_hex(m_colors.disabled);
}

// ============================================================================
// Persistência
// ============================================================================

void ThemeManager::savePreference(const char* nvsNamespace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }
    
    uint8_t themeValue = static_cast<uint8_t>(m_preset);
    err = nvs_set_u8(handle, "theme", themeValue);
    if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Theme preference saved: %d", themeValue);
    } else {
        ESP_LOGE(TAG, "Failed to save theme: %s", esp_err_to_name(err));
    }
    
    nvs_close(handle);
}

bool ThemeManager::loadPreference(const char* nvsNamespace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS not found, using default theme");
        return false;
    }
    
    uint8_t themeValue = 0;
    err = nvs_get_u8(handle, "theme", &themeValue);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Theme preference not found");
        return false;
    }
    
    switch (static_cast<ThemePreset>(themeValue)) {
        case ThemePreset::Light:
            setLightTheme();
            break;
        case ThemePreset::Dark:
            setDarkTheme();
            break;
        default:
            // Custom não é persistido
            setLightTheme();
            break;
    }
    
    ESP_LOGI(TAG, "Theme preference loaded: %d", themeValue);
    return true;
}

// ============================================================================
// Callbacks
// ============================================================================

void ThemeManager::onThemeChange(ThemeChangeCallback callback) {
    if (callback) {
        m_callbacks.push_back(callback);
    }
}

void ThemeManager::clearCallbacks() {
    m_callbacks.clear();
}

// ============================================================================
// Helpers Privados
// ============================================================================

void ThemeManager::applyTheme() {
    // O tema é aplicado automaticamente quando as funções de Theme.h
    // usam ThemeManager::instance().xxx() ao invés de valores fixos
    // 
    // Para uma aplicação completa, seria necessário:
    // 1. Invalidar todas as telas atuais
    // 2. Forçar re-render
    // 
    // Por enquanto, novos elementos usarão as cores atualizadas
    // e telas podem se registrar para receber notificação de mudança
}

void ThemeManager::notifyChange() {
    for (auto& callback : m_callbacks) {
        if (callback) {
            callback();
        }
    }
}

} // namespace ui
