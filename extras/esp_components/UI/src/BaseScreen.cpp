#include "BaseScreen.h"
#include "Theme.h"
#include "Layout.h"
#include "LvglUtils.h"
#include "esp_log.h"

static const char* TAG = "BaseScreen";

namespace ui {

BaseScreen::~BaseScreen() {
    hide();
}

void BaseScreen::show() {
    // Usa a animação de entrada da tela (pode ser customizada pela subclasse)
    showWithAnimation(getEnterAnimation());
}

void BaseScreen::showWithAnimation(anim::ScreenTransition transition) {
    if (m_screen != nullptr) {
        // Já criada, apenas carregar com animação
        if (anim::config().enabled && transition != anim::ScreenTransition::None) {
            lv_screen_load_anim(m_screen, 
                anim::toLvglAnim(transition),
                anim::config().screenTransitionMs,
                0,     // delay
                false  // não deletar tela anterior automaticamente
            );
        } else {
            lv_screen_load(m_screen);
        }
        return;
    }
    
    // Criar nova tela
    m_screen = lv_obj_create(nullptr);
    if (m_screen == nullptr) {
        ESP_LOGE(TAG, "Failed to create screen");
        return;
    }
    
    // Aplicar estilo padrão
    theme::applyScreenStyle(m_screen);
    
    // Chamar onCreate() da subclasse
    onCreate();
    
    // Carregar a tela com animação
    if (anim::config().enabled && transition != anim::ScreenTransition::None) {
        lv_screen_load_anim(m_screen, 
            anim::toLvglAnim(transition),
            anim::config().screenTransitionMs,
            0,     // delay
            true   // deletar tela anterior automaticamente
        );
    } else {
        lv_screen_load(m_screen);
    }
    
    ESP_LOGI(TAG, "Screen '%s' shown", getTitle());
}

void BaseScreen::hide() {
    if (m_screen == nullptr) {
        return;
    }
    
    // Chamar onDestroy() da subclasse
    onDestroy();
    
    // Resetar ponteiros antes de deletar
    m_contentContainer = nullptr;
    m_buttonBar = nullptr;
    
    // Deletar a tela de forma assíncrona para evitar problemas
    lv_obj_delete_async(m_screen);
    m_screen = nullptr;
    
    ESP_LOGI(TAG, "Screen '%s' hidden", getTitle());
}

void BaseScreen::hideWithAnimation(anim::ScreenTransition transition) {
    if (m_screen == nullptr) {
        return;
    }
    
    // Chamar onDestroy() da subclasse
    onDestroy();
    
    // Resetar ponteiros
    m_contentContainer = nullptr;
    m_buttonBar = nullptr;
    
    // Para animação de saída, precisamos de uma tela vazia como destino
    // A tela atual será deletada automaticamente pelo LVGL após a animação
    if (anim::config().enabled && transition != anim::ScreenTransition::None) {
        // Criar tela vazia temporária
        lv_obj_t* emptyScreen = lv_obj_create(nullptr);
        theme::applyScreenStyle(emptyScreen);
        
        // Carregar tela vazia com animação (deleta a tela atual)
        lv_screen_load_anim(emptyScreen, 
            anim::toLvglAnim(transition),
            anim::config().screenTransitionMs,
            0,    // delay
            true  // auto_del da tela anterior (m_screen)
        );
    } else {
        lv_obj_delete_async(m_screen);
    }
    
    m_screen = nullptr;
    ESP_LOGI(TAG, "Screen '%s' hidden with animation", getTitle());
}

// ============================================================================
// Animações Customizáveis
// ============================================================================

anim::ScreenTransition BaseScreen::getEnterAnimation() const {
    return anim::config().defaultEnter;
}

anim::ScreenTransition BaseScreen::getExitAnimation() const {
    return anim::config().defaultExit;
}

bool BaseScreen::isVisible() const {
    if (m_screen == nullptr) {
        return false;
    }
    return lv_screen_active() == m_screen;
}

void BaseScreen::setBackCallback(BackCallback cb) {
    m_backCallback = cb;
}

void BaseScreen::invokeBackCallback() {
    if (m_backCallback) {
        m_backCallback();
    }
}

void BaseScreen::detachScreen() {
    // Chama onDestroy para limpar recursos da subclasse
    if (m_screen != nullptr) {
        onDestroy();
    }
    
    // Resetar ponteiros mas NÃO deletar a tela LVGL
    // Isso permite que o LVGL gerencie a deleção automaticamente
    m_contentContainer = nullptr;
    m_buttonBar = nullptr;
    m_screen = nullptr;
    
    ESP_LOGI(TAG, "Screen detached (LVGL will manage deletion)");
}

// ============================================================================
// Criação de Elementos Comuns
// ============================================================================

lv_obj_t* BaseScreen::createTitle(const char* text) {
    if (m_screen == nullptr) return nullptr;
    
    const char* titleText = (text != nullptr) ? text : getTitle();
    
    lv_obj_t* title = lv_label_create(m_screen);
    lv_label_set_text(title, titleText);
    lv_obj_set_style_text_font(title, theme::fontTitle(), 0);
    lv_obj_set_style_text_color(title, theme::text(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    return title;
}

lv_obj_t* BaseScreen::createBackButton() {
    if (m_screen == nullptr) return nullptr;
    
    // Criar botão
    lv_obj_t* btn = lv_button_create(m_screen);
    lv_obj_set_size(btn, theme::BUTTON_W, theme::BUTTON_H);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -theme::BUTTON_BOTTOM_OFFSET);
    
    // Estilo do botão
    theme::applyButtonStyle(btn, theme::buttonGray());
    
    // Label do botão
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, "Voltar");
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_obj_center(label);
    
    // Armazenar ponteiro para this no user data
    lv_obj_set_user_data(btn, this);
    
    // Event handler
    lv_obj_add_event_cb(btn, backButtonEventHandler, LV_EVENT_CLICKED, this);
    
    return btn;
}

lv_obj_t* BaseScreen::createActionButton(const char* text, lv_color_t color,
                                          std::function<void()> onClick) {
    if (m_screen == nullptr) return nullptr;
    
    // Criar botão
    lv_obj_t* btn = lv_button_create(m_screen);
    lv_obj_set_size(btn, theme::BUTTON_W, theme::BUTTON_H);
    
    // Estilo do botão
    theme::applyButtonStyle(btn, color);
    
    // Label do botão
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_obj_center(label);
    
    // Armazenar callback
    if (onClick) {
        auto* cb = new std::function<void()>(onClick);
        lv_obj_set_user_data(btn, cb);
        lv_obj_add_event_cb(btn, actionButtonEventHandler, LV_EVENT_CLICKED, cb);
        
        // Limpar callback quando botão for deletado
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* callback = static_cast<std::function<void()>*>(lv_event_get_user_data(e));
            delete callback;
        }, LV_EVENT_DELETE, cb);
    }
    
    return btn;
}

lv_obj_t* BaseScreen::createContentContainer(bool scrollable) {
    if (m_screen == nullptr) return nullptr;
    
    // Criar container
    m_contentContainer = lv_obj_create(m_screen);
    
    // Calcular área de conteúdo
    auto area = layout::contentArea(true, true);
    lv_obj_set_pos(m_contentContainer, area.x, area.y);
    lv_obj_set_size(m_contentContainer, area.w, area.h);
    
    // Estilo do container
    lv_obj_set_style_bg_opa(m_contentContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_contentContainer, 0, 0);
    lv_obj_set_style_pad_all(m_contentContainer, 0, 0);
    
    // Configurar scroll
    if (scrollable) {
        lv_obj_set_scrollbar_mode(m_contentContainer, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(m_contentContainer, LV_DIR_VER);
    } else {
        lv_obj_clear_flag(m_contentContainer, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    return m_contentContainer;
}

lv_obj_t* BaseScreen::createButtonBar() {
    if (m_screen == nullptr) return nullptr;
    
    // Criar container para botões
    m_buttonBar = lv_obj_create(m_screen);
    
    // Posicionar na parte inferior
    int32_t barHeight = theme::BUTTON_H + theme::PADDING;
    lv_obj_set_size(m_buttonBar, theme::SCREEN_W - 2 * theme::PADDING, barHeight);
    lv_obj_align(m_buttonBar, LV_ALIGN_BOTTOM_MID, 0, -theme::PADDING);
    
    // Estilo do container
    lv_obj_set_style_bg_opa(m_buttonBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_buttonBar, 0, 0);
    lv_obj_set_style_pad_all(m_buttonBar, 0, 0);
    lv_obj_clear_flag(m_buttonBar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Layout flexbox para distribuir botões
    lv_obj_set_flex_flow(m_buttonBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_buttonBar, LV_FLEX_ALIGN_SPACE_EVENLY, 
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    return m_buttonBar;
}

lv_obj_t* BaseScreen::createStatusLabel(const char* text) {
    if (m_screen == nullptr) return nullptr;
    
    lv_obj_t* label = lv_label_create(m_screen);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, theme::fontCaption(), 0);
    lv_obj_set_style_text_color(label, theme::textSecondary(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, theme::SCREEN_W - 2 * theme::PADDING);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    
    // Posicionar logo abaixo do título
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, layout::titleHeight() + 5);
    
    return label;
}

// ============================================================================
// Widgets Comuns
// ============================================================================

lv_obj_t* BaseScreen::createLabel(const char* text, LabelStyle style) {
    if (m_screen == nullptr) return nullptr;
    
    lv_obj_t* label = lv_label_create(m_screen);
    lv_label_set_text(label, text);
    
    switch (style) {
        case LabelStyle::Title:
            lv_obj_set_style_text_font(label, theme::fontTitle(), 0);
            lv_obj_set_style_text_color(label, theme::text(), 0);
            break;
        case LabelStyle::Normal:
            lv_obj_set_style_text_font(label, theme::fontText(), 0);
            lv_obj_set_style_text_color(label, theme::text(), 0);
            break;
        case LabelStyle::Caption:
            lv_obj_set_style_text_font(label, theme::fontCaption(), 0);
            lv_obj_set_style_text_color(label, theme::textSecondary(), 0);
            break;
        case LabelStyle::Value:
            lv_obj_set_style_text_font(label, theme::fontText(), 0);
            lv_obj_set_style_text_color(label, theme::text(), 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            break;
    }
    
    return label;
}

lv_obj_t* BaseScreen::createSwitch(bool initialState, std::function<void(bool)> onChange) {
    if (m_screen == nullptr) return nullptr;
    
    lv_obj_t* sw = lv_switch_create(m_screen);
    lv_obj_set_size(sw, 50, 25);
    
    if (initialState) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    
    if (onChange) {
        auto* cb = new std::function<void(bool)>(onChange);
        lv_obj_set_user_data(sw, cb);
        lv_obj_add_event_cb(sw, switchEventHandler, LV_EVENT_VALUE_CHANGED, cb);
        lv_obj_add_event_cb(sw, cleanupCallback, LV_EVENT_DELETE, cb);
    }
    
    return sw;
}

lv_obj_t* BaseScreen::createSlider(int32_t min, int32_t max, int32_t initial,
                                   std::function<void(int32_t)> onChange) {
    if (m_screen == nullptr) return nullptr;
    
    lv_obj_t* slider = lv_slider_create(m_screen);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, initial, LV_ANIM_OFF);
    lv_obj_set_height(slider, 20);
    
    if (onChange) {
        auto* cb = new std::function<void(int32_t)>(onChange);
        lv_obj_set_user_data(slider, cb);
        lv_obj_add_event_cb(slider, sliderEventHandler, LV_EVENT_VALUE_CHANGED, cb);
        lv_obj_add_event_cb(slider, cleanupCallback, LV_EVENT_DELETE, cb);
    }
    
    return slider;
}

lv_obj_t* BaseScreen::createInputButton(const char* placeholder,
                                        std::function<void()> onEdit,
                                        lv_obj_t** labelOut) {
    if (m_screen == nullptr) return nullptr;
    
    // Criar botão estilizado como campo de input
    lv_obj_t* btn = lv_button_create(m_screen);
    lv_obj_set_size(btn, 200, theme::INPUT_H);
    
    // Estilo de input (fundo branco, borda cinza)
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, theme::border(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, theme::RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    
    // Label interno
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, placeholder ? placeholder : "");
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_obj_set_style_text_color(label, theme::textSecondary(), 0);
    lv_obj_center(label);
    
    if (labelOut) {
        *labelOut = label;
    }
    
    if (onEdit) {
        auto* cb = new std::function<void()>(onEdit);
        lv_obj_set_user_data(btn, cb);
        lv_obj_add_event_cb(btn, inputButtonEventHandler, LV_EVENT_CLICKED, cb);
        lv_obj_add_event_cb(btn, cleanupCallback, LV_EVENT_DELETE, cb);
    }
    
    return btn;
}

lv_obj_t* BaseScreen::createProgressBar(int32_t min, int32_t max) {
    if (m_screen == nullptr) return nullptr;
    
    lv_obj_t* bar = lv_bar_create(m_screen);
    lv_bar_set_range(bar, min, max);
    lv_bar_set_value(bar, min, LV_ANIM_OFF);
    lv_obj_set_size(bar, theme::SCREEN_W - 2 * theme::PADDING_H, 20);
    
    // Estilo da barra
    lv_obj_set_style_bg_color(bar, theme::border(), 0);
    lv_obj_set_style_bg_color(bar, theme::primary(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, theme::RADIUS_SM, 0);
    lv_obj_set_style_radius(bar, theme::RADIUS_SM, LV_PART_INDICATOR);
    
    return bar;
}

// ============================================================================
// Layouts Compostos (Row com Label + Widget)
// ============================================================================

LabeledWidget BaseScreen::createLabeledSwitch(const char* labelText, bool initial,
                                              std::function<void(bool)> onChange) {
    LabeledWidget result = {nullptr, nullptr, nullptr};
    if (m_screen == nullptr) return result;
    
    // Criar container row (sem flex para evitar conflito com posicionamento manual)
    lv_obj_t* row = lv_obj_create(m_screen);
    lv_obj_set_size(row, theme::SCREEN_W - 2 * theme::PADDING, 40);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Label à esquerda
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, labelText);
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_obj_set_style_text_color(label, theme::text(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Switch à direita
    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_size(sw, 50, 25);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    
    if (initial) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    
    if (onChange) {
        auto* cb = new std::function<void(bool)>(onChange);
        lv_obj_set_user_data(sw, cb);
        lv_obj_add_event_cb(sw, switchEventHandler, LV_EVENT_VALUE_CHANGED, cb);
        lv_obj_add_event_cb(sw, cleanupCallback, LV_EVENT_DELETE, cb);
    }
    
    result.row = row;
    result.label = label;
    result.widget = sw;
    
    return result;
}

LabeledWidget BaseScreen::createLabeledSlider(const char* labelText,
                                              int32_t min, int32_t max, int32_t initial,
                                              std::function<void(int32_t)> onChange) {
    LabeledWidget result = {nullptr, nullptr, nullptr};
    if (m_screen == nullptr) return result;
    
    // Criar container row (sem flex para evitar conflito com posicionamento manual)
    lv_obj_t* row = lv_obj_create(m_screen);
    lv_obj_set_size(row, theme::SCREEN_W - 2 * theme::PADDING, 60);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Label no topo
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, labelText);
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_obj_set_style_text_color(label, theme::text(), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Slider abaixo do label
    lv_obj_t* slider = lv_slider_create(row);
    lv_obj_set_width(slider, theme::SCREEN_W - 2 * theme::PADDING - 10);
    lv_obj_set_height(slider, 20);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, initial, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    if (onChange) {
        auto* cb = new std::function<void(int32_t)>(onChange);
        lv_obj_set_user_data(slider, cb);
        lv_obj_add_event_cb(slider, sliderEventHandler, LV_EVENT_VALUE_CHANGED, cb);
        lv_obj_add_event_cb(slider, cleanupCallback, LV_EVENT_DELETE, cb);
    }
    
    result.row = row;
    result.label = label;
    result.widget = slider;
    
    return result;
}

LabeledWidget BaseScreen::createLabeledInput(const char* labelText,
                                             const char* placeholder,
                                             std::function<void()> onEdit) {
    LabeledWidget result = {nullptr, nullptr, nullptr};
    if (m_screen == nullptr) return result;
    
    // Criar container row (sem flex para evitar conflito com posicionamento manual)
    lv_obj_t* row = lv_obj_create(m_screen);
    int32_t rowWidth = theme::SCREEN_W - 2 * theme::PADDING;
    lv_obj_set_size(row, rowWidth, theme::INPUT_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Label à esquerda (largura fixa)
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, labelText);
    lv_obj_set_style_text_font(label, theme::fontText(), 0);
    lv_obj_set_style_text_color(label, theme::text(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Input button à direita
    int32_t labelWidth = 70;
    int32_t btnWidth = rowWidth - labelWidth - 8;
    lv_obj_t* btn = lv_button_create(row);
    lv_obj_set_size(btn, btnWidth, theme::INPUT_H - 4);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // Estilo de input
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, theme::border(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, theme::RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    
    // Label interno do botão
    lv_obj_t* btnLabel = lv_label_create(btn);
    lv_label_set_text(btnLabel, placeholder ? placeholder : "");
    lv_obj_set_style_text_font(btnLabel, theme::fontText(), 0);
    lv_obj_set_style_text_color(btnLabel, theme::textSecondary(), 0);
    lv_obj_center(btnLabel);
    
    if (onEdit) {
        auto* cb = new std::function<void()>(onEdit);
        lv_obj_set_user_data(btn, cb);
        lv_obj_add_event_cb(btn, inputButtonEventHandler, LV_EVENT_CLICKED, cb);
        lv_obj_add_event_cb(btn, cleanupCallback, LV_EVENT_DELETE, cb);
    }
    
    result.row = row;
    result.label = label;
    result.widget = btn;
    
    return result;
}

// ============================================================================
// Helpers de Layout Relativo
// ============================================================================

void BaseScreen::placeAt(lv_obj_t* obj, float relX, float relY) {
    layout::setPosition(obj, relX, relY);
}

void BaseScreen::setSize(lv_obj_t* obj, float relW, float relH) {
    layout::setSize(obj, relW, relH);
}

void BaseScreen::setWidth(lv_obj_t* obj, float relW) {
    layout::setWidth(obj, relW);
}

void BaseScreen::setHeight(lv_obj_t* obj, float relH) {
    layout::setHeight(obj, relH);
}

void BaseScreen::placeBelow(lv_obj_t* obj, lv_obj_t* reference, float relGap) {
    layout::placeBelow(obj, reference, relGap);
}

void BaseScreen::placeRightOf(lv_obj_t* obj, lv_obj_t* reference, float relGap) {
    layout::placeRightOf(obj, reference, relGap);
}

void BaseScreen::centerHorizontally(lv_obj_t* obj) {
    int32_t objW = lv_obj_get_width(obj);
    lv_obj_set_x(obj, layout::centerX(objW));
}

layout::Rect BaseScreen::getContentArea(bool hasTitle, bool hasFooter) const {
    return layout::contentArea(hasTitle, hasFooter);
}

// ============================================================================
// Event Handlers
// ============================================================================

void BaseScreen::backButtonEventHandler(lv_event_t* e) {
    auto* screen = static_cast<BaseScreen*>(lv_event_get_user_data(e));
    if (screen) {
        screen->invokeBackCallback();
    }
}

void BaseScreen::actionButtonEventHandler(lv_event_t* e) {
    auto* callback = static_cast<std::function<void()>*>(lv_event_get_user_data(e));
    if (callback && *callback) {
        (*callback)();
    }
}

void BaseScreen::switchEventHandler(lv_event_t* e) {
    auto* callback = static_cast<std::function<void(bool)>*>(lv_event_get_user_data(e));
    if (callback && *callback) {
        lv_obj_t* sw = lv_event_get_target_obj(e);
        bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
        (*callback)(checked);
    }
}

void BaseScreen::sliderEventHandler(lv_event_t* e) {
    auto* callback = static_cast<std::function<void(int32_t)>*>(lv_event_get_user_data(e));
    if (callback && *callback) {
        lv_obj_t* slider = lv_event_get_target_obj(e);
        int32_t value = lv_slider_get_value(slider);
        (*callback)(value);
    }
}

void BaseScreen::inputButtonEventHandler(lv_event_t* e) {
    auto* callback = static_cast<std::function<void()>*>(lv_event_get_user_data(e));
    if (callback && *callback) {
        (*callback)();
    }
}

void BaseScreen::cleanupCallback(lv_event_t* e) {
    // Cleanup genérico para callbacks alocados dinamicamente
    void* userData = lv_event_get_user_data(e);
    if (userData) {
        // Não podemos saber o tipo exato, mas todos são std::function
        // que podem ser deletados como void*
        delete static_cast<std::function<void()>*>(userData);
    }
}

} // namespace ui
