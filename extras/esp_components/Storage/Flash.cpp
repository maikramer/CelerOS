#include "Flash.h"

wl_handle_t Flash::_wearHandle = WL_INVALID_HANDLE;
bool Flash::_initialized = false;

/**
 * @file Flash.cpp
 * @brief Implementation for the Flash class, providing access to external flash memory.
 */

ErrorCode Flash::initialize() {
    if (_initialized) {
        return CommonErrorCodes::None; // Already initialized
    }

    // Get the SPI flash partition information
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                ESP_PARTITION_SUBTYPE_DATA_FAT,
                                                                "storage");
    if (partition == nullptr) {
        ESP_LOGE("Flash", "Failed to find FAT partition!");
        return CommonErrorCodes::StorageInitFailed;
    }

    // Attempt to mount the partition
    ErrorCode err = mountFlash(partition->label, StorageConstants::BasePath);
    if (err == CommonErrorCodes::None) {
        _initialized = true;
        ESP_LOGI("Flash", "Flash memory initialized successfully.");
    } else {
        // If mounting failed, format the partition and try again
        ESP_LOGW("Flash", "Failed to mount flash, formatting... (%s)", err.description().c_str());

        if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
            ESP_LOGE("Flash", "Failed to format flash partition!");
            return CommonErrorCodes::StorageInitFailed;
        }

        // Retry mounting after formatting
        err = mountFlash(partition->label, StorageConstants::BasePath);
        if (err == CommonErrorCodes::None) {
            _initialized = true;
            ESP_LOGI("Flash", "Flash memory formatted and initialized.");
        } else {
            ESP_LOGE("Flash", "Failed to initialize flash memory after formatting: %s",
                     err.description().c_str());
            return err;
        }
    }

    return CommonErrorCodes::None;
}

wl_handle_t Flash::getWearLevellingHandle() {
    return _wearHandle;
}

ErrorCode Flash::mountFlash(const char* partitionLabel, const char* mountPoint) {
    const esp_vfs_fat_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
            .disk_status_check_enable = false,
            .use_one_fat = false
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(mountPoint, partitionLabel, &mount_config, &_wearHandle);
    if (err != ESP_OK) {
        ESP_LOGE("Flash", "Failed to mount FATFS on flash: %s", esp_err_to_name(err));

        // Map ESP-IDF errors to CommonErrorCodes
        switch (err) {
            case ESP_ERR_NOT_FOUND:
                return CommonErrorCodes::StorageNotMounted;
            case ESP_ERR_INVALID_STATE:
                return CommonErrorCodes::StorageInitFailed;
                // ... Add more error mappings if needed
            default:
                return CommonErrorCodes::OperationFailed;
        }
    }

    return CommonErrorCodes::None;
}