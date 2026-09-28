#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <memory>
#include <map>
#include <vector>
#include "AuthTypes.h"
#include "AuthErrorCodes.h"
#include "IAuthProvider.h"
#include "LocalAuthProvider.h"
#include "CloudAuthProvider.h"
#include "UserSession.h"
#include "Event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/**
 * @file AuthManager.h
 * @brief Central authentication manager orchestrating local and cloud auth.
 * 
 * This singleton class provides a unified API for authentication operations,
 * managing multiple user sessions and synchronization between providers.
 */

/**
 * @class AuthManager
 * @brief Singleton authentication manager.
 * 
 * Orchestrates authentication between local (NVS) and cloud (Supabase) providers,
 * manages multiple concurrent sessions, handles token refresh, and provides
 * synchronization between local and cloud data.
 * 
 * Example:
 * @code
 * // Initialize
 * AuthManager::instance().init(AuthProvider::Both);
 * AuthManager::instance().setSupabaseConfig(SUPABASE_URL, SUPABASE_ANON_KEY);
 * 
 * // Register event handlers
 * AuthManager::instance().onLogin.addHandler([](UserSession* session) {
 *     ESP_LOGI("App", "User logged in: %s", session->getEmail().c_str());
 * });
 * 
 * // Login
 * AuthManager::instance().login("user@email.com", "password123");
 * 
 * // Check status
 * if (AuthManager::instance().isLoggedIn()) {
 *     auto* session = AuthManager::instance().getCurrentSession();
 *     // Use session...
 * }
 * @endcode
 */
class AuthManager {
public:
    /**
     * @brief Get singleton instance.
     */
    static AuthManager& instance();

    // Prevent copying
    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;

    // ========== Initialization ==========

    /**
     * @brief Initialize the auth manager.
     * @param provider Which providers to use (Local, Cloud, or Both).
     * @return Error code.
     */
    ErrorCode init(AuthProvider provider = AuthProvider::Both);

    /**
     * @brief Initialize with full configuration.
     * @param config Authentication configuration.
     * @return Error code.
     */
    ErrorCode init(const AuthConfig& config);

    /**
     * @brief Check if manager is initialized.
     */
    bool isInitialized() const { return _initialized; }

    /**
     * @brief Set Supabase configuration.
     * @param url Supabase project URL.
     * @param anonKey Anonymous key.
     */
    void setSupabaseConfig(const std::string& url, const std::string& anonKey);

    /**
     * @brief Set Supabase configuration.
     * @param config Full Supabase config.
     */
    void setSupabaseConfig(const SupabaseConfig& config);

    /**
     * @brief Get current configuration.
     */
    const AuthConfig& getConfig() const { return _config; }

    // ========== Authentication ==========

    /**
     * @brief Login with email and password.
     * @param email User email.
     * @param password User password.
     * @param preferCloud Prefer cloud auth if both providers enabled.
     */
    void login(const std::string& email, const std::string& password, 
               bool preferCloud = true);

    /**
     * @brief Login with credentials struct.
     * @param credentials User credentials.
     */
    void login(const AuthCredentials& credentials);

    /**
     * @brief Login synchronously (blocking).
     * @param credentials User credentials.
     * @return AuthResult.
     */
    AuthResult loginSync(const AuthCredentials& credentials);

    /**
     * @brief Register a new user.
     * @param email User email.
     * @param password User password.
     * @param displayName Display name (optional).
     */
    void signup(const std::string& email, const std::string& password,
                const std::string& displayName = "");

    /**
     * @brief Register with full credentials and profile.
     * @param credentials User credentials.
     * @param profile Optional profile data.
     */
    void signup(const AuthCredentials& credentials, const UserInfo& profile = UserInfo());

    /**
     * @brief Register synchronously (blocking).
     * @param credentials User credentials.
     * @param profile Optional profile data.
     * @return AuthResult.
     */
    AuthResult signupSync(const AuthCredentials& credentials, 
                          const UserInfo& profile = UserInfo());

    /**
     * @brief Logout current user.
     */
    void logout();

    /**
     * @brief Logout specific user by ID.
     * @param userId User ID to logout.
     */
    void logout(const std::string& userId);

    /**
     * @brief Logout all users.
     */
    void logoutAll();

    // ========== Sessions ==========

    /**
     * @brief Get current (most recent) session.
     * @return Pointer to current session, or nullptr if not logged in.
     */
    UserSession* getCurrentSession();

    /**
     * @brief Get session by user ID.
     * @param userId User ID.
     * @return Pointer to session, or nullptr if not found.
     */
    UserSession* getSession(const std::string& userId);

    /**
     * @brief Get all active sessions.
     * @return Vector of session pointers.
     */
    std::vector<UserSession*> getActiveSessions();

    /**
     * @brief Get number of active sessions.
     */
    size_t getActiveSessionCount() const;

    /**
     * @brief Set a session as current (most recent).
     * @param userId User ID of session to make current.
     */
    void setCurrentSession(const std::string& userId);

    // ========== State ==========

    /**
     * @brief Get current authentication state.
     */
    AuthState getState() const;

    /**
     * @brief Check if any user is logged in.
     */
    bool isLoggedIn() const;

    /**
     * @brief Check if a specific user is logged in.
     * @param userId User ID.
     */
    bool isLoggedIn(const std::string& userId) const;

    /**
     * @brief Get active provider type.
     */
    AuthProvider getActiveProvider() const { return _config.defaultProvider; }

    // ========== User Management ==========

    /**
     * @brief Get all users (from active provider).
     * @param users Output vector.
     * @return Error code.
     */
    ErrorCode getAllUsers(std::vector<UserInfo>& users);

    /**
     * @brief Get users pending confirmation.
     * @param users Output vector.
     * @return Error code.
     */
    ErrorCode getPendingUsers(std::vector<UserInfo>& users);

    /**
     * @brief Confirm a user account.
     * @param userId User ID.
     * @return Error code.
     */
    ErrorCode confirmUser(const std::string& userId);

    /**
     * @brief Set user role.
     * @param userId User ID.
     * @param role New role.
     * @return Error code.
     */
    ErrorCode setUserRole(const std::string& userId, UserRole role);

    /**
     * @brief Enable or disable user.
     * @param userId User ID.
     * @param active True to enable.
     * @return Error code.
     */
    ErrorCode setUserActive(const std::string& userId, bool active);

    /**
     * @brief Delete user account.
     * @param userId User ID.
     * @return Error code.
     */
    ErrorCode deleteUser(const std::string& userId);

    /**
     * @brief Change user password.
     * @param oldPassword Current password.
     * @param newPassword New password.
     * @return Error code.
     */
    ErrorCode changePassword(const std::string& oldPassword, 
                             const std::string& newPassword);

    /**
     * @brief Request password reset email (cloud only).
     * @param email User email.
     * @return Error code.
     */
    ErrorCode requestPasswordReset(const std::string& email);

    // ========== Synchronization ==========

    /**
     * @brief Manually trigger sync with cloud.
     */
    void syncWithCloud();

    /**
     * @brief Enable/disable auto sync.
     * @param enabled Enable auto sync.
     * @param intervalSec Sync interval in seconds.
     */
    void setAutoSync(bool enabled, uint32_t intervalSec = 300);

    /**
     * @brief Get sync status.
     */
    const SyncStatus& getSyncStatus() const { return _syncStatus; }

    // ========== Settings ==========

    /**
     * @brief Enable/disable auto token refresh.
     */
    void setAutoRefresh(bool enabled);

    /**
     * @brief Set maximum concurrent sessions.
     */
    void setMaxSessions(uint32_t max);

    /**
     * @brief Set session persistence.
     */
    void setPersistSessions(bool persist);

    // ========== Events ==========

    /**
     * @brief Event triggered on successful login.
     * Parameter: UserSession*
     */
    Event<UserSession*> onLogin;

    /**
     * @brief Event triggered on logout.
     * Parameter: UserSession* (session being logged out)
     */
    Event<UserSession*> onLogout;

    /**
     * @brief Event triggered on login failure.
     * Parameters: credentials email, ErrorCode
     */
    Event<const std::string&, ErrorCode> onLoginFailed;

    /**
     * @brief Event triggered on signup success.
     * Parameter: UserSession*
     */
    Event<UserSession*> onSignup;

    /**
     * @brief Event triggered on signup failure.
     * Parameters: credentials email, ErrorCode
     */
    Event<const std::string&, ErrorCode> onSignupFailed;

    /**
     * @brief Event triggered when session expires.
     * Parameter: UserSession*
     */
    Event<UserSession*> onSessionExpired;

    /**
     * @brief Event triggered when tokens are refreshed.
     * Parameter: UserSession*
     */
    Event<UserSession*> onTokenRefreshed;

    /**
     * @brief Event triggered when state changes.
     * Parameters: old state, new state
     */
    Event<AuthState, AuthState> onStateChanged;

    /**
     * @brief Event triggered when sync completes.
     * Parameter: SyncStatus
     */
    Event<const SyncStatus&> onSyncComplete;

    /**
     * @brief Event triggered when sync fails.
     * Parameter: ErrorCode
     */
    Event<ErrorCode> onSyncFailed;

private:
    AuthManager();
    ~AuthManager();

    /**
     * @brief Set current state and trigger event.
     */
    void setState(AuthState newState);

    /**
     * @brief Get appropriate provider for operation.
     * @param preferCloud Prefer cloud if both enabled.
     */
    IAuthProvider* getProvider(bool preferCloud = true);

    /**
     * @brief Create and store new session.
     */
    UserSession* createSession(const AuthSession& authSession);

    /**
     * @brief Remove and cleanup session.
     */
    void removeSession(const std::string& userId);

    /**
     * @brief Handle auth result from provider.
     */
    void handleAuthResult(const AuthResult& result, bool isSignup = false);

    /**
     * @brief Load persisted sessions from NVS.
     */
    ErrorCode loadPersistedSessions();

    /**
     * @brief Save sessions to NVS.
     */
    ErrorCode persistSessions();

    /**
     * @brief Start background tasks (sync, refresh).
     */
    void startBackgroundTasks();

    /**
     * @brief Stop background tasks.
     */
    void stopBackgroundTasks();

    /**
     * @brief Sync task function.
     */
    static void syncTaskFunc(void* pvParameters);

    /**
     * @brief Token refresh task function.
     */
    static void refreshTaskFunc(void* pvParameters);

    // Providers
#if AUTH_LOCAL_ENABLED
    std::unique_ptr<LocalAuthProvider> _localProvider;
#endif
#if AUTH_CLOUD_ENABLED
    std::unique_ptr<CloudAuthProvider> _cloudProvider;
#endif

    // Sessions: userId -> UserSession
    std::map<std::string, std::unique_ptr<UserSession>> _sessions;
    std::string _currentSessionId;

    // Configuration
    AuthConfig _config;
    SupabaseConfig _supabaseConfig;

    // State
    AuthState _state;
    SyncStatus _syncStatus;
    bool _initialized;

    // Thread safety
    SemaphoreHandle_t _mutex;

    // Background tasks
    TaskHandle_t _syncTaskHandle;
    TaskHandle_t _refreshTaskHandle;
    bool _syncEnabled;
    uint32_t _syncIntervalSec;

    static constexpr const char* TAG = "AuthManager";
};

#endif // AUTH_MANAGER_H
