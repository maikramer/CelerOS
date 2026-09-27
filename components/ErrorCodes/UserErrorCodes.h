#ifndef USER_ERROR_CODES_H
#define USER_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file UserErrorCodes.h
 * @brief Defines error codes related to user management operations.
 */

namespace CommonErrorCodes {

    // User-Related Errors
    extern const ErrorCode AuthenticationFailed;    /**< User authentication failed (e.g., incorrect password). */
    extern const ErrorCode UserNotFound;           /**< The specified user was not found. */
    extern const ErrorCode UserAlreadyExists;      /**< A user with the same credentials already exists. */

}

#endif // USER_ERROR_CODES_H