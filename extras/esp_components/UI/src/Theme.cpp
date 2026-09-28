#include "Theme.h"
#include "esp_log.h"

// Declarar fonte Montserrat para ícones (padrão LVGL)
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_14);

static const char* TAG = "Theme";

namespace ui {
namespace theme {

// Fontes customizáveis (nullptr = usar padrão)
static const lv_font_t* s_titleFont = nullptr;
static const lv_font_t* s_textFont = nullptr;
static const lv_font_t* s_captionFont = nullptr;

void setTitleFont(const lv_font_t* font) {
    s_titleFont = font;
    ESP_LOGI(TAG, "Title font set: %p", font);
}

void setTextFont(const lv_font_t* font) {
    s_textFont = font;
    ESP_LOGI(TAG, "Text font set: %p", font);
}

void setCaptionFont(const lv_font_t* font) {
    s_captionFont = font;
    ESP_LOGI(TAG, "Caption font set: %p", font);
}

const lv_font_t* fontTitle() {
    return s_titleFont ? s_titleFont : &lv_font_montserrat_20;
}

const lv_font_t* fontText() {
    return s_textFont ? s_textFont : &lv_font_montserrat_14;
}

const lv_font_t* fontCaption() {
    return s_captionFont ? s_captionFont : &lv_font_montserrat_14;
}

const lv_font_t* fontIcon() {
    return &lv_font_montserrat_20;
}

void applyScreenStyle(lv_obj_t* screen) {
    if (screen == nullptr) return;
    
    lv_obj_set_style_bg_color(screen, background(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void applyLabelStyle(lv_obj_t* label) {
    if (label == nullptr) return;
    
    lv_obj_set_style_text_color(label, text(), 0);
    lv_obj_set_style_text_font(label, fontText(), 0);
    lv_obj_set_style_pad_top(label, 4, 0);
    lv_obj_set_style_pad_bottom(label, 4, 0);
}

void applyButtonStyle(lv_obj_t* button, lv_color_t color) {
    if (button == nullptr) return;
    
    // Estado normal
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(button, lv_color_white(), 0);
    lv_obj_set_style_radius(button, RADIUS_LG, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    
    // Transições suaves
    static lv_style_transition_dsc_t transDesc;
    static const lv_style_prop_t transProps[] = {
        LV_STYLE_BG_COLOR,
        LV_STYLE_TRANSFORM_SCALE_X,
        LV_STYLE_TRANSFORM_SCALE_Y,
        LV_STYLE_PROP_INV  // Terminador
    };
    lv_style_transition_dsc_init(&transDesc, transProps, lv_anim_path_ease_out, 100, 0, nullptr);
    lv_obj_set_style_transition(button, &transDesc, 0);
    
    // Estado pressed: escurecimento + leve redução de escala
    lv_obj_set_style_bg_color(button, lv_color_darken(color, LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_x(button, 245, LV_STATE_PRESSED);  // 245/256 ≈ 0.96
    lv_obj_set_style_transform_scale_y(button, 245, LV_STATE_PRESSED);
    
    // Estado focused (para navegação por teclado/encoder)
    lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(button, primary(), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(button, LV_OPA_50, LV_STATE_FOCUSED);
}

} // namespace theme
} // namespace ui
