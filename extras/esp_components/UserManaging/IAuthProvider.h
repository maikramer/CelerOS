#ifndef I_AUTH_PROVIDER_H
#define I_AUTH_PROVIDER_H

#include <string>
#include <functional>
#include "AuthTypes.h"
#include "AuthErrorCodes.h"
#include "Event.h"

/**
 * @file IAuthProvider.h
 * @brief Interface base for authentication providers.
 * 
 * This abstract class defines the contract that all authentication
 * providers (local, cloud, etc.) must implement.
 */

/**
 * @struct AuthResult
 * @brief Result of an authentication operation.
 */
struct AuthResult {
    ErrorCode error;            /**< Error code (None if successful) */
    AuthSession session;        /**< Session data (if successful) */
    bool needsConfirmation;     /**< Account needs admin confirmation */
    std::string message;        /**< Optional message */

    AuthResult()
        : error(AuthErrorCodes::None)
        , needsConfirmation(false) {}

    AuthResult(const ErrorCode& err)
        : error(err)
        , needsConfirmation(false) {}

    AuthResult(const AuthSession& sess)
        : error(AuthErrorCodes::None)
        , session(sess)
        , needsConfirmation(false) {}

    /**
     * @brief Check if operation was successful.
     */
    bool isSuccess() const {
        return error == AuthErrorCodes::None;
    }

    /**
     * @brief Check if operation failed.
     */
    bool isError() const {
        return error != AuthErrorCodes::None;
    }
};

/**
 * @typedef AuthCallback
 * @brief Callback function for async authentication operations.
 */
using AuthCallback = std::function<void(const AuthResult&)>;

/**
 * @class IAuthProvider
 * @brief Abstract base class for authentication providers.
 * 
 * Defines the interface for local and cloud authentication providers.
 * Each provider implements these methods according to their storage/API.
 */
class IAuthProvider {
public:
    virtual ~IAuthProvider() = default;

    // ========== Lifecycle ==========

    /**
     * @brief Initialize the provider.
     * @return Error code.
     */
    virtual ErrorCode init() = 0;

    /**
     * @brief Check if provider is initialized and ready.
     */
    virtual bool isReady() const = 0;

    /**
     * @brief Get the provider type.
     */
    virtual AuthProvider getType() const = 0;

    // ========== Authentication ==========

    /**
     * @brief Authenticate a user with email and password.
     * @param credentials User credentials.
     * @return AuthResult with session if successful.
     */
    virtual AuthResult login(const AuthCredentials& credentials) = 0;

    /**
     * @brief Authenticate asynchronously.
     * @param credentials User credentials.
     * @param callback Callback function called with result.
     */
    virtual void loginAsync(const AuthCredentials& credentials, AuthCallback callback) {
        // Default implementation: call sync version
        auto result = login(credentials);
        if (callback) {
            callback(result);
        }
    }

    /**
     * @brief Register a new user.
     * @param credentials User credentials (email, password, displayName).
     * @param profile Optional additional profile data.
     * @return AuthResult with session if successful.
     */
    virtual AuthResult signup(const AuthCredentials& credentials, 
                              const UserInfo& profile = UserInfo()) = 0;

    /**
     * @brief Register a new user asynchronously.
     */
    virtual void signupAsync(const AuthCredentials& credentials,
                             const UserInfo& profile,
                             AuthCallback callback) {
        auto result = signup(credentials, profile);
        if (callback) {
            callback(result);
        }
    }

    /**
     * @brief End a user session.
     * @param userId User ID to logout (empty for current user).
     * @return Error code.
     */
    virtual ErrorCode logout(const std::string& userId = "") = 0;

    // ========== Token Management (primarily for cloud providers) ==========

    /**
     * @brief Refresh an access token using a refresh token.
     * @param refreshToken The refresh token.
     * @return AuthResult with new tokens.
     */
    virtual AuthResult refreshToken(const std::string& refreshToken) {
        // Default: not supported
        return AuthResult(AuthErrorCodes::InvalidRefreshToken);
    }

    /**
     * @brief Validate an access token.
     * @param accessToken The access token to validate.
     * @return True if token is valid.
     */
    virtual bool validateToken(const std::string& accessToken) {
        // Default: not supported, assume valid
        return true;
    }

    // ========== User Profile ==========

    /**
     * @brief Get user profile by ID.
     * @param userId User ID.
     * @param profile Output parameter for user info.
     * @return Error code.
     */
    virtual ErrorCode getProfile(const std::string& userId, UserInfo& profile) = 0;

    /**
     * @brief Update user profile.
     * @param userId User ID.
     * @param profile Updated profile data.
     * @return Error code.
     */
    virtual ErrorCode updateProfile(const std::string& userId, const UserInfo& profile) = 0;

    /**
     * @brief Delete a user account.
     * @param userId User ID.
     * @return Error code.
     */
    virtual ErrorCode deleteUser(const std::string& userId) = 0;

    /**
     * @brief Check if a user exists.
     * @param email User email.
     * @return True if user exists.
     */
    virtual bool userExists(const std::string& email) = 0;

    // ========== Password Management ==========

    /**
     * @brief Change user password.
     * @param userId User ID.
     * @param oldPassword Current password.
     * @param newPassword New password.
     * @return Error code.
     */
    virtual ErrorCode changePassword(const std::string& userId,
                                     const std::string& oldPassword,
                                     const std::string& newPassword) = 0;

    /**
     * @brief Request password reset (cloud only).
     * @param email User email.
     * @return Error code.
     */
    virtual ErrorCode requestPasswordReset(const std::string& email) {
        // Default: not supported
        return AuthErrorCodes::PermissionDenied;
    }

    // ========== Admin Functions ==========

    /**
     * @brief Get all users (admin only).
     * @param users Output vector of user info.
     * @return Error code.
     */
    virtual ErrorCode getAllUsers(std::vector<UserInfo>& users) = 0;

    /**
     * @brief Get users pending confirmation.
     * @param users Output vector of user info.
     * @return Error code.
     */
    virtual ErrorCode getPendingUsers(std::vector<UserInfo>& users) = 0;

    /**
     * @brief Confirm/approve a user account.
     * @param userId User ID to confirm.
     * @return Error code.
     */
    virtual ErrorCode confirmUser(const std::string& userId) = 0;

    /**
     * @brief Set user role.
     * @param userId User ID.
     * @param role New role.
     * @return Error code.
     */
    virtual ErrorCode setUserRole(const std::string& userId, UserRole role) = 0;

    /**
     * @brief Enable or disable a user account.
     * @param userId User ID.
     * @param active True to enable, false to disable.
     * @return Error code.
     */
    virtual ErrorCode setUserActive(const std::string& userId, bool active) = 0;

    // ========== Events ==========

    /**
     * @brief Event triggered when authentication completes.
     * Parameters: AuthResult
     */
    Event<const AuthResult&> onAuthComplete;

    /**
     * @brief Event triggered when authentication fails.
     * Parameters: ErrorCode
     */
    Event<ErrorCode> onAuthError;

    /**
     * @brief Event triggered when user profile is updated.
     * Parameters: userId, UserInfo
     */
    Event<const std::string&, const UserInfo&> onProfileUpdated;

protected:
    bool _initialized = false;
};

#endif // I_AUTH_PROVIDER_H
