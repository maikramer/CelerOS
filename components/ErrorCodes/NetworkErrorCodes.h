#ifndef NETWORK_ERROR_CODES_H
#define NETWORK_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file NetworkErrorCodes.h
 * @brief Codigos de erro de rede e socket (ids 13..24 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros de rede
inline constexpr ErrorCode NetworkDown{13};
inline constexpr ErrorCode HostUnreachable{14};
inline constexpr ErrorCode ConnectionRefused{15};
inline constexpr ErrorCode AddressInUse{16};

// Erros de socket
inline constexpr ErrorCode SocketClosed{17};
inline constexpr ErrorCode SocketSendFailed{18};
inline constexpr ErrorCode SocketReceiveFailed{19};
inline constexpr ErrorCode SocketCreationFailed{20};
inline constexpr ErrorCode SocketConnectFailed{21};
inline constexpr ErrorCode SocketBindFailed{22};
inline constexpr ErrorCode SocketListenFailed{23};
inline constexpr ErrorCode SocketAcceptFailed{24};

}

#endif // NETWORK_ERROR_CODES_H
