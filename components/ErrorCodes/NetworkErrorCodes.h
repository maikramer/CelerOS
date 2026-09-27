#ifndef NETWORK_ERROR_CODES_H
#define NETWORK_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file NetworkErrorCodes.h
 * @brief Defines error codes related to network operations.
 */

namespace CommonErrorCodes {

    // Network Errors
    extern const ErrorCode NetworkDown;        /**< The network connection is down or unavailable. */
    extern const ErrorCode HostUnreachable;     /**< The specified host is unreachable. */
    extern const ErrorCode ConnectionRefused;    /**< The connection attempt was refused. */
    extern const ErrorCode AddressInUse;         /**< The requested network address is already in use. */
    
    // Socket Errors
    extern const ErrorCode SocketClosed;        /**< The socket is closed. */
    extern const ErrorCode SocketSendFailed;    /**< Failed to send data through the socket. */
    extern const ErrorCode SocketReceiveFailed; /**< Failed to receive data from the socket. */
    extern const ErrorCode SocketCreationFailed; /**< Failed to create a socket. */
    extern const ErrorCode SocketConnectFailed;  /**< Failed to connect the socket. */
    extern const ErrorCode SocketBindFailed;    /**< Failed to bind the socket. */
    extern const ErrorCode SocketListenFailed;  /**< Failed to listen on the socket. */
    extern const ErrorCode SocketAcceptFailed;  /**< Failed to accept a connection on the socket. */

}

#endif // NETWORK_ERROR_CODES_H