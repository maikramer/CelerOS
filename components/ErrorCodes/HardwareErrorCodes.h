#ifndef HARDWARE_ERROR_CODES_H
#define HARDWARE_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file HardwareErrorCodes.h
 * @brief Codigos de erro de hardware (ids 50..51 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros de hardware
inline constexpr ErrorCode SensorError{50};
inline constexpr ErrorCode DeviceNotResponding{51};

}

#endif // HARDWARE_ERROR_CODES_H
