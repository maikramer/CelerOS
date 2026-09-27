#pragma once

/**
 * @file Navigator.h
 * @brief Sistema de navegação com stack de telas
 * 
 * Gerencia a pilha de navegação, transições entre telas e histórico.
 * Integra-se com BaseScreen para navegação automática.
 */

#include "BaseScreen.h"
#include "Animation.h"
#include <memory>
#include <vector>
#include <functional>

namespace ui {

/**
 * @class Navigator
 * @brief Gerenciador de navegação singleton
 * 
 * Exemplo de uso:
 * @code
 * // Navegar para nova tela
 * Navigator::instance().push(std::make_unique<MyScreen>());
 * 
 * // Voltar
 * Navigator::instance().pop();
 * 
 * // Voltar para raiz
 * Navigator::instance().popToRoot();
 * @endcode
 */
class Navigator {
public:
    /**
     * @brief Callback de navegação
     * @param from Tela de origem (pode ser nullptr)
     * @param to Tela de destino (pode ser nullptr no pop final)
     */
    using NavigationCallback = std::function<void(BaseScreen* from, BaseScreen* to)>;
    
    /**
     * @brief Obtém a instância singleton
     */
    static Navigator& instance();
    
    // Desabilitar cópia
    Navigator(const Navigator&) = delete;
    Navigator& operator=(const Navigator&) = delete;
    
    // ========================================================================
    // Navegação
    // ========================================================================
    
    /**
     * @brief Empilha uma nova tela
     * @param screen Tela a empilhar (ownership transferido)
     * @param transition Animação de transição (padrão: da configuração global)
     */
    void push(std::unique_ptr<BaseScreen> screen, 
              anim::ScreenTransition transition = anim::ScreenTransition::None);
    
    /**
     * @brief Volta para a tela anterior
     * @param transition Animação de transição
     * @return true se havia tela para voltar
     */
    bool pop(anim::ScreenTransition transition = anim::ScreenTransition::None);
    
    /**
     * @brief Volta até uma tela específica pelo nome
     * @param screenName Nome da tela (retornado por screenName())
     * @return true se a tela foi encontrada
     */
    bool popTo(const char* screenName);
    
    /**
     * @brief Volta para a primeira tela da pilha
     */
    void popToRoot();
    
    /**
     * @brief Substitui a tela atual
     * @param screen Nova tela
     * @param transition Animação de transição
     */
    void replace(std::unique_ptr<BaseScreen> screen,
                 anim::ScreenTransition transition = anim::ScreenTransition::None);
    
    /**
     * @brief Remove todas as telas e define uma nova raiz
     * @param screen Nova tela raiz
     */
    void setRoot(std::unique_ptr<BaseScreen> screen);
    
    // ========================================================================
    // Estado
    // ========================================================================
    
    /**
     * @brief Número de telas na pilha
     */
    size_t stackSize() const;
    
    /**
     * @brief Tela atual (topo da pilha)
     * @return Ponteiro para a tela atual ou nullptr se vazio
     */
    BaseScreen* current() const;
    
    /**
     * @brief Verifica se pode voltar (mais de uma tela)
     */
    bool canGoBack() const;
    
    /**
     * @brief Limpa toda a pilha de navegação
     */
    void clear();
    
    // ========================================================================
    // Configuração
    // ========================================================================
    
    /**
     * @brief Define callback chamado em cada navegação
     */
    void setOnNavigate(NavigationCallback callback);
    
    /**
     * @brief Habilita/desabilita swipe para voltar
     * @param enabled true para habilitar
     */
    void setSwipeBackEnabled(bool enabled);
    
    /**
     * @brief Verifica se swipe-back está habilitado
     */
    bool isSwipeBackEnabled() const;
    
    /**
     * @brief Define a animação padrão para push
     */
    void setDefaultPushTransition(anim::ScreenTransition transition);
    
    /**
     * @brief Define a animação padrão para pop
     */
    void setDefaultPopTransition(anim::ScreenTransition transition);
    
private:
    Navigator() = default;
    ~Navigator() = default;
    
    std::vector<std::unique_ptr<BaseScreen>> m_stack;
    NavigationCallback m_onNavigate;
    bool m_swipeBackEnabled = true;
    anim::ScreenTransition m_defaultPushTransition = anim::ScreenTransition::SlideLeft;
    anim::ScreenTransition m_defaultPopTransition = anim::ScreenTransition::SlideRight;
    
    void notifyNavigation(BaseScreen* from, BaseScreen* to);
    void setupBackCallback(BaseScreen* screen);
};

} // namespace ui
