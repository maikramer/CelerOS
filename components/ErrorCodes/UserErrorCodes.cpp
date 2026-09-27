#include "UserErrorCodes.h"

/**
 * @file UserErrorCodes.cpp
 * @brief Implementation of user management related error codes.
 */

namespace CommonErrorCodes {
    const ErrorCode AuthenticationFailed = ErrorCode::define("AuthenticationFailed", "User authentication failed", ErrorCodeType::User);
    const ErrorCode UserNotFound = ErrorCode::define("UserNotFound", "User not found", ErrorCodeType::User);
    const ErrorCode UserAlreadyExists = ErrorCode::define("UserAlreadyExists", "User already exists", ErrorCodeType::User);
}