#include "GestureDetector.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <unordered_map>
#include <cmath>

static const char* TAG = "GestureDetector";

namespace ui {

// ============================================================================
// Estado Interno
// ============================================================================

struct GestureStateData {
    int32_t startX = 0;
    int32_t startY = 0;
    int64_t startTime = 0;
    int64_t lastTapTime = 0;
    bool isPressed = false;
    bool longPressTriggered = false;
    GestureCallback callback;
    DetailedGestureCallback detailedCallback;
    
    // Para edge swipe
    int32_t edgeWidth = 0;
    GestureCallback edgeCallback;
};

// ============================================================================
// Configuração Global e Estado
// ============================================================================

static int32_t g_swipeThreshold = 50;     // pixels
static uint32_t g_longPressTime = 500;    // ms
static uint32_t g_doubleTapTime = 300;    // ms

// Mapa de estados por objeto (usa uintptr_t como chave para evitar problemas com ponteiro opaco)
static std::unordered_map<uintptr_t, GestureStateData*> g_states;

// ============================================================================
// Helpers
// ============================================================================

static int64_t getCurrentTimeMs() {
    return esp_timer_get_time() / 1000;
}

static uintptr_t objToKey(lv_obj_t* obj) {
    return reinterpret_cast<uintptr_t>(obj);
}

static GestureStateData* getState(lv_obj_t* obj) {
    auto it = g_states.find(objToKey(obj));
    if (it != g_states.end()) {
        return it->second;
    }
    return nullptr;
}

static GestureType detectGesture(const GestureStateData* state, int32_t currentX, int32_t currentY) {
    if (!state) return GestureType::Tap;
    
    int32_t deltaX = currentX - state->startX;
    int32_t deltaY = currentY - state->startY;
    
    int32_t absX = std::abs(deltaX);
    int32_t absY = std::abs(deltaY);
    
    // Verifica se é um swipe (distância mínima)
    if (absX >= g_swipeThreshold || absY >= g_swipeThreshold) {
        // Determina direção predominante
        if (absX > absY) {
            return (deltaX > 0) ? GestureType::SwipeRight : GestureType::SwipeLeft;
        } else {
            return (deltaY > 0) ? GestureType::SwipeDown : GestureType::SwipeUp;
        }
    }
    
    // Verifica double tap
    int64_t now = getCurrentTimeMs();
    if (state->lastTapTime > 0 && 
        (now - state->lastTapTime) < static_cast<int64_t>(g_doubleTapTime)) {
        return GestureType::DoubleTap;
    }
    
    return GestureType::Tap;
}

// ============================================================================
// Configuração
// ============================================================================

void GestureDetector::setSwipeThreshold(int32_t pixels) {
    g_swipeThreshold = pixels;
}

int32_t GestureDetector::swipeThreshold() {
    return g_swipeThreshold;
}

void GestureDetector::setLongPressTime(uint32_t ms) {
    g_longPressTime = ms;
}

uint32_t GestureDetector::longPressTime() {
    return g_longPressTime;
}

void GestureDetector::setDoubleTapTime(uint32_t ms) {
    g_doubleTapTime = ms;
}

uint32_t GestureDetector::doubleTapTime() {
    return g_doubleTapTime;
}

// ============================================================================
// Event Handlers
// ============================================================================

static void handlePressed(lv_event_t* e) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    GestureStateData* state = getState(obj);
    if (!state) return;
    
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    
    state->startX = point.x;
    state->startY = point.y;
    state->startTime = getCurrentTimeMs();
    state->isPressed = true;
    state->longPressTriggered = false;
    
    ESP_LOGD(TAG, "Pressed at (%ld, %ld)", point.x, point.y);
}

static void handleReleased(lv_event_t* e) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    GestureStateData* state = getState(obj);
    if (!state || !state->isPressed) return;
    
    state->isPressed = false;
    
    // Não detecta gesto se long press já foi trigado
    if (state->longPressTriggered) {
        return;
    }
    
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    int64_t now = getCurrentTimeMs();
    
    GestureType type = detectGesture(state, point.x, point.y);
    
    // Verifica edge swipe
    if (state->edgeWidth > 0 && state->startX <= state->edgeWidth) {
        if (type == GestureType::SwipeRight && state->edgeCallback) {
            state->edgeCallback(type);
            ESP_LOGI(TAG, "Edge swipe detected");
            return;
        }
    }
    
    // Callback normal
    if (state->detailedCallback) {
        GestureInfo info{
            .type = type,
            .startX = state->startX,
            .startY = state->startY,
            .endX = point.x,
            .endY = point.y,
            .deltaX = point.x - state->startX,
            .deltaY = point.y - state->startY,
            .durationMs = static_cast<uint32_t>(now - state->startTime)
        };
        state->detailedCallback(info);
    } else if (state->callback) {
        state->callback(type);
    }
    
    // Atualiza tempo do último tap para detecção de double tap
    if (type == GestureType::Tap || type == GestureType::DoubleTap) {
        state->lastTapTime = now;
    }
    
    ESP_LOGD(TAG, "Released, gesture: %s", gestureTypeToString(type));
}

static void handleLongPress(lv_event_t* e) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    GestureStateData* state = getState(obj);
    if (!state || !state->isPressed || state->longPressTriggered) return;
    
    state->longPressTriggered = true;
    
    if (state->detailedCallback) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        int64_t now = getCurrentTimeMs();
        
        GestureInfo info{
            .type = GestureType::LongPress,
            .startX = state->startX,
            .startY = state->startY,
            .endX = point.x,
            .endY = point.y,
            .deltaX = point.x - state->startX,
            .deltaY = point.y - state->startY,
            .durationMs = static_cast<uint32_t>(now - state->startTime)
        };
        state->detailedCallback(info);
    } else if (state->callback) {
        state->callback(GestureType::LongPress);
    }
    
    ESP_LOGI(TAG, "Long press detected");
}

static void handleDelete(lv_event_t* e) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uintptr_t key = objToKey(obj);
    auto it = g_states.find(key);
    if (it != g_states.end()) {
        delete it->second;
        g_states.erase(it);
        ESP_LOGD(TAG, "State cleaned up on delete");
    }
}

// ============================================================================
// API Pública
// ============================================================================

void GestureDetector::attach(lv_obj_t* obj, GestureCallback callback) {
    if (!obj || !callback) return;
    
    // Remove detector existente se houver
    detach(obj);
    
    // Cria novo estado
    auto* state = new GestureStateData();
    state->callback = callback;
    g_states[objToKey(obj)] = state;
    
    // Habilita eventos de click
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    
    // Registra handlers
    lv_obj_add_event_cb(obj, handlePressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, handleReleased, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(obj, handleLongPress, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, handleDelete, LV_EVENT_DELETE, nullptr);
    
    ESP_LOGD(TAG, "Gesture detector attached");
}

void GestureDetector::attachDetailed(lv_obj_t* obj, DetailedGestureCallback callback) {
    if (!obj || !callback) return;
    
    // Remove detector existente se houver
    detach(obj);
    
    // Cria novo estado
    auto* state = new GestureStateData();
    state->detailedCallback = callback;
    g_states[objToKey(obj)] = state;
    
    // Habilita eventos de click
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    
    // Registra handlers
    lv_obj_add_event_cb(obj, handlePressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, handleReleased, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(obj, handleLongPress, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, handleDelete, LV_EVENT_DELETE, nullptr);
    
    ESP_LOGD(TAG, "Detailed gesture detector attached");
}

void GestureDetector::detach(lv_obj_t* obj) {
    if (!obj) return;
    
    uintptr_t key = objToKey(obj);
    auto it = g_states.find(key);
    if (it != g_states.end()) {
        delete it->second;
        g_states.erase(it);
        ESP_LOGD(TAG, "Gesture detector detached");
    }
}

void GestureDetector::enableEdgeSwipe(lv_obj_t* screen, int32_t edgeWidth, GestureCallback callback) {
    if (!screen || !callback) return;
    
    // Verifica se já tem estado
    GestureStateData* state = getState(screen);
    
    if (!state) {
        // Cria estado e anexa detector básico
        state = new GestureStateData();
        g_states[objToKey(screen)] = state;
        
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(screen, handlePressed, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(screen, handleReleased, LV_EVENT_RELEASED, nullptr);
        lv_obj_add_event_cb(screen, handleDelete, LV_EVENT_DELETE, nullptr);
    }
    
    state->edgeWidth = edgeWidth;
    state->edgeCallback = callback;
    
    ESP_LOGI(TAG, "Edge swipe enabled with width %ld", edgeWidth);
}

void GestureDetector::disableEdgeSwipe(lv_obj_t* screen) {
    if (!screen) return;
    
    GestureStateData* state = getState(screen);
    if (state) {
        state->edgeWidth = 0;
        state->edgeCallback = nullptr;
        ESP_LOGI(TAG, "Edge swipe disabled");
    }
}

} // namespace ui
