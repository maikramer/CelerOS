#include "InputField.h"
#include "Theme.h"
#include "LvglUtils.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "InputField";

// User data para botões de input
struct InputButtonData {
    std::function<void()> onEdit;
    char displayText[64];
};

namespace ui {

lv_obj_t* InputField::create(lv_obj_t* parent, Type type,
                             const char* placeholder, int32_t width) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "Parent is null");
        return nullptr;
    }
    
    // Criar textarea
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, width, theme::INPUT_H);
    lv_textarea_set_one_line(ta, true);
    
    // Placeholder
    if (placeholder) {
        lv_textarea_set_placeholder_text(ta, placeholder);
    }
    
    // Configurar tipo
    switch (type) {
        case Type::Password:
            lv_textarea_set_password_mode(ta, true);
            break;
        case Type::Number:
            lv_textarea_set_accepted_chars(ta, "0123456789.-");
            break;
        case Type::Text:
        default:
            break;
    }
    
    // Estilos
    lv_obj_set_style_bg_color(ta, theme::surface(), 0);
    lv_obj_set_style_border_color(ta, theme::border(), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, theme::RADIUS_SM, 0);
    lv_obj_set_style_text_font(ta, theme::fontText(), 0);
    lv_obj_set_style_text_color(ta, theme::text(), 0);
    lv_obj_set_style_pad_all(ta, 8, 0);
    
    // Estilo focado
    lv_obj_set_style_border_color(ta, theme::primary(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    
    return ta;
}

lv_obj_t* InputField::createButton(lv_obj_t* parent, const char* initialValue,
                                   std::function<void()> onEdit, int32_t width) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "Parent is null");
        return nullptr;
    }
    
    // Criar botão estilizado como input
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, width, theme::INPUT_H);
    
    // Estilos para parecer um campo de texto
    lv_obj_set_style_bg_color(btn, theme::surface(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, theme::border(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, theme::RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 10, 0);
    lv_obj_set_style_pad_right(btn, 10, 0);
    
    // Efeito de hover
    lv_obj_set_style_border_color(btn, theme::primary(), LV_STATE_PRESSED);
    
    // Label com o valor
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, initialValue ? initialValue : "");
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_obj_set_style_text_color(label, theme::text(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // User data para callback
    if (onEdit) {
        auto* data = new InputButtonData();
        data->onEdit = onEdit;
        strncpy(data->displayText, initialValue ? initialValue : "", sizeof(data->displayText) - 1);
        data->displayText[sizeof(data->displayText) - 1] = '\0';
        
        lv_obj_set_user_data(btn, data);
        lv_obj_add_event_cb(btn, inputButtonHandler, LV_EVENT_CLICKED, data);
        
        // Limpar user data quando deletado
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* d = static_cast<InputButtonData*>(lv_event_get_user_data(e));
            delete d;
        }, LV_EVENT_DELETE, data);
    }
    
    return btn;
}

void InputField::setButtonText(lv_obj_t* button, const char* text, bool isPassword) {
    if (button == nullptr) return;
    
    // Encontrar o label filho
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (label == nullptr) return;
    
    if (isPassword && text && strlen(text) > 0) {
        // Criar string de asteriscos
        size_t len = strlen(text);
        char* masked = new char[len + 1];
        memset(masked, '*', len);
        masked[len] = '\0';
        lv_label_set_text(label, masked);
        delete[] masked;
    } else {
        lv_label_set_text(label, text ? text : "");
    }
    
    // Atualizar user data
    auto* data = static_cast<InputButtonData*>(lv_obj_get_user_data(button));
    if (data) {
        strncpy(data->displayText, text ? text : "", sizeof(data->displayText) - 1);
        data->displayText[sizeof(data->displayText) - 1] = '\0';
    }
}

const char* InputField::getText(lv_obj_t* textarea) {
    if (textarea == nullptr) return "";
    return lv_textarea_get_text(textarea);
}

void InputField::setText(lv_obj_t* textarea, const char* text) {
    if (textarea == nullptr) return;
    lv_textarea_set_text(textarea, text ? text : "");
}

void InputField::inputButtonHandler(lv_event_t* e) {
    auto* data = static_cast<InputButtonData*>(lv_event_get_user_data(e));
    if (data && data->onEdit) {
        data->onEdit();
    }
}

} // namespace ui
