#include "Storage.h"
#include <dirent.h>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "NVS.h"

/**
 * @file Storage.cpp
 * @brief Implementation of the Storage class for managing files and data on external storage.
 */

uint32_t Storage::_sectorSize;
bool Storage::_fileSystemAvailable = false;
bool Storage::_initialized = false;

ErrorCode Storage::initialize() {
    // If already initialized, just return success
    if (_initialized) {
        return CommonErrorCodes::None;
    }

    // Always initialize NVS first (for fallback)
    ErrorCode nvs_err = NVS::initialize();
    if (nvs_err != CommonErrorCodes::None) {
        ESP_LOGE("Storage", "Failed to initialize NVS: %s", nvs_err.description().c_str());
        return nvs_err;
    }

    // Try to mount SPIFFS partition first (configured in partition table)
    // Check if SPIFFS is already mounted by checking if the base path exists
    struct stat st = {};
    bool spiffs_already_mounted = (stat(StorageConstants::BasePath, &st) == 0);
    
    if (!spiffs_already_mounted) {
        esp_vfs_spiffs_conf_t spiffs_conf = {
            .base_path = StorageConstants::BasePath,
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = true  // Format if mount fails (for first time)
        };

        esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
        if (ret == ESP_OK) {
            // SPIFFS mounted successfully
            _fileSystemAvailable = true;
            _sectorSize = 4096; // Typical SPIFFS page size
            
            // Check SPIFFS info
            size_t total = 0, used = 0;
            ret = esp_spiffs_info(spiffs_conf.partition_label, &total, &used);
            if (ret == ESP_OK) {
                ESP_LOGI("Storage", "SPIFFS mounted successfully at %s (total: %d KB, used: %d KB)", 
                         StorageConstants::BasePath, total / 1024, used / 1024);
            } else {
                ESP_LOGI("Storage", "SPIFFS mounted successfully at %s", StorageConstants::BasePath);
        }
        } else if (ret == ESP_ERR_INVALID_STATE) {
            // SPIFFS already mounted (by previous call)
            ESP_LOGI("Storage", "SPIFFS already mounted, using existing mount");
            _fileSystemAvailable = true;
            _sectorSize = 4096;
        } else {
            // SPIFFS mount failed - use NVS fallback
            ESP_LOGW("Storage", "Failed to mount SPIFFS (%s), will use NVS fallback", esp_err_to_name(ret));
            _fileSystemAvailable = false;
            _sectorSize = 512; // Default sector size
            
            // Clean up any partial mount
            if (ret == ESP_FAIL) {
                ESP_LOGW("Storage", "SPIFFS partition may need formatting");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGW("Storage", "SPIFFS partition 'storage' not found in partition table");
            }
        }
    } else {
        // SPIFFS path exists, assume it's already mounted
        ESP_LOGI("Storage", "SPIFFS appears to be already mounted at %s", StorageConstants::BasePath);
        _fileSystemAvailable = true;
        _sectorSize = 4096;
    }

    _initialized = true;
    return CommonErrorCodes::None;
}

bool Storage::isFileSystemAvailable() {
    return _fileSystemAvailable;
}

ErrorCode Storage::eraseData() {
    DIR *dir = opendir(StorageConstants::BasePath);
    if (dir == nullptr) {
        ESP_LOGE("Storage", "Failed to open storage directory: %s", StorageConstants::BasePath);
        return CommonErrorCodes::OperationFailed;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue; // Skip "." and ".." entries
        }

        std::string filePath = getFilePath(entry->d_name);
        if (remove(filePath.c_str()) != 0) {
            ESP_LOGE("Storage", "Failed to delete file: %s", filePath.c_str());
            closedir(dir);
            return CommonErrorCodes::OperationFailed;
        }
    }

    closedir(dir);
    return CommonErrorCodes::None;
}

ErrorCode Storage::deleteFile(const std::string& fileName) {
    std::string filePath = getFilePath(fileName);
    if (remove(filePath.c_str()) != 0) {
        ESP_LOGE("Storage", "Failed to delete file: %s", filePath.c_str());
        return CommonErrorCodes::FileNotFound;
    }
    return CommonErrorCodes::None;
}

ErrorCode Storage::copyFile(const std::string& sourceFileName, const std::string& destinationFileName) {
    std::string sourcePath = getFilePath(sourceFileName);
    std::string destPath = getFilePath(destinationFileName);

    std::ifstream sourceFile(sourcePath, std::ios::binary);
    std::ofstream destFile(destPath, std::ios::binary);

    if (!sourceFile.is_open()) {
        ESP_LOGE("Storage", "Failed to open source file for copying: %s", sourcePath.c_str());
        return CommonErrorCodes::FileOpenError;
    }
    if (!destFile.is_open()) {
        ESP_LOGE("Storage", "Failed to open destination file for copying: %s", destPath.c_str());
        sourceFile.close();
        return CommonErrorCodes::FileOpenError;
    }

    destFile << sourceFile.rdbuf();

    sourceFile.close();
    destFile.close();

    return CommonErrorCodes::None;
}

ErrorCode Storage::getStatus(StorageStatus& status) {
    FATFS *fs = nullptr;
    DWORD fre_clust;

    if (f_getfree("0:", &fre_clust, &fs) != FR_OK) {
        ESP_LOGE("Storage", "Failed to get storage status.");
        return CommonErrorCodes::StorageInitFailed;
    }

    if (fs == nullptr) {
        return CommonErrorCodes::StorageInitFailed;
    }

    status.totalSpace = (fs->n_fatent - 2) * fs->csize * _sectorSize;
    status.freeSpace = fre_clust * fs->csize * _sectorSize;

    return CommonErrorCodes::None;
}

#ifdef USER_MANAGEMENT_ENABLED
// User-related storage operations
ErrorCode Storage::getEntriesFromUser(const std::string& user, std::map<int64_t, uint32_t>& dataMap) {
    return getEntriesFromFile(user, dataMap);
}

ErrorCode Storage::storeUser(const JsonModels::User& user, bool overwrite) {
    ESP_LOGI("Storage", "Storing user: %s", user.Name.c_str());
    if (isReservedFileName(user.Name)) {
        return CommonErrorCodes::ArgumentError;
    }
    return storeKeyValueInternal(user.Name, user, StorageConstants::UsersFilename, overwrite);
}

ErrorCode Storage::loadUser(const std::string& userName, JsonModels::User& user) {
    return readKeyValue(userName, user, StorageConstants::UsersFilename);
}

ErrorCode Storage::getAllUsers(std::map<std::string, JsonModels::User>& usersMap) {
    ErrorCode result = getEntriesFromFile(StorageConstants::UsersFilename, usersMap);
    if (result == CommonErrorCodes::FileNotFound || result == CommonErrorCodes::FileIsEmpty) {
        return CommonErrorCodes::UserNotFound;
    }
    return result;
}
#endif

// Configuration storage operations
// Try file system first, fallback to NVS if file system is not available
ErrorCode Storage::storeConfig(const std::string& key, const std::string& value, bool overwrite) {
    if (_fileSystemAvailable) {
        // Try to store in file system first
        ESP_LOGI("Storage", "Storing config in file system: %s", key.c_str());
        ErrorCode err = storeKeyValue(key, value, StorageConstants::ConfigFilename, overwrite);
        if (err == CommonErrorCodes::None) {
            return CommonErrorCodes::None;
        }
        // If file system write failed, fallback to NVS
        ESP_LOGW("Storage", "File system write failed (%s), falling back to NVS", err.description().c_str());
    }
    
    // Fallback to NVS
    ESP_LOGI("Storage", "Storing config in NVS (fallback): %s", key.c_str());
    // NVS::storeValue will create the namespace automatically when opening in READWRITE mode
    return NVS::storeValue("config", key, value);
}

ErrorCode Storage::loadConfig(const std::string& key, std::string& value) {
    if (_fileSystemAvailable) {
        // Try to load from file system first
        ErrorCode err = readKeyValue(key, value, StorageConstants::ConfigFilename);
        if (err == CommonErrorCodes::None) {
            return CommonErrorCodes::None;
        }
        // If file not found or empty, try NVS fallback (this is normal for first run)
        if (err == CommonErrorCodes::FileNotFound || err == CommonErrorCodes::FileIsEmpty || 
            err == CommonErrorCodes::FileOpenError) {
            ESP_LOGD("Storage", "Config not found in file system, trying NVS fallback");
        } else {
            // Other error from file system, still try NVS
            ESP_LOGW("Storage", "File system read error (%s), trying NVS fallback", err.description().c_str());
        }
    }
    
    // Fallback to NVS
    ErrorCode err = NVS::readValue("config", key, value);
    if (err == CommonErrorCodes::FileNotFound) {
        // Namespace or key doesn't exist yet - this is OK for first run
        ESP_LOGD("Storage", "Config key '%s' not found in NVS (first run)", key.c_str());
    }
    return err;
}

bool Storage::isReservedFileName(const std::string& fileName) {
    return (fileName == StorageConstants::ConfigFilename ||
            fileName == StorageConstants::UsersFilename ||
            fileName == StorageConstants::InfoFilename);
}

std::string Storage::getFilePath(const std::string& fileName) {
    return std::string(StorageConstants::BasePath) + "/" + fileName + ".txt";
}