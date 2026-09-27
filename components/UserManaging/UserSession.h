#ifndef USER_SESSION_H
#define USER_SESSION_H

#include "AuthTypes.h"
#include "Event.h"
#include "SupabaseClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <memory>

/**
 * @file UserSession.h
 * @brief User session management class with Supabase integration.
 * 
 * This class represents an active user session, managing user info,
 * tokens, session lifecycle events, and providing authenticated 
 * access to Supabase for database operations.
 */

/**
 * @class UserSession
 * @brief Manages an individual user session.
 * 
 * Tracks authentication state, tokens, and provides events for
 * session lifecycle (expiration, refresh, logout).
 * 
 * Example:
 * @code
 * UserSession session(authResult.session);
 * 
 * session.onExpired.addHandler([](UserSession* s) {
 *     ESP_LOGW("App", "Session expired for %s", s->getEmail().c_str());
 * });
 * 
 * if (session.isValid() && !session.isExpired()) {
 *     // Use session
 *     httpClient.setBearerAuth(session.getAccessToken());
 * }
 * @endcode
 */
class UserSession {
public:
    /**
     * @brief Construct a new session from AuthSession data.
     * @param session AuthSession data.
     */
    explicit UserSession(const AuthSession& session);

    /**
     * @brief Default constructor (invalid session).
     */
    UserSession();

    /**
     * @brief Destructor.
     */
    ~UserSession();

    // Prevent copying
    UserSession(const UserSession&) = delete;
    UserSession& operator=(const UserSession&) = delete;

    // Allow moving
    UserSession(UserSession&& other) noexcept;
    UserSession& operator=(UserSession&& other) noexcept;

    // ========== State ==========

    /**
     * @brief Check if session is valid (has user ID and active).
     */
    bool isValid() const;

    /**
     * @brief Check if access token has expired.
     * @param bufferSec Optional buffer before actual expiration (default: 60s).
     */
    bool isExpired(uint32_t bufferSec = 60) const;

    /**
     * @brief Check if refresh token has expired.
     */
    bool isRefreshExpired() const;

    /**
     * @brief Check if user has admin role.
     */
    bool isAdmin() const;

    /**
     * @brief Check if user has super admin role.
     */
    bool isSuperAdmin() const;

    /**
     * @brief Check if user account is confirmed.
     */
    bool isConfirmed() const;

    /**
     * @brief Check if session needs token refresh.
     */
    bool needsRefresh() const;

    // ========== User Info ==========

    /**
     * @brief Get user ID.
     */
    const std::string& getUserId() const;

    /**
     * @brief Get user email.
     */
    const std::string& getEmail() const;

    /**
     * @brief Get display name.
     */
    const std::string& getDisplayName() const;

    /**
     * @brief Get user role.
     */
    UserRole getRole() const;

    /**
     * @brief Get full user info.
     */
    const UserInfo& getUser() const;

    /**
     * @brief Get user metadata.
     */
    const std::map<std::string, std::string>& getMetadata() const;

    /**
     * @brief Get metadata value by key.
     * @param key Metadata key.
     * @param defaultValue Value if key not found.
     */
    std::string getMetadataValue(const std::string& key, 
                                 const std::string& defaultValue = "") const;

    // ========== Tokens ==========

    /**
     * @brief Get access token.
     */
    const std::string& getAccessToken() const;

    /**
     * @brief Get refresh token.
     */
    const std::string& getRefreshToken() const;

    /**
     * @brief Get token expiration timestamp.
     */
    uint64_t getExpiresAt() const;

    /**
     * @brief Get seconds until token expires.
     * @return Seconds remaining, or 0 if expired.
     */
    uint32_t getSecondsUntilExpiry() const;

    /**
     * @brief Get full tokens struct.
     */
    const AuthTokens& getTokens() const;

    // ========== Session Info ==========

    /**
     * @brief Get session creation time.
     */
    uint64_t getCreatedAt() const;

    /**
     * @brief Get authentication provider.
     */
    AuthProvider getProvider() const;

    /**
     * @brief Check if session is persistent.
     */
    bool isPersistent() const;

    /**
     * @brief Get underlying AuthSession.
     */
    const AuthSession& getSession() const;

    // ========== Supabase Access ==========

    /**
     * @brief Get an authenticated SupabaseClient for this session.
     * @return Unique pointer to authenticated client, or nullptr if no valid token.
     * 
     * Creates a new SupabaseClient instance authenticated with this session's
     * access token. Use this for database operations that respect Row Level
     * Security (RLS) policies.
     * 
     * Example:
     * @code
     * auto client = session.getSupabaseClient();
     * if (client) {
     *     auto resp = client->from("user_data")
     *         .select("*")
     *         .execute();
     *     if (resp.isOk()) {
     *         // Process user-specific data
     *     }
     * }
     * @endcode
     */
    std::unique_ptr<Supabase::SupabaseClient> getSupabaseClient() const;

    /**
     * @brief Check if this session can access Supabase.
     * @return True if session has valid cloud credentials.
     */
    bool hasSupabaseAccess() const;

    // ========== Modification ==========

    /**
     * @brief Update tokens after refresh.
     * @param newTokens New tokens.
     */
    void updateTokens(const AuthTokens& newTokens);

    /**
     * @brief Update user profile.
     * @param newUser Updated user info.
     */
    void updateUser(const UserInfo& newUser);

    /**
     * @brief Set metadata value.
     * @param key Metadata key.
     * @param value Metadata value.
     */
    void setMetadataValue(const std::string& key, const std::string& value);

    /**
     * @brief Mark session as invalidated.
     */
    void invalidate();

    // ========== Events ==========

    /**
     * @brief Event triggered when session expires.
     * Parameter: this UserSession
     */
    Event<UserSession*> onExpired;

    /**
     * @brief Event triggered when tokens are refreshed.
     * Parameter: this UserSession
     */
    Event<UserSession*> onRefreshed;

    /**
     * @brief Event triggered when session is invalidated.
     * Parameter: this UserSession
     */
    Event<UserSession*> onInvalidated;

    /**
     * @brief Event triggered when user info is updated.
     * Parameter: this UserSession
     */
    Event<UserSession*> onUserUpdated;

private:
    AuthSession _session;
    bool _isValid;
    SemaphoreHandle_t _mutex;

    /**
     * @brief Get current timestamp.
     */
    uint64_t getCurrentTimestamp() const;

    static constexpr const char* TAG = "UserSession";
};

#endif // USER_SESSION_H
