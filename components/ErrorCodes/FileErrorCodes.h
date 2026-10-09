#ifndef FILE_ERROR_CODES_H
#define FILE_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file FileErrorCodes.h
 * @brief Codigos de erro do sistema de arquivos (ids 36..41 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros de arquivo
inline constexpr ErrorCode FileNotFound{36};
inline constexpr ErrorCode FileOpenError{37};
inline constexpr ErrorCode FileReadError{38};
inline constexpr ErrorCode FileWriteError{39};
inline constexpr ErrorCode FileExists{40};
inline constexpr ErrorCode FileIsEmpty{41};

}

#endif // FILE_ERROR_CODES_H
