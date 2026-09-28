#pragma once

/**
 * @file ScrollableList.h
 * @brief Lista scrollável genérica com items clicáveis
 * 
 * Componente para criar listas de itens selecionáveis, ideal para:
 * - Listas de redes WiFi
 * - Menus de seleção
 * - Listas de arquivos
 * 
 * Exemplo:
 * @code
 * ui::ScrollableList list(parent, 300, 150);
 * 
 * list.addItem("Item 1", "Descrição", [](int idx) {
 *     ESP_LOGI(TAG, "Selecionado: %d", idx);
 * });
 * 
 * list.addItem("Item 2", "Outra descrição");
 * 
 * // Callback global
 * list.setOnSelect([](int idx) {
 *     processSelection(idx);
 * });
 * @endcode
 */

#include "lvgl.h"
#include <functional>
#include <vector>
#include <string>

namespace ui {

/**
 * @class ScrollableList
 * @brief Lista scrollável com items clicáveis
 */
class ScrollableList {
public:
    /**
     * @brief Callback de seleção de item
     * @param index Índice do item selecionado (0-based)
     */
    using SelectCallback = std::function<void(int index)>;
    
    /**
     * @brief Estrutura de um item da lista
     */
    struct Item {
        std::string title;          ///< Texto principal
        std::string subtitle;       ///< Texto secundário (opcional)
        SelectCallback onClick;     ///< Callback individual (opcional)
        void* userData = nullptr;   ///< Dados do usuário (opcional)
    };
    
    /**
     * @brief Construtor
     * 
     * @param parent Objeto pai LVGL
     * @param width Largura do container (0 = largura do pai)
     * @param height Altura do container (0 = altura automática)
     */
    ScrollableList(lv_obj_t* parent, int32_t width = 0, int32_t height = 0);
    
    /**
     * @brief Destrutor
     */
    ~ScrollableList();
    
    /**
     * @brief Adiciona um item à lista
     * 
     * @param title Texto principal
     * @param subtitle Texto secundário (pode ser nullptr)
     * @param onClick Callback ao clicar (opcional)
     * @param userData Dados do usuário (opcional)
     * @return Índice do item adicionado
     */
    int addItem(const char* title, const char* subtitle = nullptr,
                SelectCallback onClick = nullptr, void* userData = nullptr);
    
    /**
     * @brief Remove todos os itens da lista
     */
    void clear();
    
    /**
     * @brief Obtém o número de itens
     * @return Quantidade de itens
     */
    size_t count() const { return m_items.size(); }
    
    /**
     * @brief Obtém um item por índice
     * @param index Índice do item
     * @return Ponteiro para o item ou nullptr
     */
    const Item* getItem(int index) const;
    
    /**
     * @brief Define o callback global de seleção
     * 
     * Este callback é chamado para qualquer item que não tenha
     * um callback individual definido.
     * 
     * @param callback Função a ser chamada
     */
    void setOnSelect(SelectCallback callback);
    
    /**
     * @brief Obtém o container LVGL
     * @return Ponteiro para o objeto container
     */
    lv_obj_t* container() const { return m_container; }
    
    /**
     * @brief Rola para o topo da lista
     */
    void scrollToTop();
    
    /**
     * @brief Rola para um item específico
     * @param index Índice do item
     */
    void scrollToItem(int index);
    
    /**
     * @brief Define a altura de cada item
     * @param height Altura em pixels
     */
    void setItemHeight(int32_t height);
    
    /**
     * @brief Define o espaçamento entre itens
     * @param gap Espaçamento em pixels
     */
    void setItemGap(int32_t gap);
    
    /**
     * @brief Atualiza o texto de um item existente
     * 
     * @param index Índice do item
     * @param title Novo título (nullptr para não alterar)
     * @param subtitle Novo subtítulo (nullptr para não alterar)
     * @return true se atualizado com sucesso
     */
    bool updateItem(int index, const char* title, const char* subtitle = nullptr);
    
private:
    struct InternalItem {
        Item data;
        lv_obj_t* button = nullptr;
        lv_obj_t* titleLabel = nullptr;
        lv_obj_t* subtitleLabel = nullptr;
    };
    
    lv_obj_t* m_container = nullptr;
    std::vector<InternalItem> m_items;
    SelectCallback m_globalCallback;
    int32_t m_itemHeight = 40;
    int32_t m_itemGap = 4;
    int32_t m_width;
    
    void createItemButton(InternalItem& item, int index);
    static void itemClickHandler(lv_event_t* e);
};

} // namespace ui
