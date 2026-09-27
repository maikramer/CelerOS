#include "CommunicationErrorCodes.h"

/**
 * @file CommunicationErrorCodes.cpp
 * @brief Implementation of general communication error codes.
 */

namespace CommonErrorCodes {
    const ErrorCode CommunicationError = ErrorCode::define("CommunicationError", "A communication error occurred", ErrorCodeType::Communication);
    const ErrorCode CommunicationTimeout = ErrorCode::define("CommunicationTimeout", "Communication timeout", ErrorCodeType::Communication);
    const ErrorCode ChecksumError = ErrorCode::define("ChecksumError", "Checksum or CRC verification failed", ErrorCodeType::Communication);
}