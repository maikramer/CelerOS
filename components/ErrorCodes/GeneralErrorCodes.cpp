#include "GeneralErrorCodes.h"

/**
 * @file GeneralErrorCodes.cpp
 * @brief Implementation of the general error codes.
 */

namespace CommonErrorCodes {
    const ErrorCode Invalid = ErrorCode::define("Invalid", "Invalid error code", ErrorCodeType::General);
    const ErrorCode None = ErrorCode::define("None", "No error occurred", ErrorCodeType::General);
    const ErrorCode UnknownError = ErrorCode::define("UnknownError", "An unexpected error occurred", ErrorCodeType::General);
    const ErrorCode OperationFailed = ErrorCode::define("OperationFailed", "The requested operation failed", ErrorCodeType::General);
    const ErrorCode NotImplemented = ErrorCode::define("NotImplemented", "Functionality not implemented yet", ErrorCodeType::General);
    const ErrorCode NotInitialized = ErrorCode::define("NotInitialized", "Component not initialized", ErrorCodeType::General);
    const ErrorCode ArgumentError = ErrorCode::define("ArgumentError", "Invalid argument provided", ErrorCodeType::General);
    const ErrorCode Timeout = ErrorCode::define("Timeout", "Operation timed out", ErrorCodeType::General);
    const ErrorCode CommandAlreadyRegistered = ErrorCode::define("CommandAlreadyRegistered", "Command already registered", ErrorCodeType::General);
    const ErrorCode InvalidCommand = ErrorCode::define("InvalidCommand", "Invalid command received", ErrorCodeType::General);
    const ErrorCode ConnectionClosed = ErrorCode::define("ConnectionClosed", "Connection is closed", ErrorCodeType::General);
    const ErrorCode ListIsEmpty = ErrorCode::define("ListIsEmpty", "The list is empty", ErrorCodeType::General);
}