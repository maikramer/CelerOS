#include "Navigator.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "Navigator";

namespace ui {

Navigator& Navigator::instance() {
    static Navigator s_instance;
    return s_instance;
}

// ============================================================================
// Navegação
// ============================================================================

void Navigator::push(std::unique_ptr<BaseScreen> screen, anim::ScreenTransition transition) {
    if (!screen) {
        ESP_LOGE(TAG, "Cannot push null screen");
        return;
    }
    
    BaseScreen* fromScreen = current();
    BaseScreen* toScreen = screen.get();
    
    // Configurar callback de voltar automaticamente
    setupBackCallback(toScreen);
    
    // Adicionar à pilha
    m_stack.push_back(std::move(screen));
    
    // Determinar transição
    anim::ScreenTransition actualTransition = 
        (transition == anim::ScreenTransition::None) 
        ? m_defaultPushTransition 
        : transition;
    
    // Mostrar nova tela
    toScreen->showWithAnimation(actualTransition);
    
    // Notificar
    notifyNavigation(fromScreen, toScreen);
    
    ESP_LOGI(TAG, "Pushed screen '%s' (stack size: %d)", 
             toScreen->getTitle(), static_cast<int>(m_stack.size()));
}

bool Navigator::pop(anim::ScreenTransition transition) {
    if (m_stack.size() <= 1) {
        ESP_LOGW(TAG, "Cannot pop: at root or empty");
        return false;
    }
    
    BaseScreen* fromScreen = current();
    
    // Remover tela atual
    m_stack.pop_back();
    
    BaseScreen* toScreen = current();
    
    // Determinar transição
    anim::ScreenTransition actualTransition = 
        (transition == anim::ScreenTransition::None) 
        ? m_defaultPopTransition 
        : transition;
    
    // Mostrar tela anterior
    if (toScreen) {
        toScreen->showWithAnimation(actualTransition);
    }
    
    // Notificar
    notifyNavigation(fromScreen, toScreen);
    
    ESP_LOGI(TAG, "Popped to '%s' (stack size: %d)", 
             toScreen ? toScreen->getTitle() : "none",
             static_cast<int>(m_stack.size()));
    
    return true;
}

bool Navigator::popTo(const char* screenName) {
    if (!screenName) return false;
    
    // Encontrar índice da tela
    int targetIndex = -1;
    for (size_t i = 0; i < m_stack.size(); i++) {
        if (m_stack[i] && std::strcmp(m_stack[i]->getTitle(), screenName) == 0) {
            targetIndex = static_cast<int>(i);
            break;
        }
    }
    
    if (targetIndex < 0) {
        ESP_LOGW(TAG, "Screen '%s' not found in stack", screenName);
        return false;
    }
    
    // Remover telas acima
    BaseScreen* fromScreen = current();
    
    while (m_stack.size() > static_cast<size_t>(targetIndex + 1)) {
        m_stack.pop_back();
    }
    
    BaseScreen* toScreen = current();
    
    if (toScreen) {
        toScreen->showWithAnimation(m_defaultPopTransition);
    }
    
    notifyNavigation(fromScreen, toScreen);
    
    ESP_LOGI(TAG, "Popped to '%s' (stack size: %d)", 
             screenName, static_cast<int>(m_stack.size()));
    
    return true;
}

void Navigator::popToRoot() {
    if (m_stack.size() <= 1) return;
    
    BaseScreen* fromScreen = current();
    
    // Manter apenas a primeira tela
    while (m_stack.size() > 1) {
        m_stack.pop_back();
    }
    
    BaseScreen* toScreen = current();
    
    if (toScreen) {
        toScreen->showWithAnimation(m_defaultPopTransition);
    }
    
    notifyNavigation(fromScreen, toScreen);
    
    ESP_LOGI(TAG, "Popped to root (stack size: %d)", 
             static_cast<int>(m_stack.size()));
}

void Navigator::replace(std::unique_ptr<BaseScreen> screen, anim::ScreenTransition transition) {
    if (!screen) {
        ESP_LOGE(TAG, "Cannot replace with null screen");
        return;
    }
    
    BaseScreen* fromScreen = current();
    BaseScreen* toScreen = screen.get();
    
    // Configurar callback
    setupBackCallback(toScreen);
    
    // Substituir tela atual
    if (!m_stack.empty()) {
        m_stack.pop_back();
    }
    m_stack.push_back(std::move(screen));
    
    // Determinar transição
    anim::ScreenTransition actualTransition = 
        (transition == anim::ScreenTransition::None) 
        ? anim::ScreenTransition::FadeIn 
        : transition;
    
    // Mostrar
    toScreen->showWithAnimation(actualTransition);
    
    notifyNavigation(fromScreen, toScreen);
    
    ESP_LOGI(TAG, "Replaced with '%s' (stack size: %d)", 
             toScreen->getTitle(), static_cast<int>(m_stack.size()));
}

void Navigator::setRoot(std::unique_ptr<BaseScreen> screen) {
    if (!screen) {
        ESP_LOGE(TAG, "Cannot set null root screen");
        return;
    }
    
    // Limpar pilha
    m_stack.clear();
    
    BaseScreen* toScreen = screen.get();
    
    // Configurar callback (não precisa para root, mas por consistência)
    setupBackCallback(toScreen);
    
    // Adicionar como raiz
    m_stack.push_back(std::move(screen));
    
    // Mostrar sem animação (é a tela inicial)
    toScreen->show();
    
    ESP_LOGI(TAG, "Set root screen '%s'", toScreen->getTitle());
}

// ============================================================================
// Estado
// ============================================================================

size_t Navigator::stackSize() const {
    return m_stack.size();
}

BaseScreen* Navigator::current() const {
    if (m_stack.empty()) return nullptr;
    return m_stack.back().get();
}

bool Navigator::canGoBack() const {
    return m_stack.size() > 1;
}

void Navigator::clear() {
    m_stack.clear();
    ESP_LOGI(TAG, "Navigation stack cleared");
}

// ============================================================================
// Configuração
// ============================================================================

void Navigator::setOnNavigate(NavigationCallback callback) {
    m_onNavigate = callback;
}

void Navigator::setSwipeBackEnabled(bool enabled) {
    m_swipeBackEnabled = enabled;
}

bool Navigator::isSwipeBackEnabled() const {
    return m_swipeBackEnabled;
}

void Navigator::setDefaultPushTransition(anim::ScreenTransition transition) {
    m_defaultPushTransition = transition;
}

void Navigator::setDefaultPopTransition(anim::ScreenTransition transition) {
    m_defaultPopTransition = transition;
}

// ============================================================================
// Helpers Privados
// ============================================================================

void Navigator::notifyNavigation(BaseScreen* from, BaseScreen* to) {
    if (m_onNavigate) {
        m_onNavigate(from, to);
    }
}

void Navigator::setupBackCallback(BaseScreen* screen) {
    if (!screen) return;
    
    // Configura callback para chamar pop() automaticamente
    screen->setBackCallback([this]() {
        this->pop();
    });
}

} // namespace ui
