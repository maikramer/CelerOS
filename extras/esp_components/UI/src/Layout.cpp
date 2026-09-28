#include "Layout.h"
#include "esp_log.h"

static const char* TAG = "Layout";

namespace ui {
namespace layout {

// ============================================================================
// Variáveis de Tamanho de Tela
// ============================================================================

static int32_t s_screenWidth = 320;   // Valor padrão
static int32_t s_screenHeight = 240;  // Valor padrão

void setScreenSize(int32_t width, int32_t height) {
    s_screenWidth = width;
    s_screenHeight = height;
    ESP_LOGI(TAG, "Screen size set to %ldx%ld", width, height);
}

int32_t screenWidth() {
    return s_screenWidth;
}

int32_t screenHeight() {
    return s_screenHeight;
}

// ============================================================================
// Breakpoints e Responsividade
// ============================================================================

// Limites de breakpoints (configuráveis)
static constexpr int32_t BREAKPOINT_SMALL = 280;
static constexpr int32_t BREAKPOINT_LARGE = 400;

ScreenSize currentScreenSize() {
    if (s_screenWidth < BREAKPOINT_SMALL) {
        return ScreenSize::Small;
    } else if (s_screenWidth >= BREAKPOINT_LARGE) {
        return ScreenSize::Large;
    } else {
        return ScreenSize::Medium;
    }
}

int32_t responsive(int32_t small, int32_t medium, int32_t large) {
    switch (currentScreenSize()) {
        case ScreenSize::Small:
            return small;
        case ScreenSize::Large:
            return large;
        case ScreenSize::Medium:
        default:
            return medium;
    }
}

float responsiveF(float small, float medium, float large) {
    switch (currentScreenSize()) {
        case ScreenSize::Small:
            return small;
        case ScreenSize::Large:
            return large;
        case ScreenSize::Medium:
        default:
            return medium;
    }
}

} // namespace layout
} // namespace ui
