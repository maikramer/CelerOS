#ifndef AUTH_ERROR_CODES_H
#define AUTH_ERROR_CODES_H

#include "ErrorCode.h"

/**
 * @file AuthErrorCodes.h
 * @brief Error codes specific to the authentication system.
 */

namespace AuthErrorCodes {

    // ========== General Auth Errors ==========
    
    /** @brief No error, operation successful */
    extern const ErrorCodeItem None;
    
    /** @brief Authentication system not initialized */
    extern const ErrorCodeItem NotInitialized;
    
    /** @brief Invalid configuration provided */
    extern const ErrorCodeItem InvalidConfig;
    
    /** @brief Operation cancelled by user */
    extern const ErrorCodeItem Cancelled;
    
    /** @brief Operation timed out */
    extern const ErrorCodeItem Timeout;
    
    // ========== Login Errors ==========
    
    /** @brief User not found in the system */
    extern const ErrorCodeItem UserNotFound;
    
    /** @brief Incorrect password provided */
    extern const ErrorCodeItem WrongPassword;
    
    /** @brief Account is not confirmed/verified */
    extern const ErrorCodeItem AccountNotConfirmed;
    
    /** @brief Account is disabled/inactive */
    extern const ErrorCodeItem AccountDisabled;
    
    /** @brief Account is locked due to too many failed attempts */
    extern const ErrorCodeItem AccountLocked;
    
    /** @brief User is already logged in */
    extern const ErrorCodeItem AlreadyLoggedIn;
    
    /** @brief No active session found */
    extern const ErrorCodeItem NotLoggedIn;
    
    // ========== Signup Errors ==========
    
    /** @brief Email address already registered */
    extern const ErrorCodeItem EmailAlreadyExists;
    
    /** @brief Invalid email format */
    extern const ErrorCodeItem InvalidEmail;
    
    /** @brief Password does not meet requirements */
    extern const ErrorCodeItem WeakPassword;
    
    /** @brief Password is too short */
    extern const ErrorCodeItem PasswordTooShort;
    
    /** @brief Password is too long */
    extern const ErrorCodeItem PasswordTooLong;
    
    /** @brief Display name is invalid */
    extern const ErrorCodeItem InvalidDisplayName;
    
    /** @brief User registration failed */
    extern const ErrorCodeItem SignupFailed;
    
    // ========== Token Errors ==========
    
    /** @brief Access token is invalid */
    extern const ErrorCodeItem InvalidAccessToken;
    
    /** @brief Access token has expired */
    extern const ErrorCodeItem AccessTokenExpired;
    
    /** @brief Refresh token is invalid */
    extern const ErrorCodeItem InvalidRefreshToken;
    
    /** @brief Refresh token has expired */
    extern const ErrorCodeItem RefreshTokenExpired;
    
    /** @brief Failed to refresh token */
    extern const ErrorCodeItem TokenRefreshFailed;
    
    // ========== Permission Errors ==========
    
    /** @brief User does not have required permissions */
    extern const ErrorCodeItem PermissionDenied;
    
    /** @brief Admin privileges required */
    extern const ErrorCodeItem AdminRequired;
    
    /** @brief Only admin can approve new users */
    extern const ErrorCodeItem ApprovalRequired;
    
    // ========== Session Errors ==========
    
    /** @brief Session is invalid */
    extern const ErrorCodeItem InvalidSession;
    
    /** @brief Session has expired */
    extern const ErrorCodeItem SessionExpired;
    
    /** @brief Maximum concurrent sessions reached */
    extern const ErrorCodeItem MaxSessionsReached;
    
    /** @brief Session not found */
    extern const ErrorCodeItem SessionNotFound;
    
    // ========== Cloud/Network Errors ==========
    
    /** @brief No network connection available */
    extern const ErrorCodeItem NoNetwork;
    
    /** @brief Cloud service unavailable */
    extern const ErrorCodeItem CloudUnavailable;
    
    /** @brief Cloud request failed */
    extern const ErrorCodeItem CloudRequestFailed;
    
    /** @brief Invalid response from cloud */
    extern const ErrorCodeItem InvalidCloudResponse;
    
    /** @brief Supabase configuration missing */
    extern const ErrorCodeItem SupabaseNotConfigured;
    
    // ========== Sync Errors ==========
    
    /** @brief Sync operation failed */
    extern const ErrorCodeItem SyncFailed;
    
    /** @brief Sync conflict detected */
    extern const ErrorCodeItem SyncConflict;
    
    /** @brief Sync already in progress */
    extern const ErrorCodeItem SyncInProgress;
    
    // ========== Storage Errors ==========
    
    /** @brief Failed to save to local storage */
    extern const ErrorCodeItem StorageSaveFailed;
    
    /** @brief Failed to load from local storage */
    extern const ErrorCodeItem StorageLoadFailed;
    
    /** @brief Local storage is full */
    extern const ErrorCodeItem StorageFull;
    
    // ========== Validation Errors ==========
    
    /** @brief Invalid credentials format */
    extern const ErrorCodeItem InvalidCredentials;
    
    /** @brief Missing required field */
    extern const ErrorCodeItem MissingRequiredField;
    
    /** @brief Invalid user ID format */
    extern const ErrorCodeItem InvalidUserId;

    /**
     * @brief Register all auth error codes with the ErrorCode system.
     * Call this during initialization.
     */
    void registerAll();

} // namespace AuthErrorCodes

#endif // AUTH_ERROR_CODES_H
