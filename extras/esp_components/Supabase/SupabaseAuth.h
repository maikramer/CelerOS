#ifndef SUPABASE_AUTH_H
#define SUPABASE_AUTH_H

#include <string>
#include <map>
#include <functional>
#include "SupabaseTypes.h"
#include "HttpClient.h"
#include "ErrorCode.h"
#include "Event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/**
 * @file SupabaseAuth.h
 * @brief Supabase Authentication module for ESP32.
 * 
 * Provides complete Supabase Auth API integration:
 * - Email/password authentication
 * - JWT token management with auto-refresh
 * - User profile management
 * - Password reset flow
 * 
 * @see https://supabase.com/docs/guides/auth
 */

namespace Supabase {

// ============================================================================
// Auth Types
// ============================================================================

/**
 * @struct AuthUser
 * @brief User information from Supabase Auth.
 */
struct AuthUser {
    std::string id;                 ///< UUID from auth.users
    std::string email;              ///< User email
    std::string phone;              ///< User phone (if set)
    bool emailConfirmed;            ///< Email verification status
    bool phoneConfirmed;            ///< Phone verification status
    uint64_t createdAt;             ///< Account creation timestamp
    uint64_t lastSignInAt;          ///< Last sign in timestamp
    std::string role;               ///< User role (from auth)
    std::map<std::string, std::string> userMetadata;  ///< Custom user metadata
    std::map<std::string, std::string> appMetadata;   ///< App-level metadata

    AuthUser()
        : emailConfirmed(false)
        , phoneConfirmed(false)
        , createdAt(0)
        , lastSignInAt(0) {}

    bool isValid() const {
        return !id.empty() && !email.empty();
    }
};

/**
 * @struct AuthTokens
 * @brief JWT tokens from Supabase Auth.
 */
struct AuthTokens {
    std::string accessToken;        ///< JWT access token
    std::string refreshToken;       ///< Refresh token for getting new access tokens
    std::string tokenType;          ///< Token type (Bearer)
    uint32_t expiresIn;             ///< Access token lifetime in seconds
    uint64_t expiresAt;             ///< Absolute expiration timestamp

    AuthTokens()
        : tokenType("Bearer")
        , expiresIn(0)
        , expiresAt(0) {}

    bool isValid() const {
        return !accessToken.empty() && !refreshToken.empty();
    }

    bool isExpired(uint32_t bufferSec = 60) const {
        if (expiresAt == 0) return true;
        return (static_cast<uint64_t>(time(nullptr)) + bufferSec) >= expiresAt;
    }
};

/**
 * @struct AuthSession
 * @brief Complete authentication session.
 */
struct AuthSession {
    AuthUser user;                  ///< User information
    AuthTokens tokens;              ///< JWT tokens
    std::string providerToken;      ///< OAuth provider token (if applicable)
    std::string providerRefreshToken;

    AuthSession() = default;

    bool isValid() const {
        return user.isValid() && tokens.isValid();
    }
};

/**
 * @struct AuthResponse
 * @brief Response from authentication operations.
 */
struct AuthResponse {
    bool success;                   ///< Operation succeeded
    AuthSession session;            ///< Session data (on success)
    std::string errorCode;          ///< Error code from Supabase
    std::string errorMessage;       ///< Human-readable error message
    int httpStatus;                 ///< HTTP status code

    AuthResponse()
        : success(false)
        , httpStatus(0) {}

    bool isOk() const { return success && httpStatus >= 200 && httpStatus < 300; }
    bool isUnauthorized() const { return httpStatus == 401; }
    bool isForbidden() const { return httpStatus == 403; }
    bool isNotFound() const { return httpStatus == 404; }
    bool isConflict() const { return httpStatus == 409; } // User already exists
    bool isRateLimited() const { return httpStatus == 429; }
};

/**
 * @struct SignupOptions
 * @brief Options for user signup.
 */
struct SignupOptions {
    std::map<std::string, std::string> userData;  ///< Custom user metadata
    std::string redirectTo;          ///< Email confirmation redirect URL
    bool emailConfirmRequired;       ///< Whether email confirmation is required

    SignupOptions() : emailConfirmRequired(true) {}
};

// ============================================================================
// SupabaseAuth Class
// ============================================================================

/**
 * @class SupabaseAuth
 * @brief Supabase authentication handler.
 * 
 * Usage:
 * @code
 * auto& auth = SupabaseAuth::instance();
 * auth.init("https://xxx.supabase.co", "anon-key");
 * 
 * // Sign up
 * auto resp = auth.signUp("user@email.com", "password123");
 * if (resp.isOk()) {
 *     ESP_LOGI("Auth", "User created: %s", resp.session.user.id.c_str());
 * }
 * 
 * // Login
 * auto resp = auth.signInWithPassword("user@email.com", "password123");
 * if (resp.isOk()) {
 *     // Use resp.session.tokens.accessToken for authenticated requests
 * }
 * 
 * // Logout
 * auth.signOut();
 * @endcode
 */
class SupabaseAuth {
public:
    /**
     * @brief Get singleton instance.
     */
    static SupabaseAuth& instance();

    // Prevent copying
    SupabaseAuth(const SupabaseAuth&) = delete;
    SupabaseAuth& operator=(const SupabaseAuth&) = delete;

    // ========== Initialization ==========

    /**
     * @brief Initialize with Supabase configuration.
     * @param config Supabase configuration.
     * @return Error code.
     */
    ErrorCode init(const SupabaseConfig& config);

    /**
     * @brief Initialize with URL and API key.
     * @param url Supabase project URL.
     * @param anonKey Anonymous API key.
     * @return Error code.
     */
    ErrorCode init(const std::string& url, const std::string& anonKey);

    /**
     * @brief Check if initialized.
     */
    bool isInitialized() const { return _initialized; }

    /**
     * @brief Check if network is available.
     */
    bool isNetworkAvailable() const;

    /**
     * @brief Get current configuration.
     */
    const SupabaseConfig& getConfig() const { return _config; }

    // ========== Authentication ==========

    /**
     * @brief Sign up with email and password.
     * @param email User email.
     * @param password User password.
     * @param options Signup options (metadata, etc.).
     * @return Authentication response.
     */
    AuthResponse signUp(const std::string& email, 
                        const std::string& password,
                        const SignupOptions& options = SignupOptions());

    /**
     * @brief Sign in with email and password.
     * @param email User email.
     * @param password User password.
     * @return Authentication response.
     */
    AuthResponse signInWithPassword(const std::string& email, 
                                    const std::string& password);

    /**
     * @brief Sign out current user.
     * @param accessToken Access token to invalidate (optional, uses current).
     * @return Error code.
     */
    ErrorCode signOut(const std::string& accessToken = "");

    /**
     * @brief Refresh session using refresh token.
     * @param refreshToken Refresh token.
     * @return Authentication response with new tokens.
     */
    AuthResponse refreshSession(const std::string& refreshToken);

    // ========== User Management ==========

    /**
     * @brief Get current user information.
     * @param accessToken Access token.
     * @return Authentication response with user info.
     */
    AuthResponse getUser(const std::string& accessToken);

    /**
     * @brief Update user information.
     * @param accessToken Access token.
     * @param email New email (optional).
     * @param password New password (optional).
     * @param userData User metadata to update.
     * @return Authentication response.
     */
    AuthResponse updateUser(const std::string& accessToken,
                            const std::string& email = "",
                            const std::string& password = "",
                            const std::map<std::string, std::string>& userData = {});

    /**
     * @brief Request password reset email.
     * @param email User email.
     * @param redirectTo Redirect URL after reset.
     * @return Error code.
     */
    ErrorCode resetPasswordForEmail(const std::string& email,
                                    const std::string& redirectTo = "");

    /**
     * @brief Verify OTP token.
     * @param email User email.
     * @param token OTP token.
     * @param type Token type (signup, recovery, etc.).
     * @return Authentication response.
     */
    AuthResponse verifyOtp(const std::string& email,
                           const std::string& token,
                           const std::string& type = "signup");

    /**
     * @brief Resend signup confirmation email.
     * @param email User email.
     * @return Error code.
     */
    ErrorCode resend(const std::string& email, 
                     const std::string& type = "signup");

    // ========== Token Management ==========

    /**
     * @brief Enable/disable auto token refresh.
     * @param enabled Enable auto refresh.
     * @param session Session to auto-refresh.
     */
    void setAutoRefresh(bool enabled, const AuthSession& session = AuthSession());

    /**
     * @brief Get current session (if auto-refreshing).
     */
    const AuthSession& getCurrentSession() const { return _currentSession; }

    /**
     * @brief Check if there's an active session.
     */
    bool hasActiveSession() const { return _currentSession.isValid(); }

    /**
     * @brief Set current session (for external management).
     */
    void setCurrentSession(const AuthSession& session);

    /**
     * @brief Clear current session.
     */
    void clearSession();

    // ========== Events ==========

    /**
     * @brief Event triggered on successful authentication.
     * Parameter: AuthSession
     */
    Event<const AuthSession&> onAuthStateChange;

    /**
     * @brief Event triggered when tokens are refreshed.
     * Parameter: AuthTokens
     */
    Event<const AuthTokens&> onTokenRefreshed;

    /**
     * @brief Event triggered on auth error.
     * Parameters: errorCode, errorMessage
     */
    Event<const std::string&, const std::string&> onAuthError;

    /**
     * @brief Event triggered when session expires.
     */
    Event<> onSessionExpired;

private:
    SupabaseAuth();
    ~SupabaseAuth();

    /**
     * @brief Make HTTP request to auth endpoint.
     */
    HttpResponse makeRequest(HttpMethod method,
                             const std::string& endpoint,
                             const std::string& body = "",
                             const std::string& accessToken = "");

    /**
     * @brief Parse auth response JSON.
     */
    AuthResponse parseAuthResponse(const HttpResponse& httpResp);

    /**
     * @brief Parse user from JSON.
     */
    AuthUser parseUser(const std::string& json);

    /**
     * @brief Parse error from JSON.
     */
    void parseError(const std::string& json, AuthResponse& resp);

    /**
     * @brief Build JSON for signup/login.
     */
    std::string buildAuthJson(const std::string& email, 
                              const std::string& password,
                              const SignupOptions* options = nullptr);

    /**
     * @brief Build JSON for user update.
     */
    std::string buildUpdateJson(const std::string& email,
                                const std::string& password,
                                const std::map<std::string, std::string>& userData);

    /**
     * @brief Start refresh task.
     */
    void startRefreshTask();

    /**
     * @brief Stop refresh task.
     */
    void stopRefreshTask();

    /**
     * @brief Refresh task function.
     */
    static void refreshTaskFunc(void* pvParameters);

    SupabaseConfig _config;
    HttpClient _httpClient;
    AuthSession _currentSession;
    bool _initialized;
    bool _autoRefreshEnabled;
    SemaphoreHandle_t _mutex;
    TaskHandle_t _refreshTaskHandle;

    static constexpr const char* TAG = "SupabaseAuth";
};

} // namespace Supabase

#endif // SUPABASE_AUTH_H
