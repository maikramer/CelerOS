#ifndef WIFI_ERROR_CODES_H
#define WIFI_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file WifiErrorCodes.h
 * @brief Defines error codes specific to WiFi operations.
 */

namespace CommonErrorCodes {

    // WiFi Errors
    extern const ErrorCode WifiInitFailed;         /**< Initialization of the WiFi module failed. */
    extern const ErrorCode WifiConnectionFailed;    /**< Failed to connect to the specified WiFi network. */
    extern const ErrorCode WifiAPStartFailed;       /**< Failed to start the WiFi Access Point. */
    extern const ErrorCode WifiScanFailed;          /**< WiFi scan for networks failed. */
    extern const ErrorCode WifiNetworkNotFound;     /**< The specified WiFi network was not found. */
    extern const ErrorCode WifiAuthFailed;          /**< WiFi authentication failed (wrong password, etc.) */
    extern const ErrorCode WifiStopFailed;          /**< Failed to stop WiFi. */

}

#endif // WIFI_ERROR_CODES_H