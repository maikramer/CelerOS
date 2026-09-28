#include "InfoList.h"
#include "Theme.h"
#include "LvglUtils.h"
#include "esp_log.h"

static const char* TAG = "InfoList";

namespace ui {

InfoList::InfoList(lv_obj_t* parent, int32_t width, int32_t height) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "Parent is null");
        return;
    }
    
    // Calcular dimensões padrão
    m_width = (width > 0) ? width : (theme::SCREEN_W - 2 * theme::PADDING_H);
    int32_t containerHeight = (height > 0) ? height : 
        (theme::SCREEN_H - theme::HEADER_H - theme::BUTTON_H - theme::BUTTON_BOTTOM_OFFSET - 20);
    
    // Criar container scrollável
    m_container = lv_obj_create(parent);
    lv_obj_set_size(m_container, m_width, containerHeight);
    lv_obj_align(m_container, LV_ALIGN_TOP_MID, 0, theme::HEADER_H + 5);
    lv_obj_set_style_bg_opa(m_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_container, 0, 0);
    lv_obj_set_style_pad_all(m_container, 0, 0);
    lv_obj_add_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(m_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(m_container, LV_SCROLLBAR_MODE_AUTO);
    
    m_yPos = 5;  // Margem inicial
    
    ESP_LOGI(TAG, "InfoList created with width %d, height %d", (int)m_width, (int)containerHeight);
}

InfoList::~InfoList() {
    // Container será deletado pelo LVGL quando o pai for deletado
    m_items.clear();
}

size_t InfoList::addItem(const char* label, const char* value) {
    if (m_container == nullptr) {
        ESP_LOGE(TAG, "Container is null");
        return 0;
    }
    
    Item item;
    item.label = label ? label : "";
    item.value = value ? value : "";
    
    // Label (texto menor, cinza)
    item.labelObj = lv_label_create(m_container);
    lv_label_set_text(item.labelObj, label);
    lv_obj_set_style_text_font(item.labelObj, theme::fontCaption(), 0);
    lv_obj_set_style_text_color(item.labelObj, theme::textSecondary(), 0);
    lv_obj_set_pos(item.labelObj, 5, m_yPos);
    
    // Value (texto maior, preto)
    item.valueObj = lv_label_create(m_container);
    lv_label_set_text(item.valueObj, value);
    lv_label_set_long_mode(item.valueObj, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(item.valueObj, m_width - 15);
    lv_obj_set_style_text_font(item.valueObj, theme::fontText(), 0);
    lv_obj_set_style_text_color(item.valueObj, theme::text(), 0);
    lv_obj_set_pos(item.valueObj, 5, m_yPos + LABEL_VALUE_GAP);
    
    m_yPos += LINE_SPACING;
    
    size_t index = m_items.size();
    m_items.push_back(item);
    
    return index;
}

void InfoList::addSeparator() {
    if (m_container == nullptr) return;
    
    lv_obj_t* line = lv_obj_create(m_container);
    lv_obj_set_size(line, m_width - 20, 1);
    lv_obj_set_style_bg_color(line, theme::border(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_pos(line, 5, m_yPos);
    
    m_yPos += SECTION_SPACING;
}

void InfoList::addSectionTitle(const char* title) {
    if (m_container == nullptr) return;
    
    m_yPos += 5;  // Espaço extra antes
    
    lv_obj_t* titleLabel = lv_label_create(m_container);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_font(titleLabel, theme::fontTitle(), 0);
    lv_obj_set_style_text_color(titleLabel, theme::primary(), 0);
    lv_obj_set_pos(titleLabel, 5, m_yPos);
    
    m_yPos += 28;  // Espaço depois do título
}

bool InfoList::updateValue(size_t index, const char* value) {
    if (index >= m_items.size()) {
        ESP_LOGW(TAG, "Invalid index %d", (int)index);
        return false;
    }
    
    Item& item = m_items[index];
    if (item.valueObj == nullptr) {
        return false;
    }
    
    item.value = value ? value : "";
    lv_label_set_text(item.valueObj, item.value.c_str());
    
    return true;
}

int32_t InfoList::contentHeight() const {
    return m_yPos;
}

void InfoList::scrollToTop() {
    if (m_container) {
        lv_obj_scroll_to_y(m_container, 0, LV_ANIM_ON);
    }
}

void InfoList::scrollToItem(size_t index) {
    if (m_container == nullptr || index >= m_items.size()) {
        return;
    }
    
    // Calcular posição aproximada do item
    int32_t y = static_cast<int32_t>(index) * LINE_SPACING;
    lv_obj_scroll_to_y(m_container, y, LV_ANIM_ON);
}

} // namespace ui
