#include "BluetoothErrorCodes.h"

/**
 * @file BluetoothErrorCodes.cpp
 * @brief Implementation of Bluetooth-related error codes.
 */

namespace CommonErrorCodes {
    // Usando ErrorCodeType::Communication ao invés de ErrorCodeType::Bluetooth
    // para tornar o Bluetooth opcional - se não usar Bluetooth, não precisa compilar este arquivo
    const ErrorCode BluetoothInitFailed = ErrorCode::define("BluetoothInitFailed", "Bluetooth initialization failed", ErrorCodeType::Communication);
    const ErrorCode BluetoothConnectionFailed = ErrorCode::define("BluetoothConnectionFailed", "Failed to establish Bluetooth connection", ErrorCodeType::Communication);
    const ErrorCode BluetoothServiceCreationFailed = ErrorCode::define("BluetoothServiceCreationFailed", "Failed to create Bluetooth service", ErrorCodeType::Communication);
    const ErrorCode BluetoothCharacteristicCreationFailed = ErrorCode::define("BluetoothCharacteristicCreationFailed", "Failed to create Bluetooth characteristic", ErrorCodeType::Communication);
    // ... Add definitions for more Bluetooth error codes ...
}