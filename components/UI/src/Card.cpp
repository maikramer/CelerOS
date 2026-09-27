#include "Card.h"
#include "Theme.h"
#include "ThemeManager.h"
#include "Layout.h"

namespace ui {

Card::Card(lv_obj_t* parent, int32_t width, int32_t height) {
    m_container = lv_obj_create(parent);
    
    // Tamanho
    if (width <= 0) {
        lv_obj_set_width(m_container, lv_pct(100));
    } else {
        lv_obj_set_width(m_container, width);
    }
    
    if (height <= 0) {
        lv_obj_set_height(m_container, LV_SIZE_CONTENT);
    } else {
        lv_obj_set_height(m_container, height);
    }
    
    applyStyle();
}

Card::~Card() {
    if (m_clickCallback) {
        delete m_clickCallback;
        m_clickCallback = nullptr;
    }
    // LVGL cuida da deleção dos objetos filhos
}

void Card::applyStyle() {
    auto& tm = ThemeManager::instance();
    
    // Fundo e borda
    lv_obj_set_style_bg_color(m_container, tm.surface(), 0);
    lv_obj_set_style_bg_opa(m_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_container, 1, 0);
    lv_obj_set_style_border_color(m_container, tm.border(), 0);
    lv_obj_set_style_radius(m_container, theme::RADIUS_MD, 0);
    
    // Padding interno
    int32_t pad = layout::responsive(8, 12, 16);
    lv_obj_set_style_pad_all(m_container, pad, 0);
    
    // Sombra (suave)
    lv_obj_set_style_shadow_width(m_container, 8, 0);
    lv_obj_set_style_shadow_ofs_y(m_container, 2, 0);
    lv_obj_set_style_shadow_opa(m_container, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(m_container, lv_color_black(), 0);
    
    // Layout flexbox vertical
    lv_obj_set_flex_flow(m_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

void Card::setTitle(const char* title) {
    if (!title) return;
    
    if (!m_title) {
        m_title = lv_label_create(m_container);
        lv_obj_set_style_text_font(m_title, theme::fontTitle(), 0);
        lv_obj_set_style_text_color(m_title, ThemeManager::instance().text(), 0);
        
        // Se tem ícone, título vai depois
        if (m_icon) {
            lv_obj_move_to_index(m_title, 1);
        }
    }
    
    lv_label_set_text(m_title, title);
}

void Card::setContent(const char* content) {
    if (!content) return;
    
    if (!m_content) {
        m_content = lv_label_create(m_container);
        lv_obj_set_style_text_font(m_content, theme::fontText(), 0);
        lv_obj_set_style_text_color(m_content, ThemeManager::instance().textSecondary(), 0);
        lv_obj_set_width(m_content, lv_pct(100));
        lv_label_set_long_mode(m_content, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_pad_top(m_content, 4, 0);
    }
    
    lv_label_set_text(m_content, content);
}

void Card::setIcon(const char* symbol) {
    if (!symbol) return;
    
    if (!m_icon) {
        m_icon = lv_label_create(m_container);
        lv_obj_set_style_text_font(m_icon, theme::fontIcon(), 0);
        lv_obj_set_style_text_color(m_icon, ThemeManager::instance().primary(), 0);
        
        // Ícone deve ser primeiro
        lv_obj_move_to_index(m_icon, 0);
    }
    
    lv_label_set_text(m_icon, symbol);
}

void Card::setOnClick(std::function<void()> callback) {
    if (m_clickCallback) {
        delete m_clickCallback;
    }
    
    m_clickCallback = new std::function<void()>(callback);
    
    lv_obj_add_flag(m_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(m_container, m_clickCallback);
    lv_obj_add_event_cb(m_container, clickHandler, LV_EVENT_CLICKED, m_clickCallback);
    
    // Efeito visual de clique
    theme::applyButtonStyle(m_container, ThemeManager::instance().surface());
}

void Card::setBackgroundColor(lv_color_t color) {
    lv_obj_set_style_bg_color(m_container, color, 0);
}

void Card::setShadowEnabled(bool enabled) {
    if (enabled) {
        lv_obj_set_style_shadow_width(m_container, 8, 0);
        lv_obj_set_style_shadow_opa(m_container, LV_OPA_20, 0);
    } else {
        lv_obj_set_style_shadow_width(m_container, 0, 0);
        lv_obj_set_style_shadow_opa(m_container, LV_OPA_TRANSP, 0);
    }
}

void Card::clickHandler(lv_event_t* e) {
    auto* callback = static_cast<std::function<void()>*>(lv_event_get_user_data(e));
    if (callback && *callback) {
        (*callback)();
    }
}

} // namespace ui
