#include "WifiOta.h"
#include <esp_log.h>

const char* WifiOta::TAG = "WifiOta";

/**
 * @file WifiOta.cpp
 * @brief Implementation of the WifiOta class for handling OTA firmware updates over HTTPS.
 */

WifiOta::WifiOta() = default;

ErrorCode WifiOta::startUpdate(const std::string& url) {
    ESP_LOGI(TAG, "Starting OTA update from: %s", url.c_str());
    onUpdateStart.trigger();
    return performUpdate(url);
}

ErrorCode WifiOta::performUpdate(const std::string& url) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.cert_pem = nullptr; // Replace with your server certificate if needed

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;

    esp_https_ota_handle_t https_ota_handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed! Error: %s", esp_err_to_name(err));
        onUpdateFailed.trigger();
        return CommonErrorCodes::OperationFailed;
    }

    // Perform OTA update com relatório de progresso
    int last_progress = -1;
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        
        // Calcular e reportar progresso
        int image_len_read = esp_https_ota_get_image_len_read(https_ota_handle);
        int image_size = esp_https_ota_get_image_size(https_ota_handle);
        
        if (image_size > 0) {
            int progress = (image_len_read * 100) / image_size;
            if (progress != last_progress && progress >= 0 && progress <= 100) {
                onProgress.trigger(progress);
                last_progress = progress;
            }
        }
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful!");
        esp_err_t ret = esp_https_ota_finish(https_ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA finish failed! Error: %s", esp_err_to_name(ret));
            onUpdateFailed.trigger();
            return CommonErrorCodes::OperationFailed;
        }
        onUpdateComplete.trigger();
        return CommonErrorCodes::None;
    } else {
        ESP_LOGE(TAG, "OTA update failed! Error: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        onUpdateFailed.trigger();
        // Return specific error codes based on the esp_https_ota error
        switch (err) {
            case ESP_ERR_INVALID_ARG:
                return CommonErrorCodes::ArgumentError;
            default:
                return CommonErrorCodes::OperationFailed; // General failure
        }
    }
}

esp_err_t WifiOta::otaProgressCallback(esp_https_ota_handle_t ota_handle, int progress, void* user_data) {
    auto* self = static_cast<WifiOta*>(user_data); // Get the WifiOta object
    self->onProgress.trigger(progress);
    return ESP_OK;
}