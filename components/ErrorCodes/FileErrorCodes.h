#ifndef FILE_ERROR_CODES_H
#define FILE_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file FileErrorCodes.h
 * @brief Defines error codes related to file system operations.
 */

namespace CommonErrorCodes {

    // File Errors
    extern const ErrorCode FileNotFound;            /**< The specified file was not found. */
    extern const ErrorCode FileOpenError;           /**< Error opening the file (e.g., permissions issue). */
    extern const ErrorCode FileReadError;          /**< Error reading data from the file. */
    extern const ErrorCode FileWriteError;         /**< Error writing data to the file. */
    extern const ErrorCode FileExists;              /**< A file with the specified name already exists. */
    extern const ErrorCode FileIsEmpty;            /**< The file is empty. */

}

#endif // FILE_ERROR_CODES_H