#ifndef BLUETOOTH_ERROR_CODES_H
#define BLUETOOTH_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file BluetoothErrorCodes.h
 * @brief Defines error codes specific to Bluetooth operations. 
 */

namespace CommonErrorCodes {

    // Bluetooth Errors
    extern const ErrorCode BluetoothInitFailed;        /**< Initialization of the Bluetooth module failed. */
    extern const ErrorCode BluetoothConnectionFailed;   /**< Failed to establish a Bluetooth connection. */
    extern const ErrorCode BluetoothServiceCreationFailed; /**< Failed to create a Bluetooth service. */
    extern const ErrorCode BluetoothCharacteristicCreationFailed; /**< Failed to create a Bluetooth characteristic. */
    // ... Add more Bluetooth specific error codes ...

}

#endif // BLUETOOTH_ERROR_CODES_H