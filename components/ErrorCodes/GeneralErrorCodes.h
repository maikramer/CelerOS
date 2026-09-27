#ifndef GENERAL_ERROR_CODES_H
#define GENERAL_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file GeneralErrorCodes.h
 * @brief Defines common, general-purpose error codes.
 */

namespace CommonErrorCodes {

        // General Errors
        extern const ErrorCode Invalid;             /**< Invalid error code. */
        extern const ErrorCode None;                /**< No error occurred. */
        extern const ErrorCode UnknownError;         /**< An unexpected or unknown error occurred. */
        extern const ErrorCode OperationFailed;      /**< The requested operation failed. */
        extern const ErrorCode NotImplemented;     /**< Functionality not yet implemented. */
        extern const ErrorCode NotInitialized;     /**< Component not initialized. */
        extern const ErrorCode ArgumentError;      /**< Invalid argument provided to a function or method. */
        extern const ErrorCode Timeout;             /**< A timeout occurred. */
        extern const ErrorCode CommandAlreadyRegistered; /**< Attempt to register an already registered command. */
        extern const ErrorCode InvalidCommand;      /**< An invalid command was received. */
        extern const ErrorCode ConnectionClosed;     /**< The connection is closed. */
        extern const ErrorCode ListIsEmpty;          /**< The list is empty. */

}

#endif // GENERAL_ERROR_CODES_H