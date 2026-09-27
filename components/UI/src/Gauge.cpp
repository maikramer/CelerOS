#include "Gauge.h"
#include "Theme.h"
#include "ThemeManager.h"
#include "Layout.h"

namespace ui {

Gauge::Gauge(lv_obj_t* parent, int32_t width) {
    auto& tm = ThemeManager::instance();
    
    // Container principal
    m_container = lv_obj_create(parent);
    lv_obj_remove_style_all(m_container);
    
    if (width > 0) {
        lv_obj_set_width(m_container, width);
    } else {
        lv_obj_set_width(m_container, lv_pct(100));
    }
    lv_obj_set_height(m_container, LV_SIZE_CONTENT);
    
    // Layout vertical
    lv_obj_set_flex_flow(m_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(m_container, 4, 0);
    
    // Label do título (inicialmente oculto)
    m_labelObj = lv_label_create(m_container);
    lv_obj_set_style_text_font(m_labelObj, theme::fontCaption(), 0);
    lv_obj_set_style_text_color(m_labelObj, tm.textSecondary(), 0);
    lv_label_set_text(m_labelObj, "");
    lv_obj_add_flag(m_labelObj, LV_OBJ_FLAG_HIDDEN);
    
    // Label do valor
    m_valueLabel = lv_label_create(m_container);
    lv_obj_set_style_text_font(m_valueLabel, theme::fontTitle(), 0);
    lv_obj_set_style_text_color(m_valueLabel, tm.text(), 0);
    updateValueLabel();
    
    // Barra de progresso
    m_bar = lv_bar_create(m_container);
    lv_obj_set_width(m_bar, lv_pct(100));
    lv_obj_set_height(m_bar, layout::responsive(6, 8, 10));
    lv_bar_set_range(m_bar, m_min, m_max);
    lv_bar_set_value(m_bar, m_value, LV_ANIM_OFF);
    
    // Estilo da barra
    int32_t radius = layout::responsive(3, 4, 5);
    lv_obj_set_style_bg_color(m_bar, tm.border(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(m_bar, radius, LV_PART_MAIN);
    
    lv_obj_set_style_bg_color(m_bar, tm.primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(m_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_bar, radius, LV_PART_INDICATOR);
}

void Gauge::setRange(int32_t min, int32_t max) {
    m_min = min;
    m_max = max;
    
    if (m_bar) {
        lv_bar_set_range(m_bar, min, max);
    }
    
    updateValueLabel();
    updateBarColor();
}

void Gauge::setValue(int32_t value) {
    m_value = value;
    
    if (m_bar) {
        lv_bar_set_value(m_bar, value, LV_ANIM_ON);
    }
    
    updateValueLabel();
    updateBarColor();
}

void Gauge::setLabel(const char* label) {
    if (!m_labelObj) return;
    
    if (label && label[0] != '\0') {
        lv_label_set_text(m_labelObj, label);
        lv_obj_clear_flag(m_labelObj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_labelObj, LV_OBJ_FLAG_HIDDEN);
    }
}

void Gauge::setUnit(const char* unit) {
    m_unit = unit;
    updateValueLabel();
}

void Gauge::setColor(lv_color_t color) {
    m_autoColor = false;
    if (m_bar) {
        lv_obj_set_style_bg_color(m_bar, color, LV_PART_INDICATOR);
    }
}

void Gauge::setAutoColor(int32_t warningThreshold, int32_t criticalThreshold) {
    m_autoColor = true;
    m_warnThreshold = warningThreshold;
    m_critThreshold = criticalThreshold;
    updateBarColor();
}

void Gauge::updateValueLabel() {
    if (!m_valueLabel) return;
    
    if (m_unit) {
        lv_label_set_text_fmt(m_valueLabel, "%ld %s", m_value, m_unit);
    } else {
        lv_label_set_text_fmt(m_valueLabel, "%ld", m_value);
    }
}

void Gauge::updateBarColor() {
    if (!m_bar || !m_autoColor) return;
    
    auto& tm = ThemeManager::instance();
    int32_t percent = getPercentage();
    
    lv_color_t color;
    if (percent >= m_critThreshold) {
        color = tm.error();
    } else if (percent >= m_warnThreshold) {
        color = tm.warning();
    } else {
        color = tm.success();
    }
    
    lv_obj_set_style_bg_color(m_bar, color, LV_PART_INDICATOR);
}

int32_t Gauge::getPercentage() const {
    if (m_max == m_min) return 0;
    return ((m_value - m_min) * 100) / (m_max - m_min);
}

} // namespace ui
