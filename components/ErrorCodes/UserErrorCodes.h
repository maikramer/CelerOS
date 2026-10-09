#ifndef USER_ERROR_CODES_H
#define USER_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file UserErrorCodes.h
 * @brief Codigos de erro de usuario (ids 47..49 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros de usuario/autenticacao
inline constexpr ErrorCode AuthenticationFailed{47};
inline constexpr ErrorCode UserNotFound{48};
inline constexpr ErrorCode UserAlreadyExists{49};

}

#endif // USER_ERROR_CODES_H
