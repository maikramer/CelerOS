#include "Header.h"
#include "Theme.h"
#include "LvglUtils.h"
#include "esp_log.h"

static const char* TAG = "Header";

// User data para armazenar callbacks e referências
struct HeaderData {
    std::function<void()> onSettingsClick;
    lv_obj_t* wifiIcon = nullptr;
    lv_obj_t* titleLabel = nullptr;
};

namespace ui {

lv_obj_t* Header::create(lv_obj_t* parent, const Config& config) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "Parent is null");
        return nullptr;
    }
    
    // Container do header
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_size(header, theme::SCREEN_W, theme::HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, theme::surface(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, theme::border(), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // User data para armazenar referências
    auto* data = new HeaderData();
    lv_obj_set_user_data(header, data);
    
    // Limpar user data quando header for deletado
    lv_obj_add_event_cb(header, [](lv_event_t* e) {
        auto* d = static_cast<HeaderData*>(lv_event_get_user_data(e));
        delete d;
    }, LV_EVENT_DELETE, data);
    
    // Ícone WiFi (esquerda)
    if (config.showWifiStatus) {
        lv_obj_t* wifiIcon = lv_label_create(header);
        lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(wifiIcon, theme::fontIcon(), 0);
        lv_obj_set_style_text_color(wifiIcon, theme::textSecondary(), 0);
        lv_obj_align(wifiIcon, LV_ALIGN_LEFT_MID, 10, 0);
        data->wifiIcon = wifiIcon;
    }
    
    // Título (centro)
    if (config.title != nullptr) {
        lv_obj_t* titleLabel = lv_label_create(header);
        lv_label_set_text(titleLabel, config.title);
        lv_obj_set_style_text_font(titleLabel, theme::fontTitle(), 0);
        lv_obj_set_style_text_color(titleLabel, theme::text(), 0);
        lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, 0);
        data->titleLabel = titleLabel;
    }
    
    // Botão configurações (direita)
    if (config.showSettingsButton) {
        lv_obj_t* settingsBtn = lv_button_create(header);
        lv_obj_set_size(settingsBtn, 40, 36);
        lv_obj_align(settingsBtn, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_set_style_bg_color(settingsBtn, theme::surface(), 0);
        lv_obj_set_style_bg_opa(settingsBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(settingsBtn, 0, 0);
        lv_obj_set_style_shadow_width(settingsBtn, 0, 0);
        
        // Ícone de configurações
        lv_obj_t* settingsIcon = lv_label_create(settingsBtn);
        lv_label_set_text(settingsIcon, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(settingsIcon, theme::fontIcon(), 0);
        lv_obj_set_style_text_color(settingsIcon, theme::textSecondary(), 0);
        lv_obj_center(settingsIcon);
        
        // Efeito de hover/press
        lv_obj_set_style_text_color(settingsIcon, theme::primary(), LV_STATE_PRESSED);
        
        // Callback
        if (config.onSettingsClick) {
            data->onSettingsClick = config.onSettingsClick;
            lv_obj_add_event_cb(settingsBtn, settingsButtonHandler, LV_EVENT_CLICKED, data);
        }
    }
    
    ESP_LOGI(TAG, "Header created with title: %s", config.title ? config.title : "(none)");
    return header;
}

void Header::updateWifiStatus(lv_obj_t* header, bool connected) {
    if (header == nullptr) return;
    
    auto* data = static_cast<HeaderData*>(lv_obj_get_user_data(header));
    if (data == nullptr || data->wifiIcon == nullptr) return;
    
    if (connected) {
        lv_obj_set_style_text_color(data->wifiIcon, theme::success(), 0);
    } else {
        lv_obj_set_style_text_color(data->wifiIcon, theme::textSecondary(), 0);
    }
}

lv_obj_t* Header::getWifiIcon(lv_obj_t* header) {
    if (header == nullptr) return nullptr;
    
    auto* data = static_cast<HeaderData*>(lv_obj_get_user_data(header));
    if (data == nullptr) return nullptr;
    
    return data->wifiIcon;
}

void Header::settingsButtonHandler(lv_event_t* e) {
    auto* data = static_cast<HeaderData*>(lv_event_get_user_data(e));
    if (data && data->onSettingsClick) {
        data->onSettingsClick();
    }
}

} // namespace ui
