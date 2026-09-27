#ifndef COMMUNICATION_ERROR_CODES_H
#define COMMUNICATION_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file CommunicationErrorCodes.h
 * @brief Defines general communication-related error codes (Serial, I2C, SPI, etc.). 
 */

namespace CommonErrorCodes {

    // Communication Errors
    extern const ErrorCode CommunicationError;      /**< A general communication error occurred. */
    extern const ErrorCode CommunicationTimeout;     /**< A timeout occurred during communication. */
    extern const ErrorCode ChecksumError;           /**< Data checksum or CRC verification failed. */

}

#endif // COMMUNICATION_ERROR_CODES_H