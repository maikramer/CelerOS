#pragma once

/**
 * @file Header.h
 * @brief Barra de status/header reutilizável
 * 
 * Componente para criar barras de status com:
 * - Ícone de status WiFi
 * - Botão de configurações
 * - Título opcional
 */

#include "lvgl.h"
#include <functional>

namespace ui {

/**
 * @class Header
 * @brief Componente de barra de status
 * 
 * Exemplo:
 * @code
 * auto* header = ui::Header::create(screen, {
 *     .showWifiStatus = true,
 *     .showSettingsButton = true,
 *     .title = "Minha App",
 *     .onSettingsClick = []() { openSettings(); }
 * });
 * 
 * // Atualizar status WiFi periodicamente
 * ui::Header::updateWifiStatus(header, networkMgr.isConnected());
 * @endcode
 */
class Header {
public:
    /**
     * @brief Configuração do header
     */
    struct Config {
        bool showWifiStatus = true;           ///< Mostrar ícone WiFi
        bool showSettingsButton = false;      ///< Mostrar botão configurações
        const char* title = nullptr;          ///< Título central (opcional)
        std::function<void()> onSettingsClick;///< Callback do botão settings
    };
    
    /**
     * @brief Cria um header
     * 
     * @param parent Objeto pai (normalmente a tela)
     * @param config Configuração do header
     * @return Ponteiro para o objeto do header
     */
    static lv_obj_t* create(lv_obj_t* parent, const Config& config);
    
    /**
     * @brief Atualiza o status do ícone WiFi
     * 
     * @param header Objeto do header criado por create()
     * @param connected true se WiFi está conectado
     */
    static void updateWifiStatus(lv_obj_t* header, bool connected);
    
    /**
     * @brief Obtém o objeto do ícone WiFi
     * 
     * @param header Objeto do header
     * @return Ponteiro para o label do ícone WiFi, ou nullptr
     */
    static lv_obj_t* getWifiIcon(lv_obj_t* header);
    
private:
    static void settingsButtonHandler(lv_event_t* e);
};

} // namespace ui
