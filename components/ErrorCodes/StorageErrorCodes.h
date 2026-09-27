#ifndef STORAGE_ERROR_CODES_H
#define STORAGE_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file StorageErrorCodes.h
 * @brief Defines error codes related to storage operations (e.g., SD card, Flash).
 */

namespace CommonErrorCodes {

    // Storage Errors (SD Card, Flash, etc.)
    extern const ErrorCode StorageInitFailed;       /**< Storage device initialization failed. */
    extern const ErrorCode StorageReadError;        /**< Error reading data from the storage device. */
    extern const ErrorCode StorageWriteError;       /**< Error writing data to the storage device. */
    extern const ErrorCode StorageNotMounted;      /**< The storage device is not mounted or accessible. */
    extern const ErrorCode StorageFull;            /**< The storage device is full. */

}

#endif // STORAGE_ERROR_CODES_H