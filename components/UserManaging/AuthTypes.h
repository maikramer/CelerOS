#ifndef AUTH_TYPES_H
#define AUTH_TYPES_H

#include <string>
#include <map>
#include <cstdint>
#include <vector>

/**
 * @file AuthTypes.h
 * @brief Type definitions for the authentication system.
 * 
 * This file contains all enums, structs, and type aliases used
 * throughout the authentication module.
 */

// ============================================================================
// Compile-time Configuration Flags
// ============================================================================

#ifndef AUTH_LOCAL_ENABLED
#define AUTH_LOCAL_ENABLED 1
#endif

#ifndef AUTH_CLOUD_ENABLED
#define AUTH_CLOUD_ENABLED 1
#endif

#ifndef AUTH_BLUETOOTH_ENABLED
#define AUTH_BLUETOOTH_ENABLED 0
#endif

#ifndef AUTH_WIFI_ENABLED
#define AUTH_WIFI_ENABLED 1
#endif

#ifndef AUTH_REALTIME_ENABLED
#define AUTH_REALTIME_ENABLED 0
#endif

// ============================================================================
// Enums
// ============================================================================

/**
 * @enum AuthState
 * @brief Current state of the authentication system.
 */
enum class AuthState {
    Idle,               /**< Not initialized or no activity */
    LoggedOut,          /**< No active session */
    LoggingIn,          /**< Login in progress */
    SigningUp,          /**< Signup in progress */
    LoggedIn,           /**< Active session exists */
    TokenRefreshing,    /**< Refreshing access token */
    Syncing,            /**< Syncing with cloud */
    Error               /**< Error state */
};

/**
 * @enum AuthProvider
 * @brief Authentication provider type.
 */
enum class AuthProvider {
    None,       /**< No provider */
    Local,      /**< Local NVS storage only */
    Cloud,      /**< Cloud (Supabase) only */
    Both        /**< Both local and cloud with sync */
};

/**
 * @enum UserRole
 * @brief User role/permission level.
 */
enum class UserRole {
    Guest,          /**< Unauthenticated or limited access */
    User,           /**< Regular authenticated user */
    Admin,          /**< Administrator with full access */
    SuperAdmin      /**< Super administrator */
};

/**
 * @enum SyncDirection
 * @brief Direction for data synchronization.
 */
enum class SyncDirection {
    LocalToCloud,   /**< Push local changes to cloud */
    CloudToLocal,   /**< Pull cloud changes to local */
    Bidirectional   /**< Merge both directions */
};

/**
 * @enum SyncConflictResolution
 * @brief Strategy for resolving sync conflicts.
 */
enum class SyncConflictResolution {
    CloudWins,      /**< Cloud data takes priority */
    LocalWins,      /**< Local data takes priority */
    NewestWins,     /**< Most recent modification wins */
    Manual          /**< Require manual resolution */
};

// ============================================================================
// Structs
// ============================================================================

/**
 * @struct UserInfo
 * @brief Complete user profile information.
 */
struct UserInfo {
    std::string id;             /**< Unique user ID (UUID) */
    std::string email;          /**< User email address */
    std::string displayName;    /**< Display name */
    std::string passwordHash;   /**< Hashed password (local only) */
    UserRole role;              /**< User role */
    bool isConfirmed;           /**< Email/account confirmed */
    bool isActive;              /**< Account is active */
    uint64_t createdAt;         /**< Creation timestamp (Unix epoch) */
    uint64_t updatedAt;         /**< Last update timestamp */
    uint64_t lastLoginAt;       /**< Last login timestamp */
    std::map<std::string, std::string> metadata; /**< Custom metadata */

    UserInfo()
        : role(UserRole::User)
        , isConfirmed(false)
        , isActive(true)
        , createdAt(0)
        , updatedAt(0)
        , lastLoginAt(0) {}

    /**
     * @brief Check if user is admin or higher.
     */
    bool isAdmin() const {
        return role == UserRole::Admin || role == UserRole::SuperAdmin;
    }

    /**
     * @brief Check if user info is valid.
     */
    bool isValid() const {
        return !id.empty() && !email.empty();
    }
};

/**
 * @struct AuthTokens
 * @brief JWT tokens for authentication.
 */
struct AuthTokens {
    std::string accessToken;    /**< JWT access token */
    std::string refreshToken;   /**< JWT refresh token */
    uint64_t accessExpiresAt;   /**< Access token expiration (Unix epoch) */
    uint64_t refreshExpiresAt;  /**< Refresh token expiration (Unix epoch) */

    AuthTokens()
        : accessExpiresAt(0)
        , refreshExpiresAt(0) {}

    /**
     * @brief Check if access token is expired.
     * @param bufferSec Buffer time in seconds before actual expiration.
     */
    bool isAccessExpired(uint32_t bufferSec = 60) const;

    /**
     * @brief Check if refresh token is expired.
     */
    bool isRefreshExpired() const;

    /**
     * @brief Check if tokens are valid (non-empty and not expired).
     */
    bool isValid() const {
        return !accessToken.empty() && !isAccessExpired();
    }

    /**
     * @brief Clear all tokens.
     */
    void clear() {
        accessToken.clear();
        refreshToken.clear();
        accessExpiresAt = 0;
        refreshExpiresAt = 0;
    }
};

/**
 * @struct AuthSession
 * @brief Complete authentication session.
 */
struct AuthSession {
    UserInfo user;              /**< User information */
    AuthTokens tokens;          /**< JWT tokens */
    AuthProvider provider;      /**< Provider that created this session */
    uint64_t createdAt;         /**< Session creation time */
    bool isPersistent;          /**< Should survive app restart */

    AuthSession()
        : provider(AuthProvider::None)
        , createdAt(0)
        , isPersistent(true) {}

    /**
     * @brief Check if session is valid.
     */
    bool isValid() const {
        return user.isValid() && (provider == AuthProvider::Local || tokens.isValid());
    }

    /**
     * @brief Get user ID shortcut.
     */
    const std::string& getUserId() const {
        return user.id;
    }
};

/**
 * @struct AuthCredentials
 * @brief Login/signup credentials.
 */
struct AuthCredentials {
    std::string email;
    std::string password;
    std::string displayName;    /**< Optional, for signup */
    bool rememberMe;            /**< Persist session */

    AuthCredentials()
        : rememberMe(true) {}

    AuthCredentials(const std::string& email, const std::string& password)
        : email(email)
        , password(password)
        , rememberMe(true) {}
};

/**
 * @struct SupabaseConfig
 * @brief Configuration for Supabase connection.
 */
struct SupabaseConfig {
    std::string url;            /**< Supabase project URL */
    std::string anonKey;        /**< Supabase anonymous key */
    std::string serviceKey;     /**< Supabase service key (optional) */
    uint32_t timeoutMs;         /**< Request timeout in milliseconds */
    bool enableRealtime;        /**< Enable realtime subscriptions */

    SupabaseConfig()
        : timeoutMs(10000)
        , enableRealtime(AUTH_REALTIME_ENABLED) {}

    /**
     * @brief Check if configuration is valid.
     */
    bool isValid() const {
        return !url.empty() && !anonKey.empty();
    }

    /**
     * @brief Get auth endpoint URL.
     */
    std::string getAuthUrl() const {
        return url + "/auth/v1";
    }

    /**
     * @brief Get REST API endpoint URL.
     */
    std::string getRestUrl() const {
        return url + "/rest/v1";
    }

    /**
     * @brief Get realtime endpoint URL.
     */
    std::string getRealtimeUrl() const {
        return url + "/realtime/v1";
    }
};

/**
 * @struct AuthConfig
 * @brief General authentication configuration.
 */
struct AuthConfig {
    AuthProvider defaultProvider;   /**< Default auth provider */
    bool autoRefreshTokens;         /**< Automatically refresh tokens */
    uint32_t tokenRefreshBufferSec; /**< Seconds before expiry to refresh */
    bool autoSyncEnabled;           /**< Enable automatic sync */
    uint32_t autoSyncIntervalSec;   /**< Auto sync interval */
    SyncConflictResolution conflictResolution; /**< Sync conflict strategy */
    bool persistSessions;           /**< Persist sessions to NVS */
    uint32_t maxConcurrentSessions; /**< Max simultaneous sessions */

    AuthConfig()
        : defaultProvider(AuthProvider::Both)
        , autoRefreshTokens(true)
        , tokenRefreshBufferSec(300)  // 5 minutes
        , autoSyncEnabled(true)
        , autoSyncIntervalSec(300)    // 5 minutes
        , conflictResolution(SyncConflictResolution::CloudWins)
        , persistSessions(true)
        , maxConcurrentSessions(5) {}
};

/**
 * @struct SyncStatus
 * @brief Status of a sync operation.
 */
struct SyncStatus {
    bool inProgress;
    uint64_t lastSyncAt;
    uint32_t usersUploaded;
    uint32_t usersDownloaded;
    uint32_t conflicts;
    std::string lastError;

    SyncStatus()
        : inProgress(false)
        , lastSyncAt(0)
        , usersUploaded(0)
        , usersDownloaded(0)
        , conflicts(0) {}
};

// ============================================================================
// Constants
// ============================================================================

namespace AuthConstants {
    // NVS namespaces and keys
    constexpr const char* NVS_NAMESPACE = "auth";
    constexpr const char* NVS_KEY_USERS = "users";
    constexpr const char* NVS_KEY_SESSIONS = "sessions";
    constexpr const char* NVS_KEY_CONFIG = "config";
    constexpr const char* NVS_KEY_CURRENT_USER = "current";
    
    // Token settings
    constexpr uint32_t DEFAULT_ACCESS_TOKEN_LIFETIME_SEC = 3600;      // 1 hour
    constexpr uint32_t DEFAULT_REFRESH_TOKEN_LIFETIME_SEC = 604800;   // 7 days
    
    // Password requirements
    constexpr uint32_t MIN_PASSWORD_LENGTH = 6;
    constexpr uint32_t MAX_PASSWORD_LENGTH = 128;
    
    // Retry settings
    constexpr uint32_t MAX_LOGIN_RETRIES = 5;
    constexpr uint32_t LOGIN_LOCKOUT_SEC = 300;  // 5 minutes
    
    // Supabase table names
    constexpr const char* SUPABASE_PROFILES_TABLE = "profiles";
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert AuthState to string.
 */
inline const char* authStateToString(AuthState state) {
    switch (state) {
        case AuthState::Idle: return "Idle";
        case AuthState::LoggedOut: return "LoggedOut";
        case AuthState::LoggingIn: return "LoggingIn";
        case AuthState::SigningUp: return "SigningUp";
        case AuthState::LoggedIn: return "LoggedIn";
        case AuthState::TokenRefreshing: return "TokenRefreshing";
        case AuthState::Syncing: return "Syncing";
        case AuthState::Error: return "Error";
        default: return "Unknown";
    }
}

/**
 * @brief Convert AuthProvider to string.
 */
inline const char* authProviderToString(AuthProvider provider) {
    switch (provider) {
        case AuthProvider::None: return "None";
        case AuthProvider::Local: return "Local";
        case AuthProvider::Cloud: return "Cloud";
        case AuthProvider::Both: return "Both";
        default: return "Unknown";
    }
}

/**
 * @brief Convert UserRole to string.
 */
inline const char* userRoleToString(UserRole role) {
    switch (role) {
        case UserRole::Guest: return "Guest";
        case UserRole::User: return "User";
        case UserRole::Admin: return "Admin";
        case UserRole::SuperAdmin: return "SuperAdmin";
        default: return "Unknown";
    }
}

#endif // AUTH_TYPES_H
