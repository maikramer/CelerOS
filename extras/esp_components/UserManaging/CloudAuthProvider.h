#ifndef CLOUD_AUTH_PROVIDER_H
#define CLOUD_AUTH_PROVIDER_H

#include "IAuthProvider.h"
#include "SupabaseAuth.h"
#include "SupabaseClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <memory>

/**
 * @file CloudAuthProvider.h
 * @brief Authentication provider using Supabase cloud service.
 * 
 * This provider integrates with Supabase for cloud-based authentication,
 * now using the unified SupabaseAuth module for auth operations and
 * SupabaseClient for database operations.
 * 
 * @see https://supabase.com/docs/guides/auth
 */

#if AUTH_CLOUD_ENABLED

/**
 * @class CloudAuthProvider
 * @brief Cloud authentication provider using Supabase.
 * 
 * Features:
 * - Login/Signup via Supabase Auth API (using SupabaseAuth)
 * - JWT token management with auto-refresh
 * - User profile sync with database (using SupabaseClient)
 * - Authenticated database access per session
 * 
 * Example:
 * @code
 * SupabaseConfig config;
 * config.url = "https://xxx.supabase.co";
 * config.anonKey = "eyJ...";
 * 
 * CloudAuthProvider cloudAuth;
 * cloudAuth.init(config);
 * 
 * AuthCredentials creds("user@email.com", "password123");
 * AuthResult result = cloudAuth.login(creds);
 * 
 * if (result.isSuccess()) {
 *     // Get authenticated Supabase client for this user
 *     auto client = cloudAuth.getAuthenticatedClient(result.session);
 *     auto resp = client->from("user_data").select("*").execute();
 * }
 * @endcode
 */
class CloudAuthProvider : public IAuthProvider {
public:
    CloudAuthProvider();
    ~CloudAuthProvider() override;

    // ========== Configuration ==========

    /**
     * @brief Initialize with Supabase configuration.
     * @param config Supabase configuration.
     * @return Error code.
     */
    ErrorCode init(const SupabaseConfig& config);

    /**
     * @brief Initialize using default configuration.
     * Uses AUTH_SUPABASE_URL and AUTH_SUPABASE_ANON_KEY defines.
     */
    ErrorCode init() override;

    /**
     * @brief Update Supabase configuration.
     */
    void setConfig(const SupabaseConfig& config);

    /**
     * @brief Get current configuration.
     */
    const SupabaseConfig& getConfig() const { return _config; }

    // ========== IAuthProvider Interface ==========

    bool isReady() const override;
    AuthProvider getType() const override { return AuthProvider::Cloud; }

    AuthResult login(const AuthCredentials& credentials) override;
    void loginAsync(const AuthCredentials& credentials, AuthCallback callback) override;
    
    AuthResult signup(const AuthCredentials& credentials, 
                      const UserInfo& profile = UserInfo()) override;
    void signupAsync(const AuthCredentials& credentials,
                     const UserInfo& profile,
                     AuthCallback callback) override;
    
    ErrorCode logout(const std::string& userId = "") override;

    AuthResult refreshToken(const std::string& refreshToken) override;
    bool validateToken(const std::string& accessToken) override;

    ErrorCode getProfile(const std::string& userId, UserInfo& profile) override;
    ErrorCode updateProfile(const std::string& userId, const UserInfo& profile) override;
    ErrorCode deleteUser(const std::string& userId) override;
    bool userExists(const std::string& email) override;

    ErrorCode changePassword(const std::string& userId,
                             const std::string& oldPassword,
                             const std::string& newPassword) override;
    ErrorCode requestPasswordReset(const std::string& email) override;

    ErrorCode getAllUsers(std::vector<UserInfo>& users) override;
    ErrorCode getPendingUsers(std::vector<UserInfo>& users) override;
    ErrorCode confirmUser(const std::string& userId) override;
    ErrorCode setUserRole(const std::string& userId, UserRole role) override;
    ErrorCode setUserActive(const std::string& userId, bool active) override;

    // ========== Cloud-Specific Methods ==========

    /**
     * @brief Check if network is available for cloud operations.
     */
    bool isNetworkAvailable() const;

    /**
     * @brief Enable or disable auto token refresh.
     */
    void setAutoRefresh(bool enabled);

    /**
     * @brief Manually trigger token refresh for a session.
     * @param session Session to refresh.
     * @return Updated AuthResult.
     */
    AuthResult manualRefresh(const AuthSession& session);

    /**
     * @brief Get an authenticated SupabaseClient for a session.
     * @param session Auth session with valid access token.
     * @return Unique pointer to authenticated client.
     * 
     * Use this to perform database operations as the authenticated user.
     * The client respects Row Level Security (RLS) policies.
     */
    std::unique_ptr<Supabase::SupabaseClient> getAuthenticatedClient(const AuthSession& session);

    /**
     * @brief Get direct access to SupabaseAuth module.
     * @return Reference to the SupabaseAuth singleton.
     */
    Supabase::SupabaseAuth& getSupabaseAuth() { return Supabase::SupabaseAuth::instance(); }

    /**
     * @brief Get user metadata from Supabase.
     * @param userId User ID.
     * @param metadata Output map.
     * @return Error code.
     */
    ErrorCode getUserMetadata(const std::string& userId, 
                              std::map<std::string, std::string>& metadata);

    /**
     * @brief Update user metadata in Supabase.
     */
    ErrorCode setUserMetadata(const std::string& userId,
                              const std::map<std::string, std::string>& metadata);

    // ========== Events ==========

    /**
     * @brief Event triggered when token is refreshed.
     * Parameter: New AuthTokens
     */
    Event<const AuthTokens&> onTokenRefreshed;

    /**
     * @brief Event triggered when network status changes.
     * Parameter: bool (connected)
     */
    Event<bool> onNetworkStatusChanged;

private:
    /**
     * @brief Convert Supabase::AuthResponse to AuthResult.
     */
    AuthResult convertToAuthResult(const Supabase::AuthResponse& response);

    /**
     * @brief Convert Supabase::AuthUser to UserInfo.
     */
    UserInfo convertToUserInfo(const Supabase::AuthUser& user);

    /**
     * @brief Convert Supabase::AuthTokens to AuthTokens.
     */
    AuthTokens convertToAuthTokens(const Supabase::AuthTokens& tokens);

    /**
     * @brief Parse user profile from JSON (for database queries).
     */
    ErrorCode parseUserProfile(const std::string& json, UserInfo& profile);

    /**
     * @brief Build JSON for profile update.
     */
    std::string buildProfileJson(const UserInfo& profile);

    /**
     * @brief Map error from SupabaseAuth to our error codes.
     */
    ErrorCode mapSupabaseError(const std::string& errorCode, const std::string& message);

    /**
     * @brief Convert UserRole to string for database.
     */
    static const char* roleToDbString(UserRole role);

    /**
     * @brief Parse UserRole from database string.
     */
    static UserRole roleFromDbString(const std::string& str);

    /**
     * @brief Setup event handlers from SupabaseAuth.
     */
    void setupEventHandlers();

    SupabaseConfig _config;
    SemaphoreHandle_t _mutex;
    bool _autoRefreshEnabled;

    static constexpr const char* TAG = "CloudAuth";
};

#endif // AUTH_CLOUD_ENABLED

#endif // CLOUD_AUTH_PROVIDER_H
