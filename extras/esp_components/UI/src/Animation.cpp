#include "Animation.h"
#include "Theme.h"

namespace ui {
namespace anim {

// ============================================================================
// Configuração Global (Singleton)
// ============================================================================

static Config s_config;

Config& config() {
    return s_config;
}

void setConfig(const Config& cfg) {
    s_config = cfg;
}

// ============================================================================
// Conversão para LVGL
// ============================================================================

lv_screen_load_anim_t toLvglAnim(ScreenTransition transition) {
    switch (transition) {
        case ScreenTransition::None:
            return LV_SCR_LOAD_ANIM_NONE;
        case ScreenTransition::FadeIn:
        case ScreenTransition::FadeOut:
            return LV_SCR_LOAD_ANIM_FADE_IN;
        case ScreenTransition::SlideLeft:
            return LV_SCR_LOAD_ANIM_MOVE_LEFT;
        case ScreenTransition::SlideRight:
            return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        case ScreenTransition::SlideUp:
            return LV_SCR_LOAD_ANIM_MOVE_TOP;
        case ScreenTransition::SlideDown:
            return LV_SCR_LOAD_ANIM_MOVE_BOTTOM;
        case ScreenTransition::OverLeft:
            return LV_SCR_LOAD_ANIM_OVER_LEFT;
        case ScreenTransition::OverRight:
            return LV_SCR_LOAD_ANIM_OVER_RIGHT;
        case ScreenTransition::OverUp:
            return LV_SCR_LOAD_ANIM_OVER_TOP;
        case ScreenTransition::OverDown:
            return LV_SCR_LOAD_ANIM_OVER_BOTTOM;
        default:
            return LV_SCR_LOAD_ANIM_NONE;
    }
}

// ============================================================================
// Helper para obter duração
// ============================================================================

static uint32_t getDuration(uint32_t requested) {
    return (requested > 0) ? requested : s_config.elementAnimMs;
}

// ============================================================================
// Animações de Opacidade
// ============================================================================

void fadeIn(lv_obj_t* obj, uint32_t durationMs, uint32_t delayMs) {
    if (obj == nullptr || !s_config.enabled) return;
    
    uint32_t duration = getDuration(durationMs);
    
    // Começa invisível
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
    
    // Anima para opaco
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_delay(&anim, delayMs);
    lv_anim_set_exec_cb(&anim, [](void* var, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(v), 0);
    });
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

void fadeOut(lv_obj_t* obj, uint32_t durationMs, uint32_t delayMs) {
    if (obj == nullptr || !s_config.enabled) return;
    
    uint32_t duration = getDuration(durationMs);
    
    // Anima para transparente
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_delay(&anim, delayMs);
    lv_anim_set_exec_cb(&anim, [](void* var, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(v), 0);
    });
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_start(&anim);
}

// ============================================================================
// Animações de Posição (Slide)
// ============================================================================

void slideIn(lv_obj_t* obj, Direction dir, uint32_t durationMs, uint32_t delayMs) {
    if (obj == nullptr || !s_config.enabled) return;
    
    uint32_t duration = getDuration(durationMs);
    
    // Posição final (onde o objeto deve ficar)
    int32_t finalX = lv_obj_get_x(obj);
    int32_t finalY = lv_obj_get_y(obj);
    
    // Posição inicial (fora da tela)
    int32_t startX = finalX;
    int32_t startY = finalY;
    
    switch (dir) {
        case Direction::Left:
            startX = theme::SCREEN_W;
            break;
        case Direction::Right:
            startX = -lv_obj_get_width(obj);
            break;
        case Direction::Up:
            startY = theme::SCREEN_H;
            break;
        case Direction::Down:
            startY = -lv_obj_get_height(obj);
            break;
    }
    
    // Posiciona no início
    lv_obj_set_pos(obj, startX, startY);
    
    // Animação X
    if (startX != finalX) {
        lv_anim_t animX;
        lv_anim_init(&animX);
        lv_anim_set_var(&animX, obj);
        lv_anim_set_values(&animX, startX, finalX);
        lv_anim_set_duration(&animX, duration);
        lv_anim_set_delay(&animX, delayMs);
        lv_anim_set_exec_cb(&animX, [](void* var, int32_t v) {
            lv_obj_set_x(static_cast<lv_obj_t*>(var), v);
        });
        lv_anim_set_path_cb(&animX, lv_anim_path_ease_out);
        lv_anim_start(&animX);
    }
    
    // Animação Y
    if (startY != finalY) {
        lv_anim_t animY;
        lv_anim_init(&animY);
        lv_anim_set_var(&animY, obj);
        lv_anim_set_values(&animY, startY, finalY);
        lv_anim_set_duration(&animY, duration);
        lv_anim_set_delay(&animY, delayMs);
        lv_anim_set_exec_cb(&animY, [](void* var, int32_t v) {
            lv_obj_set_y(static_cast<lv_obj_t*>(var), v);
        });
        lv_anim_set_path_cb(&animY, lv_anim_path_ease_out);
        lv_anim_start(&animY);
    }
}

void slideOut(lv_obj_t* obj, Direction dir, uint32_t durationMs, uint32_t delayMs) {
    if (obj == nullptr || !s_config.enabled) return;
    
    uint32_t duration = getDuration(durationMs);
    
    // Posição inicial (onde o objeto está)
    int32_t startX = lv_obj_get_x(obj);
    int32_t startY = lv_obj_get_y(obj);
    
    // Posição final (fora da tela)
    int32_t finalX = startX;
    int32_t finalY = startY;
    
    switch (dir) {
        case Direction::Left:
            finalX = -lv_obj_get_width(obj);
            break;
        case Direction::Right:
            finalX = theme::SCREEN_W;
            break;
        case Direction::Up:
            finalY = -lv_obj_get_height(obj);
            break;
        case Direction::Down:
            finalY = theme::SCREEN_H;
            break;
    }
    
    // Animação X
    if (startX != finalX) {
        lv_anim_t animX;
        lv_anim_init(&animX);
        lv_anim_set_var(&animX, obj);
        lv_anim_set_values(&animX, startX, finalX);
        lv_anim_set_duration(&animX, duration);
        lv_anim_set_delay(&animX, delayMs);
        lv_anim_set_exec_cb(&animX, [](void* var, int32_t v) {
            lv_obj_set_x(static_cast<lv_obj_t*>(var), v);
        });
        lv_anim_set_path_cb(&animX, lv_anim_path_ease_in);
        lv_anim_start(&animX);
    }
    
    // Animação Y
    if (startY != finalY) {
        lv_anim_t animY;
        lv_anim_init(&animY);
        lv_anim_set_var(&animY, obj);
        lv_anim_set_values(&animY, startY, finalY);
        lv_anim_set_duration(&animY, duration);
        lv_anim_set_delay(&animY, delayMs);
        lv_anim_set_exec_cb(&animY, [](void* var, int32_t v) {
            lv_obj_set_y(static_cast<lv_obj_t*>(var), v);
        });
        lv_anim_set_path_cb(&animY, lv_anim_path_ease_in);
        lv_anim_start(&animY);
    }
}

// ============================================================================
// Animação de Escala (Pulse)
// ============================================================================

void pulse(lv_obj_t* obj, float scale, uint32_t durationMs) {
    if (obj == nullptr || !s_config.enabled) return;
    
    uint32_t duration = getDuration(durationMs);
    uint32_t halfDuration = duration / 2;
    
    // Escala em valores LVGL (256 = 1.0)
    int32_t normalScale = 256;
    int32_t maxScale = static_cast<int32_t>(256 * scale);
    
    // Primeira fase: crescer
    lv_anim_t animGrowX;
    lv_anim_init(&animGrowX);
    lv_anim_set_var(&animGrowX, obj);
    lv_anim_set_values(&animGrowX, normalScale, maxScale);
    lv_anim_set_duration(&animGrowX, halfDuration);
    lv_anim_set_exec_cb(&animGrowX, [](void* var, int32_t v) {
        lv_obj_set_style_transform_scale_x(static_cast<lv_obj_t*>(var), v, 0);
    });
    lv_anim_set_path_cb(&animGrowX, lv_anim_path_ease_out);
    
    lv_anim_t animGrowY;
    lv_anim_init(&animGrowY);
    lv_anim_set_var(&animGrowY, obj);
    lv_anim_set_values(&animGrowY, normalScale, maxScale);
    lv_anim_set_duration(&animGrowY, halfDuration);
    lv_anim_set_exec_cb(&animGrowY, [](void* var, int32_t v) {
        lv_obj_set_style_transform_scale_y(static_cast<lv_obj_t*>(var), v, 0);
    });
    lv_anim_set_path_cb(&animGrowY, lv_anim_path_ease_out);
    
    // Segunda fase: voltar ao normal
    lv_anim_t animShrinkX;
    lv_anim_init(&animShrinkX);
    lv_anim_set_var(&animShrinkX, obj);
    lv_anim_set_values(&animShrinkX, maxScale, normalScale);
    lv_anim_set_duration(&animShrinkX, halfDuration);
    lv_anim_set_delay(&animShrinkX, halfDuration);
    lv_anim_set_exec_cb(&animShrinkX, [](void* var, int32_t v) {
        lv_obj_set_style_transform_scale_x(static_cast<lv_obj_t*>(var), v, 0);
    });
    lv_anim_set_path_cb(&animShrinkX, lv_anim_path_ease_in);
    
    lv_anim_t animShrinkY;
    lv_anim_init(&animShrinkY);
    lv_anim_set_var(&animShrinkY, obj);
    lv_anim_set_values(&animShrinkY, maxScale, normalScale);
    lv_anim_set_duration(&animShrinkY, halfDuration);
    lv_anim_set_delay(&animShrinkY, halfDuration);
    lv_anim_set_exec_cb(&animShrinkY, [](void* var, int32_t v) {
        lv_obj_set_style_transform_scale_y(static_cast<lv_obj_t*>(var), v, 0);
    });
    lv_anim_set_path_cb(&animShrinkY, lv_anim_path_ease_in);
    
    lv_anim_start(&animGrowX);
    lv_anim_start(&animGrowY);
    lv_anim_start(&animShrinkX);
    lv_anim_start(&animShrinkY);
}

// ============================================================================
// Animação Sequencial (Stagger)
// ============================================================================

void staggerFadeIn(lv_obj_t* parent, uint32_t delayBetweenMs, uint32_t durationMs) {
    if (parent == nullptr || !s_config.enabled) return;
    
    uint32_t duration = getDuration(durationMs);
    uint32_t childCount = lv_obj_get_child_count(parent);
    
    for (uint32_t i = 0; i < childCount; i++) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (child != nullptr) {
            uint32_t delay = i * delayBetweenMs;
            fadeIn(child, duration, delay);
        }
    }
}

// ============================================================================
// Controle de Animação
// ============================================================================

void stop(lv_obj_t* obj) {
    if (obj == nullptr) return;
    lv_anim_delete(obj, nullptr);
}

} // namespace anim
} // namespace ui
