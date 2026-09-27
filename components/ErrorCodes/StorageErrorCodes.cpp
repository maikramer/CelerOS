#include "StorageErrorCodes.h"

/**
 * @file StorageErrorCodes.cpp
 * @brief Implementation of storage-related error codes.
 */

namespace CommonErrorCodes {
    const ErrorCode StorageInitFailed = ErrorCode::define("StorageInitFailed", "Storage device initialization failed", ErrorCodeType::Storage);
    const ErrorCode StorageReadError = ErrorCode::define("StorageReadError", "Error reading from storage device", ErrorCodeType::Storage);
    const ErrorCode StorageWriteError = ErrorCode::define("StorageWriteError", "Error writing to storage device", ErrorCodeType::Storage);
    const ErrorCode StorageNotMounted = ErrorCode::define("StorageNotMounted", "Storage device not mounted", ErrorCodeType::Storage);
    const ErrorCode StorageFull = ErrorCode::define("StorageFull", "Storage device is full", ErrorCodeType::Storage);
}