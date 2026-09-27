#include "ScrollableList.h"
#include "Theme.h"
#include "esp_log.h"

static const char* TAG = "ScrollableList";

namespace ui {

ScrollableList::ScrollableList(lv_obj_t* parent, int32_t width, int32_t height) {
    // Determinar largura
    m_width = (width > 0) ? width : (theme::SCREEN_W - 2 * theme::PADDING);
    
    // Criar container principal
    m_container = lv_obj_create(parent);
    
    // Tamanho
    lv_obj_set_width(m_container, m_width);
    if (height > 0) {
        lv_obj_set_height(m_container, height);
    } else {
        lv_obj_set_height(m_container, LV_SIZE_CONTENT);
    }
    
    // Estilo do container
    lv_obj_set_style_bg_color(m_container, theme::surface(), 0);
    lv_obj_set_style_bg_opa(m_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(m_container, theme::border(), 0);
    lv_obj_set_style_border_width(m_container, 1, 0);
    lv_obj_set_style_radius(m_container, theme::RADIUS_MD, 0);
    lv_obj_set_style_pad_all(m_container, theme::PADDING, 0);
    lv_obj_set_style_pad_row(m_container, m_itemGap, 0);
    
    // Layout flex vertical
    lv_obj_set_flex_flow(m_container, LV_FLEX_FLOW_COLUMN);
    
    // Scroll
    lv_obj_set_scrollbar_mode(m_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(m_container, LV_DIR_VER);
}

ScrollableList::~ScrollableList() {
    // Items são filhos do container, serão deletados automaticamente
    m_items.clear();
}

int ScrollableList::addItem(const char* title, const char* subtitle,
                            SelectCallback onClick, void* userData) {
    InternalItem item;
    item.data.title = title ? title : "";
    item.data.subtitle = subtitle ? subtitle : "";
    item.data.onClick = onClick;
    item.data.userData = userData;
    
    int index = static_cast<int>(m_items.size());
    m_items.push_back(item);
    
    // Criar elemento visual
    createItemButton(m_items.back(), index);
    
    return index;
}

void ScrollableList::clear() {
    // Limpar container LVGL
    lv_obj_clean(m_container);
    
    // Limpar dados
    m_items.clear();
}

const ScrollableList::Item* ScrollableList::getItem(int index) const {
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return nullptr;
    }
    return &m_items[index].data;
}

void ScrollableList::setOnSelect(SelectCallback callback) {
    m_globalCallback = callback;
}

void ScrollableList::scrollToTop() {
    lv_obj_scroll_to_y(m_container, 0, LV_ANIM_ON);
}

void ScrollableList::scrollToItem(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return;
    }
    
    if (m_items[index].button != nullptr) {
        lv_obj_scroll_to_view(m_items[index].button, LV_ANIM_ON);
    }
}

void ScrollableList::setItemHeight(int32_t height) {
    m_itemHeight = height;
}

void ScrollableList::setItemGap(int32_t gap) {
    m_itemGap = gap;
    lv_obj_set_style_pad_row(m_container, gap, 0);
}

bool ScrollableList::updateItem(int index, const char* title, const char* subtitle) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return false;
    }
    
    auto& item = m_items[index];
    
    if (title != nullptr) {
        item.data.title = title;
        if (item.titleLabel != nullptr) {
            lv_label_set_text(item.titleLabel, title);
        }
    }
    
    if (subtitle != nullptr) {
        item.data.subtitle = subtitle;
        if (item.subtitleLabel != nullptr) {
            lv_label_set_text(item.subtitleLabel, subtitle);
        }
    }
    
    return true;
}

void ScrollableList::createItemButton(InternalItem& item, int index) {
    // Criar botão
    item.button = lv_button_create(m_container);
    lv_obj_set_width(item.button, lv_pct(100));
    
    // Altura baseada em se tem subtítulo ou não
    int32_t btnHeight = item.data.subtitle.empty() ? m_itemHeight : (m_itemHeight + 16);
    lv_obj_set_height(item.button, btnHeight);
    
    // Estilo do botão
    lv_obj_set_style_bg_color(item.button, theme::surface(), 0);
    lv_obj_set_style_bg_color(item.button, theme::border(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item.button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(item.button, theme::border(), 0);
    lv_obj_set_style_border_width(item.button, 1, 0);
    lv_obj_set_style_radius(item.button, theme::RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(item.button, theme::PADDING, 0);
    lv_obj_set_style_pad_ver(item.button, 4, 0);
    
    // Layout interno
    if (item.data.subtitle.empty()) {
        // Apenas título - centralizar verticalmente
        item.titleLabel = lv_label_create(item.button);
        lv_label_set_text(item.titleLabel, item.data.title.c_str());
        lv_obj_set_style_text_font(item.titleLabel, theme::fontText(), 0);
        lv_obj_set_style_text_color(item.titleLabel, theme::text(), 0);
        lv_label_set_long_mode(item.titleLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(item.titleLabel, lv_pct(100));
        lv_obj_center(item.titleLabel);
    } else {
        // Título + subtítulo - layout vertical
        lv_obj_set_flex_flow(item.button, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item.button, LV_FLEX_ALIGN_CENTER, 
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        
        // Título
        item.titleLabel = lv_label_create(item.button);
        lv_label_set_text(item.titleLabel, item.data.title.c_str());
        lv_obj_set_style_text_font(item.titleLabel, theme::fontText(), 0);
        lv_obj_set_style_text_color(item.titleLabel, theme::text(), 0);
        lv_label_set_long_mode(item.titleLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(item.titleLabel, lv_pct(100));
        
        // Subtítulo
        item.subtitleLabel = lv_label_create(item.button);
        lv_label_set_text(item.subtitleLabel, item.data.subtitle.c_str());
        lv_obj_set_style_text_font(item.subtitleLabel, theme::fontCaption(), 0);
        lv_obj_set_style_text_color(item.subtitleLabel, theme::textSecondary(), 0);
        lv_label_set_long_mode(item.subtitleLabel, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(item.subtitleLabel, lv_pct(100));
    }
    
    // Armazenar índice e ponteiro para this no user_data
    // Usamos um struct simples para passar ambos
    struct ItemData {
        ScrollableList* list;
        int index;
    };
    auto* itemData = new ItemData{this, index};
    lv_obj_set_user_data(item.button, itemData);
    
    // Event handler
    lv_obj_add_event_cb(item.button, itemClickHandler, LV_EVENT_CLICKED, itemData);
    
    // Limpar dados quando botão for deletado
    lv_obj_add_event_cb(item.button, [](lv_event_t* e) {
        auto* data = static_cast<ItemData*>(lv_event_get_user_data(e));
        delete data;
    }, LV_EVENT_DELETE, itemData);
}

void ScrollableList::itemClickHandler(lv_event_t* e) {
    struct ItemData {
        ScrollableList* list;
        int index;
    };
    
    auto* data = static_cast<ItemData*>(lv_event_get_user_data(e));
    if (data == nullptr || data->list == nullptr) {
        return;
    }
    
    int index = data->index;
    auto* list = data->list;
    
    ESP_LOGD(TAG, "Item %d clicked", index);
    
    // Verificar se tem callback individual
    if (index >= 0 && index < static_cast<int>(list->m_items.size())) {
        const auto& item = list->m_items[index];
        if (item.data.onClick) {
            item.data.onClick(index);
            return;
        }
    }
    
    // Usar callback global se disponível
    if (list->m_globalCallback) {
        list->m_globalCallback(index);
    }
}

} // namespace ui
