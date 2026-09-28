#pragma once

/**
 * @file InfoList.h
 * @brief Lista de informações key-value
 * 
 * Componente para exibir pares label/valor de forma organizada,
 * ideal para telas "Sobre" e informações do sistema.
 */

#include "lvgl.h"
#include <vector>
#include <string>

namespace ui {

/**
 * @class InfoList
 * @brief Lista scrollável de informações key-value
 * 
 * Exemplo:
 * @code
 * ui::InfoList infoList(screen);
 * infoList.addItem("Versão", "1.0.0");
 * infoList.addItem("Device ID", "ABC123");
 * infoList.addSeparator();
 * infoList.addItem("Memória", "45 KB livre");
 * 
 * // Atualizar valor dinamicamente
 * infoList.updateValue(2, "50 KB livre");
 * @endcode
 */
class InfoList {
public:
    /**
     * @brief Construtor
     * 
     * @param parent Objeto pai
     * @param width Largura do container (padrão: largura total - padding)
     * @param height Altura do container (padrão: até o botão voltar)
     */
    InfoList(lv_obj_t* parent, int32_t width = 0, int32_t height = 0);
    
    /**
     * @brief Destrutor
     */
    ~InfoList();
    
    /**
     * @brief Adiciona um item label/valor
     * 
     * @param label Texto do label (cinza, menor)
     * @param value Texto do valor (preto, maior)
     * @return Índice do item adicionado
     */
    size_t addItem(const char* label, const char* value);
    
    /**
     * @brief Adiciona uma linha separadora
     */
    void addSeparator();
    
    /**
     * @brief Adiciona um título de seção
     * 
     * @param title Texto do título
     */
    void addSectionTitle(const char* title);
    
    /**
     * @brief Atualiza o valor de um item existente
     * 
     * @param index Índice do item (retornado por addItem)
     * @param value Novo valor
     * @return true se atualizado com sucesso
     */
    bool updateValue(size_t index, const char* value);
    
    /**
     * @brief Obtém a altura total do conteúdo
     * @return Altura em pixels
     */
    int32_t contentHeight() const;
    
    /**
     * @brief Obtém o container LVGL
     * @return Ponteiro para o objeto container
     */
    lv_obj_t* container() const { return m_container; }
    
    /**
     * @brief Rola para o topo
     */
    void scrollToTop();
    
    /**
     * @brief Rola para um item específico
     * @param index Índice do item
     */
    void scrollToItem(size_t index);
    
private:
    struct Item {
        lv_obj_t* labelObj = nullptr;
        lv_obj_t* valueObj = nullptr;
        std::string label;
        std::string value;
    };
    
    lv_obj_t* m_container = nullptr;
    std::vector<Item> m_items;
    int32_t m_yPos = 0;
    int32_t m_width;
    
    static constexpr int32_t LABEL_VALUE_GAP = 18;
    static constexpr int32_t LINE_SPACING = 38;
    static constexpr int32_t SECTION_SPACING = 12;
};

} // namespace ui
