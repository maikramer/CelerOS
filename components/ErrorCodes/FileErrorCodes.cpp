#include "FileErrorCodes.h"

/**
 * @file FileErrorCodes.cpp
 * @brief Implementation of file system error codes.
 */

namespace CommonErrorCodes {
    const ErrorCode FileNotFound = ErrorCode::define("FileNotFound", "File not found", ErrorCodeType::File);
    const ErrorCode FileOpenError = ErrorCode::define("FileOpenError", "Error opening file", ErrorCodeType::File);
    const ErrorCode FileReadError = ErrorCode::define("FileReadError", "Error reading from file", ErrorCodeType::File);
    const ErrorCode FileWriteError = ErrorCode::define("FileWriteError", "Error writing to file", ErrorCodeType::File);
    const ErrorCode FileExists = ErrorCode::define("FileExists", "File already exists", ErrorCodeType::File);
    const ErrorCode FileIsEmpty = ErrorCode::define("FileIsEmpty", "File is empty", ErrorCodeType::File);
}