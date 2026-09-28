#ifndef LOCAL_AUTH_PROVIDER_H
#define LOCAL_AUTH_PROVIDER_H

#include "IAuthProvider.h"
#include <map>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @file LocalAuthProvider.h
 * @brief Authentication provider using local NVS storage.
 * 
 * This provider stores user credentials and profiles in the ESP32's
 * Non-Volatile Storage (NVS). It supports offline authentication
 * and can be used standalone or in combination with a cloud provider.
 */

#if AUTH_LOCAL_ENABLED

/**
 * @class LocalAuthProvider
 * @brief Local authentication provider using NVS storage.
 * 
 * Features:
 * - Stores users in NVS flash
 * - Password hashing with SHA-256
 * - Offline authentication
 * - Persistent sessions
 * 
 * Example:
 * @code
 * LocalAuthProvider localAuth;
 * localAuth.init();
 * 
 * AuthCredentials creds("user@email.com", "password123");
 * AuthResult result = localAuth.login(creds);
 * 
 * if (result.isSuccess()) {
 *     ESP_LOGI("Auth", "Logged in as %s", result.session.user.email.c_str());
 * }
 * @endcode
 */
class LocalAuthProvider : public IAuthProvider {
public:
    LocalAuthProvider();
    ~LocalAuthProvider() override;

    // ========== IAuthProvider Interface ==========

    ErrorCode init() override;
    bool isReady() const override;
    AuthProvider getType() const override { return AuthProvider::Local; }

    AuthResult login(const AuthCredentials& credentials) override;
    AuthResult signup(const AuthCredentials& credentials, 
                      const UserInfo& profile = UserInfo()) override;
    ErrorCode logout(const std::string& userId = "") override;

    ErrorCode getProfile(const std::string& userId, UserInfo& profile) override;
    ErrorCode updateProfile(const std::string& userId, const UserInfo& profile) override;
    ErrorCode deleteUser(const std::string& userId) override;
    bool userExists(const std::string& email) override;

    ErrorCode changePassword(const std::string& userId,
                             const std::string& oldPassword,
                             const std::string& newPassword) override;

    ErrorCode getAllUsers(std::vector<UserInfo>& users) override;
    ErrorCode getPendingUsers(std::vector<UserInfo>& users) override;
    ErrorCode confirmUser(const std::string& userId) override;
    ErrorCode setUserRole(const std::string& userId, UserRole role) override;
    ErrorCode setUserActive(const std::string& userId, bool active) override;

    // ========== Local-Specific Methods ==========

    /**
     * @brief Check if any admin user exists.
     * @return True if at least one admin is registered.
     */
    bool hasAdmin() const;

    /**
     * @brief Get the number of registered users.
     */
    size_t getUserCount() const;

    /**
     * @brief Clear all stored users (use with caution!).
     * @return Error code.
     */
    ErrorCode clearAllUsers();

    /**
     * @brief Export all users for sync.
     * @param users Output map of userId -> UserInfo.
     * @return Error code.
     */
    ErrorCode exportUsers(std::map<std::string, UserInfo>& users);

    /**
     * @brief Import users from sync.
     * @param users Map of userId -> UserInfo to import.
     * @param overwrite True to overwrite existing users.
     * @return Error code.
     */
    ErrorCode importUsers(const std::map<std::string, UserInfo>& users, 
                          bool overwrite = false);

    /**
     * @brief Get user by email.
     * @param email User email.
     * @param user Output parameter.
     * @return Error code.
     */
    ErrorCode getUserByEmail(const std::string& email, UserInfo& user);

    /**
     * @brief Verify a password against stored hash.
     * @param email User email.
     * @param password Password to verify.
     * @return True if password is correct.
     */
    bool verifyPassword(const std::string& email, const std::string& password);

private:
    /**
     * @brief Load all users from NVS into memory cache.
     */
    ErrorCode loadUsersFromNvs();

    /**
     * @brief Save all users from cache to NVS.
     */
    ErrorCode saveUsersToNvs();

    /**
     * @brief Save a single user to NVS.
     */
    ErrorCode saveUserToNvs(const UserInfo& user);

    /**
     * @brief Remove a user from NVS.
     */
    ErrorCode removeUserFromNvs(const std::string& userId);

    /**
     * @brief Hash a password using SHA-256.
     * @param password Plain text password.
     * @param salt Optional salt (if empty, generates new salt).
     * @return Hashed password string.
     */
    std::string hashPassword(const std::string& password, const std::string& salt = "");

    /**
     * @brief Generate a salt for password hashing.
     */
    std::string generateSalt();

    /**
     * @brief Generate a unique user ID.
     */
    std::string generateUserId();

    /**
     * @brief Validate email format.
     */
    bool isValidEmail(const std::string& email) const;

    /**
     * @brief Validate password requirements.
     */
    ErrorCode validatePassword(const std::string& password) const;

    /**
     * @brief Get current timestamp in seconds.
     */
    uint64_t getCurrentTimestamp() const;

    /**
     * @brief Create a session for a user.
     */
    AuthSession createSession(const UserInfo& user);

    /**
     * @brief Find user by email in cache.
     * @return Pointer to user or nullptr if not found.
     */
    UserInfo* findUserByEmail(const std::string& email);

    /**
     * @brief Find user by ID in cache.
     * @return Pointer to user or nullptr if not found.
     */
    UserInfo* findUserById(const std::string& userId);

    // User cache: userId -> UserInfo
    std::map<std::string, UserInfo> _users;
    
    // Email to userId mapping for fast lookup
    std::map<std::string, std::string> _emailToId;

    // Mutex for thread safety
    SemaphoreHandle_t _mutex;

    // Track failed login attempts: email -> count
    std::map<std::string, uint32_t> _failedAttempts;
    std::map<std::string, uint64_t> _lockoutUntil;

    static constexpr const char* TAG = "LocalAuth";
};

#endif // AUTH_LOCAL_ENABLED

#endif // LOCAL_AUTH_PROVIDER_H
