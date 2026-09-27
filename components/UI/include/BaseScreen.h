#pragma once

/**
 * @file BaseScreen.h
 * @brief Classe base abstrata para telas LVGL
 * 
 * Fornece funcionalidade comum para todas as telas:
 * - Criação e destruição automática
 * - Gerenciamento de callback de retorno
 * - Helpers para criar elementos comuns (título, botão voltar)
 * - Sistema de layout relativo para posicionamento automático
 * - Animações de transição entre telas
 */

#include "lvgl.h"
#include "Layout.h"
#include "Animation.h"
#include <functional>

namespace ui {

/**
 * @brief Estilos de label pré-definidos
 */
enum class LabelStyle {
    Title,      ///< Fonte grande, cor primária (para títulos)
    Normal,     ///< Fonte normal, cor texto padrão
    Caption,    ///< Fonte pequena, cor secundária (para legendas)
    Value       ///< Fonte normal, alinhamento centro (para valores)
};

/**
 * @brief Estrutura para retornar widgets compostos (label + widget)
 */
struct LabeledWidget {
    lv_obj_t* row;      ///< Container da row
    lv_obj_t* label;    ///< Label à esquerda
    lv_obj_t* widget;   ///< Widget à direita (switch, slider, etc)
};

/**
 * @class BaseScreen
 * @brief Classe base para implementação de telas
 * 
 * Subclasses devem implementar onCreate() e getTitle().
 * 
 * Exemplo:
 * @code
 * class MyScreen : public ui::BaseScreen {
 * protected:
 *     void onCreate() override {
 *         createTitle();  // Título automático no topo
 *         
 *         auto* content = createContentContainer();
 *         auto* label = lv_label_create(content);
 *         lv_label_set_text(label, "Hello!");
 *         
 *         createBackButton();  // Botão voltar automático
 *     }
 *     const char* getTitle() const override { return "My Screen"; }
 * };
 * 
 * MyScreen myScreen;
 * myScreen.setBackCallback([]() { goBack(); });
 * myScreen.show();
 * @endcode
 */
class BaseScreen {
public:
    virtual ~BaseScreen();
    
    /**
     * @brief Tipo do callback de retorno
     */
    using BackCallback = std::function<void()>;
    
    /**
     * @brief Mostra a tela (cria se necessário e carrega)
     * 
     * Usa a animação de entrada configurada globalmente ou
     * a retornada por getEnterAnimation().
     */
    void show();
    
    /**
     * @brief Mostra a tela com animação específica
     * @param transition Tipo de transição a usar
     */
    void showWithAnimation(anim::ScreenTransition transition);
    
    /**
     * @brief Esconde e destrói a tela
     */
    void hide();
    
    /**
     * @brief Esconde a tela com animação específica
     * @param transition Tipo de transição a usar
     * @note A tela será destruída após a animação completar
     */
    void hideWithAnimation(anim::ScreenTransition transition);
    
    /**
     * @brief Verifica se a tela está visível
     * @return true se a tela existe e está carregada
     */
    bool isVisible() const;
    
    /**
     * @brief Define o callback chamado ao pressionar "Voltar"
     * @param cb Função a ser chamada
     */
    void setBackCallback(BackCallback cb);
    
    /**
     * @brief Retorna o título da tela
     * @return String constante com o título
     */
    virtual const char* getTitle() const = 0;
    
protected:
    /**
     * @brief Chamado para criar o conteúdo da tela
     * 
     * Subclasses devem implementar este método para criar seus
     * elementos de UI. Use screen() para obter o objeto pai.
     */
    virtual void onCreate() = 0;
    
    /**
     * @brief Chamado antes de destruir a tela
     * 
     * Subclasses podem sobrescrever para limpar recursos.
     */
    virtual void onDestroy() {}
    
    /**
     * @brief Retorna a animação de entrada da tela
     * 
     * Subclasses podem sobrescrever para usar animação específica.
     * Por padrão, usa a configuração global.
     * 
     * @return Tipo de transição para entrada
     */
    virtual anim::ScreenTransition getEnterAnimation() const;
    
    /**
     * @brief Retorna a animação de saída da tela
     * 
     * Subclasses podem sobrescrever para usar animação específica.
     * Por padrão, usa a configuração global.
     * 
     * @return Tipo de transição para saída
     */
    virtual anim::ScreenTransition getExitAnimation() const;
    
    /**
     * @brief Obtém o objeto da tela
     * @return Ponteiro para o objeto LVGL da tela
     */
    lv_obj_t* screen() const { return m_screen; }
    
    // ========================================================================
    // Criação de Elementos Comuns
    // ========================================================================
    
    /**
     * @brief Cria um título centralizado no topo
     * @param text Texto do título (nullptr usa getTitle())
     * @return Ponteiro para o label criado
     */
    lv_obj_t* createTitle(const char* text = nullptr);
    
    /**
     * @brief Cria um botão "Voltar" padrão
     * @return Ponteiro para o botão criado
     */
    lv_obj_t* createBackButton();
    
    /**
     * @brief Cria um botão de ação
     * @param text Texto do botão
     * @param color Cor de fundo
     * @param onClick Callback ao clicar
     * @return Ponteiro para o botão criado
     */
    lv_obj_t* createActionButton(const char* text, lv_color_t color,
                                  std::function<void()> onClick);
    
    /**
     * @brief Cria um container scrollável para conteúdo
     * 
     * O container é dimensionado automaticamente para ocupar a área
     * entre o título e o rodapé (área de botões).
     * 
     * @param scrollable Se o container deve ser scrollável (padrão: true)
     * @return Ponteiro para o container criado
     */
    lv_obj_t* createContentContainer(bool scrollable = true);
    
    /**
     * @brief Cria uma barra de botões no rodapé
     * 
     * Útil para telas com múltiplos botões de ação.
     * Os botões são alinhados horizontalmente com espaçamento automático.
     * 
     * @return Ponteiro para o container de botões
     */
    lv_obj_t* createButtonBar();
    
    /**
     * @brief Cria um label de status centralizado
     * 
     * Posicionado logo abaixo do título, útil para mensagens de status.
     * 
     * @param text Texto inicial
     * @return Ponteiro para o label criado
     */
    lv_obj_t* createStatusLabel(const char* text = "");
    
    // ========================================================================
    // Widgets Comuns (com estilos e eventos pré-configurados)
    // ========================================================================
    
    /**
     * @brief Cria um label com estilo pré-definido
     * 
     * @param text Texto do label
     * @param style Estilo a aplicar (Normal, Title, Caption, Value)
     * @return Ponteiro para o label criado
     */
    lv_obj_t* createLabel(const char* text, LabelStyle style = LabelStyle::Normal);
    
    /**
     * @brief Cria um switch com callback de mudança de estado
     * 
     * @param initialState Estado inicial (true = ligado)
     * @param onChange Callback chamado quando estado muda
     * @return Ponteiro para o switch criado
     */
    lv_obj_t* createSwitch(bool initialState, std::function<void(bool)> onChange);
    
    /**
     * @brief Cria um slider com range e callback
     * 
     * @param min Valor mínimo
     * @param max Valor máximo
     * @param initial Valor inicial
     * @param onChange Callback chamado quando valor muda
     * @return Ponteiro para o slider criado
     */
    lv_obj_t* createSlider(int32_t min, int32_t max, int32_t initial,
                           std::function<void(int32_t)> onChange);
    
    /**
     * @brief Cria um botão estilizado como campo de input
     * 
     * Útil para abrir tela de edição separada (teclado virtual, etc).
     * 
     * @param placeholder Texto placeholder inicial
     * @param onEdit Callback chamado quando usuário toca para editar
     * @param labelOut Ponteiro opcional para receber o label interno
     * @return Ponteiro para o botão criado
     */
    lv_obj_t* createInputButton(const char* placeholder,
                                std::function<void()> onEdit,
                                lv_obj_t** labelOut = nullptr);
    
    /**
     * @brief Cria uma barra de progresso
     * 
     * @param min Valor mínimo (padrão: 0)
     * @param max Valor máximo (padrão: 100)
     * @return Ponteiro para a barra de progresso
     */
    lv_obj_t* createProgressBar(int32_t min = 0, int32_t max = 100);
    
    // ========================================================================
    // Layouts Compostos (Row com Label + Widget)
    // ========================================================================
    
    /**
     * @brief Cria uma row com label + switch
     * 
     * Layout: [Label]                    [Switch]
     * 
     * @param labelText Texto do label à esquerda
     * @param initial Estado inicial do switch
     * @param onChange Callback quando switch muda
     * @return Estrutura com ponteiros para row, label e switch
     */
    LabeledWidget createLabeledSwitch(const char* labelText, bool initial,
                                      std::function<void(bool)> onChange);
    
    /**
     * @brief Cria uma row com label + slider
     * 
     * Layout: [Label]
     *         [========Slider========]
     * 
     * @param labelText Texto do label
     * @param min Valor mínimo do slider
     * @param max Valor máximo do slider
     * @param initial Valor inicial
     * @param onChange Callback quando valor muda
     * @return Estrutura com ponteiros para row, label e slider
     */
    LabeledWidget createLabeledSlider(const char* labelText,
                                      int32_t min, int32_t max, int32_t initial,
                                      std::function<void(int32_t)> onChange);
    
    /**
     * @brief Cria uma row com label + input button
     * 
     * Layout: [Label]  [=====Input Button=====]
     * 
     * @param labelText Texto do label à esquerda
     * @param placeholder Texto placeholder do input
     * @param onEdit Callback quando usuário toca para editar
     * @return Estrutura com ponteiros para row, label e input button
     */
    LabeledWidget createLabeledInput(const char* labelText,
                                     const char* placeholder,
                                     std::function<void()> onEdit);
    
    // ========================================================================
    // Helpers de Layout Relativo
    // ========================================================================
    
    /**
     * @brief Posiciona objeto usando coordenadas relativas (0.0-1.0)
     * 
     * @param obj Objeto a posicionar
     * @param relX Posição X relativa (0.0 = esquerda, 1.0 = direita)
     * @param relY Posição Y relativa (0.0 = topo, 1.0 = base)
     */
    void placeAt(lv_obj_t* obj, float relX, float relY);
    
    /**
     * @brief Define tamanho usando valores relativos (0.0-1.0)
     * 
     * @param obj Objeto a dimensionar
     * @param relW Largura relativa (0.0 a 1.0)
     * @param relH Altura relativa (0.0 a 1.0)
     */
    void setSize(lv_obj_t* obj, float relW, float relH);
    
    /**
     * @brief Define largura usando valor relativo (0.0-1.0)
     * 
     * @param obj Objeto a dimensionar
     * @param relW Largura relativa (0.0 a 1.0)
     */
    void setWidth(lv_obj_t* obj, float relW);
    
    /**
     * @brief Define altura usando valor relativo (0.0-1.0)
     * 
     * @param obj Objeto a dimensionar
     * @param relH Altura relativa (0.0 a 1.0)
     */
    void setHeight(lv_obj_t* obj, float relH);
    
    /**
     * @brief Posiciona objeto abaixo de outro com gap relativo
     * 
     * @param obj Objeto a posicionar
     * @param reference Objeto de referência (acima)
     * @param relGap Gap relativo à altura da tela (padrão 2%)
     */
    void placeBelow(lv_obj_t* obj, lv_obj_t* reference, float relGap = 0.02f);
    
    /**
     * @brief Posiciona objeto à direita de outro com gap relativo
     * 
     * @param obj Objeto a posicionar
     * @param reference Objeto de referência (à esquerda)
     * @param relGap Gap relativo à largura da tela (padrão 2%)
     */
    void placeRightOf(lv_obj_t* obj, lv_obj_t* reference, float relGap = 0.02f);
    
    /**
     * @brief Centraliza objeto horizontalmente
     * @param obj Objeto a centralizar
     */
    void centerHorizontally(lv_obj_t* obj);
    
    /**
     * @brief Obtém a área de conteúdo disponível
     * 
     * @param hasTitle Se a tela tem área de título
     * @param hasFooter Se a tela tem área de rodapé
     * @return layout::Rect com posição e tamanho
     */
    layout::Rect getContentArea(bool hasTitle = true, bool hasFooter = true) const;
    
    /**
     * @brief Invoca o callback de retorno
     */
    void invokeBackCallback();
    
public:
    /**
     * @brief Desassocia a tela LVGL do objeto C++
     * 
     * Após chamar este método, o destrutor NÃO deletará a tela LVGL.
     * Use quando quiser que o LVGL gerencie a deleção da tela
     * (por exemplo, ao carregar uma nova tela com auto_del=true).
     */
    void detachScreen();
    
private:
    lv_obj_t* m_screen = nullptr;
    lv_obj_t* m_contentContainer = nullptr;
    lv_obj_t* m_buttonBar = nullptr;
    BackCallback m_backCallback;
    
    static void backButtonEventHandler(lv_event_t* e);
    static void actionButtonEventHandler(lv_event_t* e);
    static void switchEventHandler(lv_event_t* e);
    static void sliderEventHandler(lv_event_t* e);
    static void inputButtonEventHandler(lv_event_t* e);
    static void cleanupCallback(lv_event_t* e);
};

} // namespace ui
