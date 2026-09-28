#pragma once

#include "WifiOta.h"
#include "Event.h"
#include "CommonErrorCodes.h"
#include "esp_err.h"
#include "esp_mac.h"
#include <string>
#include <cstring>
#include <cstdio>
#include <functional>

/**
 * @file OtaManager.h
 * @brief Manager para OTA updates usando device_id para identificar dispositivos
 */

/**
 * @struct OtaUpdateInfo
 * @brief Resultado da verificação de atualização do servidor OTA
 * 
 * Esta struct é retornada por checkForUpdateFromServer() e contém
 * todas as informações sobre a atualização disponível.
 */
struct OtaUpdateInfo {
    bool success = false;           ///< true se a requisição HTTP foi bem sucedida
    int httpStatus = 0;             ///< Código HTTP da resposta (200, 404, etc)
    bool updateAvailable = false;   ///< true se há atualização disponível
    char latestVersion[32] = {0};   ///< Versão mais recente disponível
    int firmwareSize = 0;           ///< Tamanho do firmware em bytes
    char description[128] = {0};    ///< Descrição da atualização
};

/**
 * @class OtaManager
 * @brief Gerencia atualizações OTA usando device_id para identificar dispositivos específicos
 */
class OtaManager {
public:
    /**
     * @brief Obtém instância singleton do OtaManager
     */
    static OtaManager& instance();
    
    /**
     * @brief Inicializa o OtaManager
     * @return ESP_OK em caso de sucesso
     */
    esp_err_t init();
    
    /**
     * @brief Obtém o device_id do dispositivo (baseado no MAC address)
     * @return String com device_id (ex: "A1B2C3D4E5F6")
     */
    const char* getDeviceId() const;
    
    /**
     * @brief Verifica se há atualização disponível para este device_id
     * @param baseUrl URL base do servidor OTA (ex: "https://api.example.com/ota")
     * @param currentVersion Versão atual do firmware (ex: "1.0.0")
     * @param availableVersion Buffer para receber versão disponível (opcional)
     * @param availableVersionSize Tamanho do buffer
     * @return true se há atualização disponível, false caso contrário
     * @deprecated Use checkForUpdateFromServer() que retorna mais informações
     */
    bool checkForUpdate(const char* baseUrl, const char* currentVersion, 
                       char* availableVersion = nullptr, size_t availableVersionSize = 0);
    
    /**
     * @brief Verifica atualização via endpoint /ota/check do servidor
     * 
     * Faz HTTP GET para {baseUrl}/ota/check?device_id={id}&version={version}
     * e retorna struct com todas as informações da atualização disponível.
     * 
     * Formato esperado da resposta JSON:
     * {
     *   "updateAvailable": true/false,
     *   "latestVersion": "1.0.0",
     *   "firmwareSize": 1234567,
     *   "description": "Descrição da atualização"
     * }
     * 
     * @param baseUrl URL base do servidor (ex: "http://192.168.1.100:10234")
     * @param currentVersion Versão atual do firmware
     * @return OtaUpdateInfo com resultado da verificação
     */
    OtaUpdateInfo checkForUpdateFromServer(const char* baseUrl, const char* currentVersion);
    
    /**
     * @brief Inicia atualização OTA usando device_id
     * @param baseUrl URL base do servidor OTA
     * @param version Versão específica para baixar (opcional, nullptr = última versão)
     * @return ErrorCode indicando sucesso ou falha
     */
    ErrorCode startUpdate(const char* baseUrl, const char* version = nullptr);
    
    /**
     * @brief Inicia atualização OTA usando URL completa
     * @param url URL completa do firmware
     * @return ErrorCode indicando sucesso ou falha
     */
    ErrorCode startUpdateFromUrl(const std::string& url);
    
    /**
     * @brief Constrói URL de OTA usando device_id
     * @param baseUrl URL base do servidor OTA
     * @param version Versão específica (opcional)
     * @return URL completa com device_id como parâmetro
     */
    std::string buildOtaUrl(const char* baseUrl, const char* version = nullptr) const;
    
    /**
     * @brief Eventos do WifiOta (delegados)
     */
    Event<> onUpdateStart;
    Event<> onUpdateComplete;
    Event<> onUpdateFailed;
    Event<int> onProgress;

private:
    OtaManager();
    ~OtaManager() = default;
    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;
    
    WifiOta ota_;
    mutable char device_id_[17] = {0};  // 12 hex chars + null terminator
    mutable bool device_id_initialized_ = false;
    bool initialized_ = false;
    
    /**
     * @brief Inicializa device_id a partir do MAC address
     */
    void initDeviceId() const;
};
