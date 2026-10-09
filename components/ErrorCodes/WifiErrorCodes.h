#ifndef WIFI_ERROR_CODES_H
#define WIFI_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file WifiErrorCodes.h
 * @brief Codigos de erro de WiFi (ids 25..31 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros de WiFi
inline constexpr ErrorCode WifiInitFailed{25};
inline constexpr ErrorCode WifiConnectionFailed{26};
inline constexpr ErrorCode WifiAPStartFailed{27};
inline constexpr ErrorCode WifiScanFailed{28};
inline constexpr ErrorCode WifiNetworkNotFound{29};
inline constexpr ErrorCode WifiAuthFailed{30};
inline constexpr ErrorCode WifiStopFailed{31};

}

#endif // WIFI_ERROR_CODES_H
