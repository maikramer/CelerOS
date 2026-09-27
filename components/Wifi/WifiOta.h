#ifndef WIFI_OTA_H
#define WIFI_OTA_H

#include "esp_https_ota.h"
#include "esp_log.h"
#include "Event.h"
#include "CommonErrorCodes.h" // For error handling

/**
 * @file WifiOta.h
 * @brief This file defines the WifiOta class for performing Over-the-Air (OTA) firmware updates.
 */

/**
 * @class WifiOta
 * @brief Handles OTA updates over HTTPS, providing a robust and safe mechanism for firmware upgrades.
 */
class WifiOta {
public:
    /**
     * @brief Constructor.
     */
    WifiOta();

    /**
     * @brief Starts the OTA update process from the given URL.
     *
     * @param url The URL of the firmware image to download and install.
     * @return ErrorCode indicating success or failure of the OTA update process.
     */
    ErrorCode startUpdate(const std::string& url);

    /**
     * @brief Event triggered when the OTA update process starts.
     */
    Event<> onUpdateStart;

    /**
     * @brief Event triggered when the OTA update completes successfully.
     */
    Event<> onUpdateComplete;

    /**
     * @brief Event triggered when the OTA update fails.
     */
    Event<> onUpdateFailed;

    /**
     * @brief Event triggered when the OTA update progress changes.
     *
     * The event handler receives the percentage of the update progress (0-100).
     */
    Event<int> onProgress;

private:
    static const char* TAG; /**< Tag for logging messages. */

    /**
     * @brief Handles the OTA update process.
     *
     * @param url The URL of the firmware image.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode performUpdate(const std::string& url);

    /**
     * @brief Callback function for esp_https_ota to report update progress.
     *
     * @param ota_handle The handle to the OTA update process.
     * @param progress The current progress percentage (0-100).
     * @param user_data User data pointer (optional).
     * @return ESP_OK if the callback was handled successfully, ESP_FAIL otherwise.
     */
    static esp_err_t otaProgressCallback(esp_https_ota_handle_t ota_handle, int progress, void* user_data);
};

#endif // WIFI_OTA_H