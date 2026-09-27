#include "WifiErrorCodes.h"

/**
 * @file WifiErrorCodes.cpp
 * @brief Implementation of WiFi-related error codes.
 */

namespace CommonErrorCodes {
    const ErrorCode WifiInitFailed = ErrorCode::define("WifiInitFailed", "WiFi initialization failed", ErrorCodeType::WiFi);
    const ErrorCode WifiConnectionFailed = ErrorCode::define("WifiConnectionFailed", "Failed to connect to WiFi network", ErrorCodeType::WiFi);
    const ErrorCode WifiAPStartFailed = ErrorCode::define("WifiAPStartFailed", "Failed to start WiFi Access Point", ErrorCodeType::WiFi);
    const ErrorCode WifiScanFailed = ErrorCode::define("WifiScanFailed", "WiFi scan for networks failed", ErrorCodeType::WiFi);
    const ErrorCode WifiNetworkNotFound = ErrorCode::define("WifiNetworkNotFound", "The specified WiFi network was not found", ErrorCodeType::WiFi);
    const ErrorCode WifiAuthFailed = ErrorCode::define("WifiAuthFailed", "WiFi authentication failed", ErrorCodeType::WiFi);
    const ErrorCode WifiStopFailed = ErrorCode::define("WifiStopFailed", "Failed to stop WiFi", ErrorCodeType::WiFi);
}