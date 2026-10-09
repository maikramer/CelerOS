#ifndef STORAGE_ERROR_CODES_H
#define STORAGE_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file StorageErrorCodes.h
 * @brief Codigos de erro de armazenamento (ids 42..46 em ERROR_CODES.md).
 */

namespace CommonErrorCodes {

// Erros de armazenamento (SD, flash, etc.)
inline constexpr ErrorCode StorageInitFailed{42};
inline constexpr ErrorCode StorageReadError{43};
inline constexpr ErrorCode StorageWriteError{44};
inline constexpr ErrorCode StorageNotMounted{45};
inline constexpr ErrorCode StorageFull{46};

}

#endif // STORAGE_ERROR_CODES_H
