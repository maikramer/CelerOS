#include "ProgressRing.h"
#include "Theme.h"
#include "ThemeManager.h"

namespace ui {

ProgressRing::ProgressRing(lv_obj_t* parent, int32_t diameter) 
    : m_diameter(diameter) {
    
    // Container
    m_container = lv_obj_create(parent);
    lv_obj_remove_style_all(m_container);
    lv_obj_set_size(m_container, diameter, diameter);
    lv_obj_clear_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);
    
    createBar();
}

ProgressRing::~ProgressRing() {
    stopIndeterminateAnimation();
}

void ProgressRing::createBar() {
    // Usa lv_bar como alternativa ao lv_arc que não está disponível
    // Cria uma barra de progresso circular usando estilo
    m_bar = lv_bar_create(m_container);
    
    // Tamanho da barra (horizontal, mas vamos estilizar)
    int32_t barHeight = m_diameter / 6;
    lv_obj_set_size(m_bar, m_diameter - 8, barHeight);
    lv_obj_center(m_bar);
    
    // Range e valor
    lv_bar_set_range(m_bar, 0, 100);
    lv_bar_set_value(m_bar, m_value, LV_ANIM_OFF);
    
    // Estilo
    auto& tm = ThemeManager::instance();
    
    // Background da barra
    lv_obj_set_style_bg_color(m_bar, tm.border(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(m_bar, barHeight / 2, LV_PART_MAIN);
    
    // Indicador
    lv_obj_set_style_bg_color(m_bar, tm.primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(m_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_bar, barHeight / 2, LV_PART_INDICATOR);
}

void ProgressRing::setValue(int32_t percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    m_value = percent;
    
    if (!m_indeterminate && m_bar) {
        lv_bar_set_value(m_bar, percent, LV_ANIM_ON);
    }
    
    updateLabel();
}

void ProgressRing::setColor(lv_color_t color) {
    if (m_bar) {
        lv_obj_set_style_bg_color(m_bar, color, LV_PART_INDICATOR);
    }
}

void ProgressRing::setBackgroundColor(lv_color_t color) {
    if (m_bar) {
        lv_obj_set_style_bg_color(m_bar, color, LV_PART_MAIN);
    }
}

void ProgressRing::setLineWidth(int32_t width) {
    if (m_bar) {
        lv_obj_set_height(m_bar, width);
        lv_obj_set_style_radius(m_bar, width / 2, LV_PART_MAIN);
        lv_obj_set_style_radius(m_bar, width / 2, LV_PART_INDICATOR);
    }
}

void ProgressRing::setIndeterminate(bool enabled) {
    if (m_indeterminate == enabled) return;
    
    m_indeterminate = enabled;
    
    if (enabled) {
        startIndeterminateAnimation();
    } else {
        stopIndeterminateAnimation();
        setValue(m_value);  // Restaura valor
    }
}

void ProgressRing::startIndeterminateAnimation() {
    if (!m_bar) return;
    
    // Animação de ida e volta
    lv_anim_init(&m_anim);
    lv_anim_set_var(&m_anim, m_bar);
    lv_anim_set_values(&m_anim, 0, 100);
    lv_anim_set_duration(&m_anim, 1000);
    lv_anim_set_playback_duration(&m_anim, 1000);
    lv_anim_set_repeat_count(&m_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&m_anim, [](void* var, int32_t v) {
        lv_bar_set_value(static_cast<lv_obj_t*>(var), v, LV_ANIM_OFF);
    });
    lv_anim_set_path_cb(&m_anim, lv_anim_path_ease_in_out);
    lv_anim_start(&m_anim);
}

void ProgressRing::stopIndeterminateAnimation() {
    if (m_bar) {
        lv_anim_delete(m_bar, nullptr);
    }
}

void ProgressRing::showPercentLabel(bool show) {
    m_showPercent = show;
    
    if (show && !m_label) {
        m_label = lv_label_create(m_container);
        lv_obj_set_style_text_font(m_label, theme::fontCaption(), 0);
        lv_obj_set_style_text_color(m_label, ThemeManager::instance().text(), 0);
        lv_obj_align(m_label, LV_ALIGN_BOTTOM_MID, 0, -2);
    } else if (!show && m_label) {
        lv_obj_delete(m_label);
        m_label = nullptr;
    }
    
    updateLabel();
}

void ProgressRing::setCenterText(const char* text) {
    if (!m_label) {
        m_label = lv_label_create(m_container);
        lv_obj_set_style_text_font(m_label, theme::fontCaption(), 0);
        lv_obj_set_style_text_color(m_label, ThemeManager::instance().text(), 0);
        lv_obj_align(m_label, LV_ALIGN_BOTTOM_MID, 0, -2);
    }
    
    if (text) {
        lv_label_set_text(m_label, text);
        m_showPercent = false;
    }
}

void ProgressRing::updateLabel() {
    if (!m_label || !m_showPercent) return;
    
    if (m_indeterminate) {
        lv_label_set_text(m_label, "...");
    } else {
        lv_label_set_text_fmt(m_label, "%ld%%", static_cast<long>(m_value));
    }
}

} // namespace ui
