#include "Dialog.h"
#include "Theme.h"
#include "LvglUtils.h"
#include "esp_log.h"

static const char* TAG = "Dialog";

namespace ui {

// Variáveis estáticas
lv_obj_t* Dialog::s_overlay = nullptr;
lv_obj_t* Dialog::s_dialog = nullptr;
lv_obj_t* Dialog::s_loadingDialog = nullptr;
std::function<void()> Dialog::s_onOk = nullptr;
std::function<void()> Dialog::s_onCancel = nullptr;

// Constantes de layout
static constexpr int32_t DIALOG_W = 280;
static constexpr int32_t DIALOG_H = 160;
static constexpr int32_t DIALOG_BTN_W = 100;
static constexpr int32_t DIALOG_BTN_H = 36;
static constexpr int32_t DIALOG_BTN_GAP = 10;

void Dialog::createOverlay() {
    if (s_overlay != nullptr) {
        // Já existe
        return;
    }
    
    lv_obj_t* activeScreen = lv_screen_active();
    if (activeScreen == nullptr) {
        ESP_LOGE(TAG, "No active screen");
        return;
    }
    
    // Criar overlay semi-transparente
    s_overlay = lv_obj_create(activeScreen);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, theme::SCREEN_W, theme::SCREEN_H);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
}

void Dialog::show(Type type, const char* title, const char* message,
                  std::function<void()> onOk) {
    // Fechar dialog anterior se houver
    close();
    
    s_onOk = onOk;
    s_onCancel = nullptr;
    
    // Criar overlay
    createOverlay();
    if (s_overlay == nullptr) return;
    
    // Criar container do dialog
    s_dialog = lv_obj_create(s_overlay);
    lv_obj_set_size(s_dialog, DIALOG_W, DIALOG_H);
    lv_obj_align(s_dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_dialog, theme::surface(), 0);
    lv_obj_set_style_bg_opa(s_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_dialog, theme::RADIUS_MD, 0);
    lv_obj_set_style_border_width(s_dialog, 1, 0);
    lv_obj_set_style_border_color(s_dialog, theme::border(), 0);
    lv_obj_set_style_shadow_width(s_dialog, 20, 0);
    lv_obj_set_style_shadow_opa(s_dialog, LV_OPA_30, 0);
    lv_obj_clear_flag(s_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Cor do título baseada no tipo
    lv_color_t titleColor;
    switch (type) {
        case Type::Info:
            titleColor = theme::primary();
            break;
        case Type::Warning:
            titleColor = theme::warning();
            break;
        case Type::Error:
            titleColor = theme::error();
            break;
        case Type::Success:
            titleColor = theme::success();
            break;
    }
    
    // Título
    lv_obj_t* titleLabel = lv_label_create(s_dialog);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_font(titleLabel, theme::fontTitle(), 0);
    lv_obj_set_style_text_color(titleLabel, titleColor, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 12);
    
    // Mensagem
    lv_obj_t* msgLabel = lv_label_create(s_dialog);
    lv_label_set_text(msgLabel, message);
    lv_label_set_long_mode(msgLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msgLabel, DIALOG_W - 40);
    lv_obj_set_style_text_font(msgLabel, theme::fontText(), 0);
    lv_obj_set_style_text_color(msgLabel, theme::text(), 0);
    lv_obj_set_style_text_align(msgLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msgLabel, LV_ALIGN_CENTER, 0, -5);
    
    // Botão OK
    lv_obj_t* btnOk = lv_button_create(s_dialog);
    lv_obj_set_size(btnOk, DIALOG_BTN_W, DIALOG_BTN_H);
    lv_obj_align(btnOk, LV_ALIGN_BOTTOM_MID, 0, -12);
    theme::applyButtonStyle(btnOk, theme::primary());
    
    lv_obj_t* btnOkLabel = lv_label_create(btnOk);
    lv_label_set_text(btnOkLabel, "OK");
    lv_obj_set_style_text_font(btnOkLabel, theme::fontText(), 0);
    lv_obj_center(btnOkLabel);
    
    lv_obj_add_event_cb(btnOk, okButtonHandler, LV_EVENT_CLICKED, nullptr);
    
    ESP_LOGI(TAG, "Dialog shown: %s", title);
}

void Dialog::confirm(const char* title, const char* message,
                    std::function<void()> onConfirm,
                    std::function<void()> onCancel) {
    // Fechar dialog anterior se houver
    close();
    
    s_onOk = onConfirm;
    s_onCancel = onCancel;
    
    // Criar overlay
    createOverlay();
    if (s_overlay == nullptr) return;
    
    // Criar container do dialog
    s_dialog = lv_obj_create(s_overlay);
    lv_obj_set_size(s_dialog, DIALOG_W, DIALOG_H);
    lv_obj_align(s_dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_dialog, theme::surface(), 0);
    lv_obj_set_style_bg_opa(s_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_dialog, theme::RADIUS_MD, 0);
    lv_obj_set_style_border_width(s_dialog, 1, 0);
    lv_obj_set_style_border_color(s_dialog, theme::border(), 0);
    lv_obj_set_style_shadow_width(s_dialog, 20, 0);
    lv_obj_set_style_shadow_opa(s_dialog, LV_OPA_30, 0);
    lv_obj_clear_flag(s_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Título
    lv_obj_t* titleLabel = lv_label_create(s_dialog);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_font(titleLabel, theme::fontTitle(), 0);
    lv_obj_set_style_text_color(titleLabel, theme::primary(), 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 12);
    
    // Mensagem
    lv_obj_t* msgLabel = lv_label_create(s_dialog);
    lv_label_set_text(msgLabel, message);
    lv_label_set_long_mode(msgLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msgLabel, DIALOG_W - 40);
    lv_obj_set_style_text_font(msgLabel, theme::fontText(), 0);
    lv_obj_set_style_text_color(msgLabel, theme::text(), 0);
    lv_obj_set_style_text_align(msgLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msgLabel, LV_ALIGN_CENTER, 0, -5);
    
    // Botão Cancelar
    lv_obj_t* btnCancel = lv_button_create(s_dialog);
    lv_obj_set_size(btnCancel, DIALOG_BTN_W, DIALOG_BTN_H);
    lv_obj_align(btnCancel, LV_ALIGN_BOTTOM_LEFT, 20, -12);
    theme::applyButtonStyle(btnCancel, theme::buttonGray());
    
    lv_obj_t* btnCancelLabel = lv_label_create(btnCancel);
    lv_label_set_text(btnCancelLabel, "Cancelar");
    lv_obj_set_style_text_font(btnCancelLabel, theme::fontText(), 0);
    lv_obj_center(btnCancelLabel);
    
    lv_obj_add_event_cb(btnCancel, cancelButtonHandler, LV_EVENT_CLICKED, nullptr);
    
    // Botão Confirmar
    lv_obj_t* btnOk = lv_button_create(s_dialog);
    lv_obj_set_size(btnOk, DIALOG_BTN_W, DIALOG_BTN_H);
    lv_obj_align(btnOk, LV_ALIGN_BOTTOM_RIGHT, -20, -12);
    theme::applyButtonStyle(btnOk, theme::primary());
    
    lv_obj_t* btnOkLabel = lv_label_create(btnOk);
    lv_label_set_text(btnOkLabel, "Confirmar");
    lv_obj_set_style_text_font(btnOkLabel, theme::fontText(), 0);
    lv_obj_center(btnOkLabel);
    
    lv_obj_add_event_cb(btnOk, okButtonHandler, LV_EVENT_CLICKED, nullptr);
    
    ESP_LOGI(TAG, "Confirm dialog shown: %s", title);
}

void Dialog::showLoading(const char* message) {
    // Fechar dialog anterior se houver
    close();
    
    // Criar overlay
    createOverlay();
    if (s_overlay == nullptr) return;
    
    // Criar container do dialog
    s_loadingDialog = lv_obj_create(s_overlay);
    lv_obj_set_size(s_loadingDialog, 180, 80);
    lv_obj_align(s_loadingDialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_loadingDialog, theme::surface(), 0);
    lv_obj_set_style_bg_opa(s_loadingDialog, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_loadingDialog, theme::RADIUS_MD, 0);
    lv_obj_set_style_border_width(s_loadingDialog, 1, 0);
    lv_obj_set_style_border_color(s_loadingDialog, theme::border(), 0);
    lv_obj_clear_flag(s_loadingDialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Indicador de loading usando barra de progresso indeterminada
    lv_obj_t* bar = lv_bar_create(s_loadingDialog);
    lv_obj_set_size(bar, 120, 8);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 18);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, theme::border(), 0);
    lv_obj_set_style_bg_color(bar, theme::primary(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    
    // Mensagem
    lv_obj_t* msgLabel = lv_label_create(s_loadingDialog);
    lv_label_set_text(msgLabel, message);
    lv_obj_set_style_text_font(msgLabel, theme::fontText(), 0);
    lv_obj_set_style_text_color(msgLabel, theme::text(), 0);
    lv_obj_align(msgLabel, LV_ALIGN_BOTTOM_MID, 0, -15);
    
    ESP_LOGI(TAG, "Loading dialog shown: %s", message);
}

void Dialog::hideLoading() {
    if (s_loadingDialog != nullptr) {
        lv_obj_delete_async(s_loadingDialog);
        s_loadingDialog = nullptr;
    }
    
    if (s_overlay != nullptr) {
        lv_obj_delete_async(s_overlay);
        s_overlay = nullptr;
    }
    
    ESP_LOGI(TAG, "Loading dialog hidden");
}

void Dialog::close() {
    // Limpar callbacks
    s_onOk = nullptr;
    s_onCancel = nullptr;
    
    // Deletar dialogs
    if (s_dialog != nullptr) {
        lv_obj_delete_async(s_dialog);
        s_dialog = nullptr;
    }
    
    if (s_loadingDialog != nullptr) {
        lv_obj_delete_async(s_loadingDialog);
        s_loadingDialog = nullptr;
    }
    
    // Deletar overlay
    if (s_overlay != nullptr) {
        lv_obj_delete_async(s_overlay);
        s_overlay = nullptr;
    }
}

bool Dialog::isOpen() {
    return s_overlay != nullptr;
}

void Dialog::okButtonHandler(lv_event_t* e) {
    (void)e; // Não usado
    
    auto callback = s_onOk;  // Copiar antes de fechar
    close();
    
    if (callback) {
        callback();
    }
}

void Dialog::cancelButtonHandler(lv_event_t* e) {
    (void)e; // Não usado
    
    auto callback = s_onCancel;  // Copiar antes de fechar
    close();
    
    if (callback) {
        callback();
    }
}

} // namespace ui
