#include "NetworkErrorCodes.h"

/**
 * @file NetworkErrorCodes.cpp
 * @brief Implementation of network-related error codes.
 */

namespace CommonErrorCodes {
    const ErrorCode NetworkDown = ErrorCode::define("NetworkDown", "Network connection is down", ErrorCodeType::Network);
    const ErrorCode HostUnreachable = ErrorCode::define("HostUnreachable", "Host is unreachable", ErrorCodeType::Network);
    const ErrorCode ConnectionRefused = ErrorCode::define("ConnectionRefused", "Connection refused", ErrorCodeType::Network);
    const ErrorCode AddressInUse = ErrorCode::define("AddressInUse", "Network address is in use", ErrorCodeType::Network);
    
    // Socket Errors
    const ErrorCode SocketClosed = ErrorCode::define("SocketClosed", "Socket is closed", ErrorCodeType::Network);
    const ErrorCode SocketSendFailed = ErrorCode::define("SocketSendFailed", "Failed to send data through socket", ErrorCodeType::Network);
    const ErrorCode SocketReceiveFailed = ErrorCode::define("SocketReceiveFailed", "Failed to receive data from socket", ErrorCodeType::Network);
    const ErrorCode SocketCreationFailed = ErrorCode::define("SocketCreationFailed", "Failed to create socket", ErrorCodeType::Network);
    const ErrorCode SocketConnectFailed = ErrorCode::define("SocketConnectFailed", "Failed to connect socket", ErrorCodeType::Network);
    const ErrorCode SocketBindFailed = ErrorCode::define("SocketBindFailed", "Failed to bind socket", ErrorCodeType::Network);
    const ErrorCode SocketListenFailed = ErrorCode::define("SocketListenFailed", "Failed to listen on socket", ErrorCodeType::Network);
    const ErrorCode SocketAcceptFailed = ErrorCode::define("SocketAcceptFailed", "Failed to accept connection on socket", ErrorCodeType::Network);
}