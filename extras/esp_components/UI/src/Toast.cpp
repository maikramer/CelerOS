#include "Toast.h"
#include "Theme.h"
#include "ThemeManager.h"
#include "Layout.h"
#include "Animation.h"
#include "esp_log.h"

static const char* TAG = "Toast";

namespace ui {

// ============================================================================
// Estado Global
// ============================================================================

static lv_obj_t* s_toastContainer = nullptr;
static lv_timer_t* s_dismissTimer = nullptr;
static std::function<void()>* s_actionCallback = nullptr;

// ============================================================================
// Helpers
// ============================================================================

static lv_color_t getToastColor(ToastType type) {
    auto& tm = ThemeManager::instance();
    switch (type) {
        case ToastType::Success: return tm.success();
        case ToastType::Warning: return tm.warning();
        case ToastType::Error:   return tm.error();
        case ToastType::Info:
        default:                 return tm.secondary();
    }
}

static void dismissTimerCallback(lv_timer_t* timer) {
    (void)timer;
    dismissToast();
}

static void actionButtonHandler(lv_event_t* e) {
    (void)e;
    if (s_actionCallback && *s_actionCallback) {
        (*s_actionCallback)();
    }
    dismissToast();
}

// ============================================================================
// API Pública
// ============================================================================

void showToast(const char* message, uint32_t durationMs, ToastPosition position, ToastType type) {
    if (!message) return;
    
    // Fecha toast existente
    dismissToast();
    
    // Obtém tela ativa
    lv_obj_t* screen = lv_screen_active();
    if (!screen) {
        ESP_LOGE(TAG, "No active screen");
        return;
    }
    
    auto& tm = ThemeManager::instance();
    
    // Cria container
    s_toastContainer = lv_obj_create(screen);
    lv_obj_remove_style_all(s_toastContainer);
    
    // Estilo
    lv_obj_set_style_bg_color(s_toastContainer, getToastColor(type), 0);
    lv_obj_set_style_bg_opa(s_toastContainer, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_toastContainer, theme::RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(s_toastContainer, layout::responsive(12, 16, 20), 0);
    lv_obj_set_style_pad_ver(s_toastContainer, layout::responsive(8, 10, 12), 0);
    
    // Tamanho
    lv_obj_set_width(s_toastContainer, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toastContainer, layout::screenWidth() - 32, 0);
    lv_obj_set_height(s_toastContainer, LV_SIZE_CONTENT);
    
    // Label
    lv_obj_t* label = lv_label_create(s_toastContainer);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(label, layout::screenWidth() - 64, 0);
    
    // Posição
    int32_t margin = layout::responsive(10, 15, 20);
    switch (position) {
        case ToastPosition::Top:
            lv_obj_align(s_toastContainer, LV_ALIGN_TOP_MID, 0, margin);
            break;
        case ToastPosition::Center:
            lv_obj_align(s_toastContainer, LV_ALIGN_CENTER, 0, 0);
            break;
        case ToastPosition::Bottom:
        default:
            lv_obj_align(s_toastContainer, LV_ALIGN_BOTTOM_MID, 0, -margin);
            break;
    }
    
    // Animação de entrada
    anim::fadeIn(s_toastContainer, 150);
    
    // Timer para fechar
    s_dismissTimer = lv_timer_create(dismissTimerCallback, durationMs, nullptr);
    lv_timer_set_repeat_count(s_dismissTimer, 1);
    
    ESP_LOGD(TAG, "Toast shown: %s", message);
}

void showSnackbar(const char* message, const char* actionText,
                  std::function<void()> onAction, uint32_t durationMs) {
    if (!message) return;
    
    // Fecha toast existente
    dismissToast();
    
    // Obtém tela ativa
    lv_obj_t* screen = lv_screen_active();
    if (!screen) {
        ESP_LOGE(TAG, "No active screen");
        return;
    }
    
    auto& tm = ThemeManager::instance();
    
    // Cria container
    s_toastContainer = lv_obj_create(screen);
    lv_obj_remove_style_all(s_toastContainer);
    
    // Estilo
    lv_obj_set_style_bg_color(s_toastContainer, tm.secondary(), 0);
    lv_obj_set_style_bg_opa(s_toastContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toastContainer, theme::RADIUS_SM, 0);
    lv_obj_set_style_pad_left(s_toastContainer, layout::responsive(12, 16, 20), 0);
    lv_obj_set_style_pad_right(s_toastContainer, layout::responsive(4, 6, 8), 0);
    lv_obj_set_style_pad_ver(s_toastContainer, layout::responsive(4, 6, 8), 0);
    
    // Layout horizontal
    lv_obj_set_flex_flow(s_toastContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_toastContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, 
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Tamanho
    lv_obj_set_width(s_toastContainer, layout::screenWidth() - 32);
    lv_obj_set_height(s_toastContainer, LV_SIZE_CONTENT);
    
    // Label da mensagem
    lv_obj_t* label = lv_label_create(s_toastContainer);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(label, 1);
    
    // Botão de ação
    if (actionText && onAction) {
        // Salva callback
        s_actionCallback = new std::function<void()>(onAction);
        
        lv_obj_t* btn = lv_button_create(s_toastContainer);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 8, 0);
        
        lv_obj_t* btnLabel = lv_label_create(btn);
        lv_label_set_text(btnLabel, actionText);
        lv_obj_set_style_text_color(btnLabel, tm.primary(), 0);
        lv_obj_set_style_text_font(btnLabel, theme::fontText(), 0);
        
        lv_obj_add_event_cb(btn, actionButtonHandler, LV_EVENT_CLICKED, nullptr);
    }
    
    // Posição (sempre na base para snackbar)
    int32_t margin = layout::responsive(10, 15, 20);
    lv_obj_align(s_toastContainer, LV_ALIGN_BOTTOM_MID, 0, -margin);
    
    // Animação de entrada
    anim::fadeIn(s_toastContainer, 150);
    
    // Timer para fechar
    s_dismissTimer = lv_timer_create(dismissTimerCallback, durationMs, nullptr);
    lv_timer_set_repeat_count(s_dismissTimer, 1);
    
    ESP_LOGD(TAG, "Snackbar shown: %s", message);
}

void dismissToast() {
    // Cancela timer
    if (s_dismissTimer) {
        lv_timer_delete(s_dismissTimer);
        s_dismissTimer = nullptr;
    }
    
    // Limpa callback
    if (s_actionCallback) {
        delete s_actionCallback;
        s_actionCallback = nullptr;
    }
    
    // Remove container
    if (s_toastContainer) {
        lv_obj_delete(s_toastContainer);
        s_toastContainer = nullptr;
        ESP_LOGD(TAG, "Toast dismissed");
    }
}

bool isToastVisible() {
    return s_toastContainer != nullptr;
}

} // namespace ui
