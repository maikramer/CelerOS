#ifndef GENERAL_ERROR_CODES_H
#define GENERAL_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file GeneralErrorCodes.h
 * @brief Codigos de erro gerais (ids 1..12 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros gerais
inline constexpr ErrorCode Invalid{1};
inline constexpr ErrorCode None{2};
inline constexpr ErrorCode UnknownError{3};
inline constexpr ErrorCode OperationFailed{4};
inline constexpr ErrorCode NotImplemented{5};
inline constexpr ErrorCode NotInitialized{6};
inline constexpr ErrorCode ArgumentError{7};
inline constexpr ErrorCode Timeout{8};
inline constexpr ErrorCode CommandAlreadyRegistered{9};
inline constexpr ErrorCode InvalidCommand{10};
inline constexpr ErrorCode ConnectionClosed{11};
inline constexpr ErrorCode ListIsEmpty{12};

}

#endif // GENERAL_ERROR_CODES_H
