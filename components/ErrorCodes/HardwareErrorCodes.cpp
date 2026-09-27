#include "HardwareErrorCodes.h"

/**
 * @file HardwareErrorCodes.cpp
 * @brief Implementation of hardware-related error codes.
 */

namespace CommonErrorCodes {
    const ErrorCode SensorError = ErrorCode::define("SensorError", "Sensor reading failed or is invalid", ErrorCodeType::Hardware);
    const ErrorCode DeviceNotResponding = ErrorCode::define("DeviceNotResponding", "Hardware device not responding", ErrorCodeType::Hardware);
    // ... (Add more hardware error code definitions) ...
}