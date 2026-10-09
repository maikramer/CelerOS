#ifndef COMMUNICATION_ERROR_CODES_H
#define COMMUNICATION_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file CommunicationErrorCodes.h
 * @brief Codigos de erro de comunicacao (Serial, I2C, SPI; ids 52..54 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros de comunicacao
inline constexpr ErrorCode CommunicationError{52};
inline constexpr ErrorCode CommunicationTimeout{53};
inline constexpr ErrorCode ChecksumError{54};

}

#endif // COMMUNICATION_ERROR_CODES_H
