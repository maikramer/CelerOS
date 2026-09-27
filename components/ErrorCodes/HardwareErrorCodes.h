#ifndef HARDWARE_ERROR_CODES_H
#define HARDWARE_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file HardwareErrorCodes.h
 * @brief Defines error codes related to hardware operations. 
 */

namespace CommonErrorCodes {

    // Hardware Errors
    extern const ErrorCode SensorError;             /**< A sensor reading failed or returned an invalid value. */
    extern const ErrorCode DeviceNotResponding;    /**< A hardware device is not responding. */
    // ... (Add more hardware-related error codes) ...

}

#endif // HARDWARE_ERROR_CODES_H