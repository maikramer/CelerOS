#include "OtaManager.h"
#include "HttpClient.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "Event.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace {
constexpr char TAG[] = "OtaManager";
}

OtaManager::OtaManager() : initialized_(false) {
    // Conectar eventos do WifiOta aos eventos públicos usando addHandler
    ota_.onUpdateStart.addHandler([this]() { onUpdateStart.trigger(); });
    ota_.onUpdateComplete.addHandler([this]() { onUpdateComplete.trigger(); });
    ota_.onUpdateFailed.addHandler([this]() { onUpdateFailed.trigger(); });
    ota_.onProgress.addHandler([this](int progress) { onProgress.trigger(progress); });
}

OtaManager& OtaManager::instance() {
    static OtaManager manager;
    return manager;
}

esp_err_t OtaManager::init() {
    if (initialized_) {
        return ESP_OK;
    }
    
    initDeviceId();
    ESP_LOGI(TAG, "OtaManager inicializado para device_id: %s", device_id_);
    
    initialized_ = true;
    return ESP_OK;
}

void OtaManager::initDeviceId() const {
    if (device_id_initialized_) {
        return;
    }
    
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err == ESP_OK) {
        snprintf(device_id_, sizeof(device_id_),
                 "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGE(TAG, "Falha ao ler MAC address: %s", esp_err_to_name(err));
        strncpy(device_id_, "UNKNOWN", sizeof(device_id_) - 1);
        device_id_[sizeof(device_id_) - 1] = '\0';
    }
    
    device_id_initialized_ = true;
}

const char* OtaManager::getDeviceId() const {
    if (!device_id_initialized_) {
        initDeviceId();
    }
    return device_id_;
}

std::string OtaManager::buildOtaUrl(const char* baseUrl, const char* version) const {
    std::string url(baseUrl);
    
    // Adicionar device_id como query parameter
    char query[256] = {0};
    if (version != nullptr && strlen(version) > 0) {
        snprintf(query, sizeof(query), "?device_id=%s&version=%s", getDeviceId(), version);
    } else {
        snprintf(query, sizeof(query), "?device_id=%s", getDeviceId());
    }
    
    url += query;
    return url;
}

bool OtaManager::checkForUpdate(const char* baseUrl, const char* currentVersion,
                                char* availableVersion, size_t availableVersionSize) {
    if (baseUrl == nullptr || currentVersion == nullptr) {
        ESP_LOGE(TAG, "Parâmetros inválidos para verificar atualização");
        return false;
    }
    
    ESP_LOGI(TAG, "Verificando atualizações para device_id: %s", getDeviceId());
    
    // Construir URL de verificação
    std::string checkUrl = buildOtaUrl(baseUrl, nullptr);
    // Adicionar action e current_version se ainda não estiverem na URL
    if (checkUrl.find("action=") == std::string::npos) {
        checkUrl += (checkUrl.find('?') != std::string::npos ? "&" : "?");
        checkUrl += "action=check";
    }
    if (checkUrl.find("current_version=") == std::string::npos) {
        checkUrl += "&current_version=";
        checkUrl += currentVersion;
    }
    
    ESP_LOGI(TAG, "URL de verificação: %s", checkUrl.c_str());
    
    // Configurar cliente HTTP
    esp_http_client_config_t config = {};
    config.url = checkUrl.c_str();
    config.timeout_ms = 10000;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Falha ao inicializar cliente HTTP");
        return false;
    }
    
    // Fazer requisição GET
    esp_err_t err = esp_http_client_perform(client);
    bool updateAvailable = false;
    
    if (err == ESP_OK) {
        int statusCode = esp_http_client_get_status_code(client);
        int contentLength = esp_http_client_get_content_length(client);
        
        ESP_LOGI(TAG, "Status HTTP: %d, Content-Length: %d", statusCode, contentLength);
        
        if (statusCode == 200 && contentLength > 0) {
            // Ler resposta JSON
            char* buffer = (char*)malloc(contentLength + 1);
            if (buffer != nullptr) {
                int dataRead = esp_http_client_read_response(client, buffer, contentLength);
                buffer[dataRead] = '\0';
                
                ESP_LOGI(TAG, "Resposta: %s", buffer);
                
                // Parse JSON simples (sem cJSON para evitar dependência extra)
                // Procurar por "update_available":true no JSON
                const char* updateAvailableStr = strstr(buffer, "\"update_available\"");
                if (updateAvailableStr != nullptr) {
                    const char* trueStr = strstr(updateAvailableStr, "true");
                    if (trueStr != nullptr && (trueStr - updateAvailableStr) < 50) {
                        updateAvailable = true;
                        
                        // Extrair versão se solicitado
                        if (availableVersion != nullptr && availableVersionSize > 0) {
                            const char* versionStr = strstr(buffer, "\"version\"");
                            if (versionStr != nullptr) {
                                const char* versionValue = strstr(versionStr, "\"");
                                if (versionValue != nullptr) {
                                    versionValue++; // Pular aspas de abertura
                                    const char* versionEnd = strstr(versionValue, "\"");
                                    if (versionEnd != nullptr) {
                                        size_t versionLen = versionEnd - versionValue;
                                        if (versionLen < availableVersionSize) {
                                            strncpy(availableVersion, versionValue, versionLen);
                                            availableVersion[versionLen] = '\0';
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                free(buffer);
            }
        }
    } else {
        ESP_LOGE(TAG, "Erro ao verificar atualização: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return updateAvailable;
}

ErrorCode OtaManager::startUpdate(const char* baseUrl, const char* version) {
    if (baseUrl == nullptr) {
        ESP_LOGE(TAG, "URL base não pode ser nullptr");
        return CommonErrorCodes::ArgumentError;
    }
    
    std::string url = buildOtaUrl(baseUrl, version);
    ESP_LOGI(TAG, "Iniciando OTA update de: %s", url.c_str());
    
    return ota_.startUpdate(url);
}

ErrorCode OtaManager::startUpdateFromUrl(const std::string& url) {
    ESP_LOGI(TAG, "Iniciando OTA update de URL: %s", url.c_str());
    return ota_.startUpdate(url);
}

OtaUpdateInfo OtaManager::checkForUpdateFromServer(const char* baseUrl, const char* currentVersion) {
    OtaUpdateInfo info;
    
    if (baseUrl == nullptr || currentVersion == nullptr) {
        ESP_LOGE(TAG, "Parâmetros inválidos para checkForUpdateFromServer");
        info.success = false;
        info.httpStatus = 0;
        return info;
    }
    
    // Construir URL: {baseUrl}/ota/check?device_id={id}&version={version}
    char checkUrl[256];
    snprintf(checkUrl, sizeof(checkUrl), "%s/ota/check?device_id=%s&version=%s",
             baseUrl, getDeviceId(), currentVersion);
    
    ESP_LOGI(TAG, "Verificando atualizações: %s", checkUrl);
    
    // Fazer requisição HTTP GET
    HttpClient httpClient;
    httpClient.setHeader("X-Device-Id", getDeviceId());
    httpClient.setHeader("X-Current-Version", currentVersion);
    httpClient.setTimeout(15000);  // 15 segundos timeout
    
    auto response = httpClient.get(checkUrl);
    
    info.httpStatus = response.statusCode;
    info.success = response.isOk();
    
    if (!response.isOk()) {
        ESP_LOGE(TAG, "Erro ao verificar atualização: HTTP %d", response.statusCode);
        return info;
    }
    
    // Parsear resposta JSON
    try {
        auto json = nlohmann::json::parse(response.body);
        
        info.updateAvailable = json.value("updateAvailable", false);
        
        std::string latestVersion = json.value("latestVersion", "");
        if (!latestVersion.empty()) {
            strncpy(info.latestVersion, latestVersion.c_str(), sizeof(info.latestVersion) - 1);
            info.latestVersion[sizeof(info.latestVersion) - 1] = '\0';
        }
        
        info.firmwareSize = json.value("firmwareSize", 0);
        
        std::string description = json.value("description", "");
        if (!description.empty()) {
            strncpy(info.description, description.c_str(), sizeof(info.description) - 1);
            info.description[sizeof(info.description) - 1] = '\0';
        }
        
        ESP_LOGI(TAG, "Verificação concluída: updateAvailable=%d, latestVersion=%s",
                 info.updateAvailable, info.latestVersion);
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Erro ao parsear resposta JSON: %s", e.what());
        info.success = false;
    }
    
    return info;
}
