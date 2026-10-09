#ifndef BLUETOOTH_ERROR_CODES_H
#define BLUETOOTH_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file BluetoothErrorCodes.h
 * @brief Codigos de erro de Bluetooth (ids 32..35 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros de Bluetooth
inline constexpr ErrorCode BluetoothInitFailed{32};
inline constexpr ErrorCode BluetoothConnectionFailed{33};
inline constexpr ErrorCode BluetoothServiceCreationFailed{34};
inline constexpr ErrorCode BluetoothCharacteristicCreationFailed{35};

}

#endif // BLUETOOTH_ERROR_CODES_H
