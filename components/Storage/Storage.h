#ifndef STORAGE_H
#define STORAGE_H

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include "CommonErrorCodes.h"
#include "JsonModels.h"
#include "projectConfig.h"
#include "esp_log.h"
#include "ff.h"
#include "NVS.h"

/**
 * @file Storage.h
 * @brief Defines the Storage class for handling file system operations
 *        on external storage (e.g., SD card).
 */

/**
 * @namespace StorageConstants
 * @brief Contains constants related to the storage module.
 */
namespace StorageConstants {
    constexpr const char* BasePath = "/storage";    /**< Base path for storage operations. */
    constexpr const char* UsersFilename = "users";  /**< File name for storing user data. */
    constexpr const char* ConfigFilename = "config";  /**< File name for storing configuration data. */
    constexpr const char* InfoFilename = "info";     /**< File name for storing general information data. */
}

/**
 * @struct StorageStatus
 * @brief Represents the current status of the storage device.
 */
struct StorageStatus {
    uint64_t freeSpace; /**< Available free space on the storage device (in bytes). */
    uint64_t totalSpace; /**< Total space on the storage device (in bytes). */
};

/**
 * @class Storage
 * @brief Provides a high-level interface for managing files and data on external storage.
 */
class Storage {
public:
    /**
     * @brief Initializes the Storage class.
     *
     * This function must be called before using any other Storage methods.
     * It will try to initialize file system first, and fallback to NVS if file system is not available.
     *
     * @return ErrorCode indicating success or failure of the initialization.
     */
    static ErrorCode initialize();
    
    /**
     * @brief Checks if file system storage is available.
     *
     * @return True if file system is available, false if using NVS fallback.
     */
    static bool isFileSystemAvailable();

    /**
     * @brief Erases all data from the storage device.
     *
     * @return ErrorCode indicating success or failure.
     */
    static ErrorCode eraseData();

    /**
     * @brief Deletes a file from the storage device.
     *
     * @param fileName The name of the file to delete (without the extension).
     * @return ErrorCode indicating success or failure.
     */
    static ErrorCode deleteFile(const std::string& fileName);

    /**
     * @brief Copies a file on the storage device.
     *
     * @param sourceFileName The name of the source file (without the extension).
     * @param destinationFileName The name of the destination file (without the extension).
     * @return ErrorCode indicating success or failure.
     */
    static ErrorCode copyFile(const std::string& sourceFileName, const std::string& destinationFileName);

    /**
     * @brief Gets the current status of the storage device.
     *
     * @param status A StorageStatus object that will be populated with the storage device information.
     * @return ErrorCode indicating success or failure.
     */
    static ErrorCode getStatus(StorageStatus& status);

    /**
     * @brief Stores a key-value pair in a file on the storage device.
     *
     * This method handles creating/opening the file, writing the data in the format "key=value",
     * and closing the file. If the key already exists in the file and `overwrite` is false,
     * the value will not be updated.
     *
     * @tparam TKey The type of the key. The type must be serializable to a string using `std::stringstream`.
     * @tparam TValue The type of the value. The type must be serializable to a string using `std::stringstream`.
     * @param key The key to store.
     * @param value The value to store.
     * @param fileName The name of the file to store the data in (without the ".txt" extension).
     * @param overwrite If true, an existing key will be overwritten; otherwise, the existing value is kept.
     * @return ErrorCode indicating success or failure. CommonErrorCodes::KeyAlreadyExists is returned
     *         if the key already exists and `overwrite` is false.
     */
    template<typename TKey, typename TValue>
    static ErrorCode storeKeyValue(const TKey& key, const TValue& value,
                                   const std::string& fileName, bool overwrite = true);

    /**
     * @brief Reads a value from a file based on its key.
     *
     * This method searches the file for a line matching the format "key=value". If a matching key is found,
     * the corresponding value is parsed and stored in the `value` reference.
     *
     * @tparam TKey The type of the key. The type must be serializable to a string using `std::stringstream`.
     * @tparam TValue The type of the value. The type must be deserializable from a string using `std::stringstream`.
     * @param key The key to search for.
     * @param value A reference to a variable that will store the read value.
     * @param fileName The name of the file to read from (without the ".txt" extension).
     * @return ErrorCode indicating success or failure. CommonErrorCodes::KeyNotFound is returned
     *         if the key is not found in the file.
     */
    template<typename TKey, typename TValue>
    static ErrorCode readKeyValue(const TKey& key, TValue& value, const std::string& fileName);

    /**
     * @brief Reads or creates a key-value pair from a file.
     *
     * If the key exists in the file, the value is read and stored in the `value` reference. If the key does not
     * exist or the file does not exist, the key-value pair is created in the file using the provided `defaultValue`.
     *
     * @tparam TKey The type of the key. The type must be serializable to a string using `std::stringstream`.
     * @tparam TValue The type of the value. The type must be serializable and deserializable from a string using `std::stringstream`.
     * @param key The key to read or create.
     * @param value A reference to a variable that will store the read or created value.
     * @param fileName The name of the file to read from/write to (without the ".txt" extension).
     * @param keyName The name of the key to use in the file (can be different from the `key` parameter).
     * @return ErrorCode indicating success or failure.
     */
    template<typename TKey, typename TValue>
    static ErrorCode readOrCreateKeyValue(const TKey& key, TValue& value,
                                          const std::string& fileName, const std::string& keyName);

    /**
     * @brief Retrieves all key-value pairs from a file and stores them in a map.
     *
     * The file is parsed line-by-line, assuming the format "key=value". Each key-value pair is added to the `dataMap`.
     *
     * @tparam TKey The type of the key. The type must be deserializable from a string using `std::stringstream`.
     * @tparam TValue The type of the value. The type must be deserializable from a string using `std::stringstream`.
     * @param fileName The name of the file to read from (without the ".txt" extension).
     * @param dataMap A reference to a map that will store the retrieved key-value pairs.
     * @return ErrorCode indicating success or failure. CommonErrorCodes::FileIsEmpty is returned if the file
     *         exists but contains no valid key-value pairs.
     */
    template<typename TKey, typename TValue>
    static ErrorCode getEntriesFromFile(const std::string& fileName, std::map<TKey, TValue>& dataMap);

#ifdef USER_MANAGEMENT_ENABLED
    // User-related storage operations
    static ErrorCode getEntriesFromUser(const std::string& user, std::map<int64_t, uint32_t>& dataMap);
    static ErrorCode storeUser(const JsonModels::User& user, bool overwrite = true);
    static ErrorCode loadUser(const std::string& userName, JsonModels::User& user);
    static ErrorCode getAllUsers(std::map<std::string, JsonModels::User>& usersMap);
#endif

    // Configuration storage operations
    static ErrorCode storeConfig(const std::string& key, const std::string& value, bool overwrite = true);
    static ErrorCode loadConfig(const std::string& key, std::string& value);

private:
    static uint32_t _sectorSize; /**< The sector size of the storage device. */
    static bool _fileSystemAvailable; /**< Flag indicating if file system is available. */
    static bool _initialized; /**< Flag indicating if Storage has been initialized. */

    /**
     * @brief Checks if a file name is reserved by the system.
     *
     * @param fileName The file name to check.
     * @return True if the file name is reserved, false otherwise.
     */
    static bool isReservedFileName(const std::string& fileName);

    /**
     * @brief Helper function to get the full file path with the base path and extension.
     *
     * @param fileName The file name (without extension).
     * @return The full file path.
     */
    static std::string getFilePath(const std::string& fileName);

    /**
     * @brief Stores a key-value pair in a file without checking for reserved filenames.
     *
     * @tparam TKey The type of the key. The type must be serializable to a string using `std::stringstream`.
     * @tparam TValue The type of the value. The type must be serializable to a string using `std::stringstream`.
     * @param key The key to store.
     * @param value The value to store.
     * @param fileName The name of the file to store the data in (without the extension).
     * @param overwrite If true, an existing key will be overwritten.
     * @return ErrorCode indicating success or failure.
     */
    template<typename TKey, typename TValue>
    static ErrorCode storeKeyValueInternal(const TKey& key, const TValue& value,
                                           const std::string& fileName, bool overwrite);
};

// Template Implementations (must be in the header file)

template<typename TKey, typename TValue>
ErrorCode Storage::storeKeyValue(const TKey& key, const TValue& value,
                                 const std::string& fileName, bool overwrite) {
    if (isReservedFileName(fileName)) {
        return CommonErrorCodes::ArgumentError;
    }
    return storeKeyValueInternal(key, value, fileName, overwrite);
}

template<typename TKey, typename TValue>
ErrorCode Storage::readKeyValue(const TKey& key, TValue& value, const std::string& fileName) {
    std::string filePath = getFilePath(fileName);

    std::ifstream input(filePath);
    if (!input) {
        // File doesn't exist - this is normal for first run, use debug level
        ESP_LOGD("Storage", "File not found: %s (first run?)", filePath.c_str());
        return CommonErrorCodes::FileOpenError;
    }

    std::string line;
    std::stringstream keyStream;
    while (std::getline(input, line)) {
        size_t separatorPos = line.find('=');
        if (separatorPos == std::string::npos) {
            continue; // Skip lines without the separator
        }

        std::string fileKey = line.substr(0, separatorPos);
        std::string fileValue = line.substr(separatorPos + 1);

        // Compare keys, using string conversion for TKey
        keyStream << key;
        if (fileKey == keyStream.str()) {
            std::stringstream valueStream(fileValue);
            valueStream >> value; // Read the value
            return CommonErrorCodes::None;
        }
    }

    ESP_LOGW("Storage", "Key '%s' not found in file: %s", keyStream.str().c_str(), filePath.c_str());
    return CommonErrorCodes::FileNotFound;
}

template<typename TKey, typename TValue>
ErrorCode Storage::readOrCreateKeyValue(const TKey& key, TValue& value,
                                        const std::string& fileName, const std::string& keyName) {
    ErrorCode err = readKeyValue(key, value, fileName);
    if (err == CommonErrorCodes::FileNotFound || err == CommonErrorCodes::FileIsEmpty) {
        // Key or file not found, so create the key-value pair
        ESP_LOGI("Storage", "Creating key '%s' in file: %s", keyName.c_str(), fileName.c_str());
        return storeKeyValue(keyName, value, fileName);
    } else if (err != CommonErrorCodes::None) {
        return err; // Return other errors
    }

    return CommonErrorCodes::None; // Key was found and value read successfully
}

template<typename TKey, typename TValue>
ErrorCode Storage::getEntriesFromFile(const std::string& fileName, std::map<TKey, TValue>& dataMap) {
    std::string filePath = getFilePath(fileName);
    std::ifstream input(filePath);
    if (!input) {
        ESP_LOGE("Storage", "Error opening file for reading: %s", filePath.c_str());
        return CommonErrorCodes::FileOpenError;
    }

    dataMap.clear();

    std::string line;
    while (std::getline(input, line)) {
        size_t separatorPos = line.find('=');
        if (separatorPos == std::string::npos) {
            continue; // Skip lines without separator
        }

        std::string fileKey = line.substr(0, separatorPos);
        std::string fileValue = line.substr(separatorPos + 1);

        // Convert strings to TKey and TValue
        std::stringstream keyStream(fileKey);
        std::stringstream valueStream(fileValue);

        TKey key;
        TValue value;
        keyStream >> key;
        valueStream >> value;

        dataMap[key] = value;
    }

    if (dataMap.empty()) {
        ESP_LOGW("Storage", "No entries found in file: %s", filePath.c_str());
        return CommonErrorCodes::FileIsEmpty;
    }

    return CommonErrorCodes::None;
}

template<typename TKey, typename TValue>
ErrorCode Storage::storeKeyValueInternal(const TKey& key, const TValue& value,
                                         const std::string& fileName, bool overwrite) {
    std::string filePath = getFilePath(fileName);

    // 1. Check if the key already exists
    if (!overwrite) {
        TValue existingValue;
        ErrorCode err = readKeyValue<TKey, TValue>(key, existingValue, fileName);
        if (err == CommonErrorCodes::None) {
            // Convert key to char* for logging
            std::stringstream keyStream;
            keyStream << key;
            ESP_LOGW("Storage", "Key '%s' already exists in file: %s", keyStream.str().c_str(), fileName.c_str());
            return CommonErrorCodes::FileExists;
        } else if (err != CommonErrorCodes::FileNotFound && err != CommonErrorCodes::FileIsEmpty) {
            return err;
        }
    }

    // 2. Open the file in append mode
    std::ofstream outputFile(filePath, std::ios::app);
    if (!outputFile) {
        ESP_LOGE("Storage", "Error opening file for writing: %s", filePath.c_str());
        return CommonErrorCodes::FileOpenError;
    }

    // 3. Write the key-value pair to the file
    outputFile << key << "=" << value << std::endl;
    outputFile.close();

    return CommonErrorCodes::None;
}
#endif // STORAGE_H