#include "CloudAuthProvider.h"

#if AUTH_CLOUD_ENABLED

#include <cstring>
#include <ctime>
#include "esp_log.h"
#include "cJSON.h"

namespace {
    constexpr const char* REST_PROFILES = "/rest/v1/profiles";
}

CloudAuthProvider::CloudAuthProvider()
    : _mutex(nullptr)
    , _autoRefreshEnabled(true) {
    _mutex = xSemaphoreCreateMutex();
}

CloudAuthProvider::~CloudAuthProvider() {
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
    }
}

ErrorCode CloudAuthProvider::init(const SupabaseConfig& config) {
    if (!config.isValid()) {
        ESP_LOGE(TAG, "Invalid Supabase configuration");
        return AuthErrorCodes::InvalidConfig;
    }

    _config = config;
    
    // Initialize SupabaseAuth singleton
    auto& auth = Supabase::SupabaseAuth::instance();
    ErrorCode err = auth.init(config);
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to initialize SupabaseAuth: %s", err.description().c_str());
        return err;
    }

    // Initialize SupabaseClient singleton if not already
    auto& client = Supabase::SupabaseClient::instance();
    if (!client.isConfigured()) {
        err = client.init(config.url, config.apiKey);
        if (err != CommonErrorCodes::None) {
            ESP_LOGW(TAG, "SupabaseClient init returned: %s", err.description().c_str());
        }
    }

    // Setup event handlers
    setupEventHandlers();

    _initialized = true;
    ESP_LOGI(TAG, "CloudAuthProvider initialized for %s", config.url.c_str());
    
    return AuthErrorCodes::None;
}

ErrorCode CloudAuthProvider::init() {
#if defined(AUTH_SUPABASE_URL) && defined(AUTH_SUPABASE_ANON_KEY)
    SupabaseConfig config;
    config.url = AUTH_SUPABASE_URL;
    config.anonKey = AUTH_SUPABASE_ANON_KEY;
    return init(config);
#else
    ESP_LOGE(TAG, "Supabase URL and key not defined");
    return AuthErrorCodes::SupabaseNotConfigured;
#endif
}

void CloudAuthProvider::setConfig(const SupabaseConfig& config) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _config = config;
    xSemaphoreGive(_mutex);
    
    // Re-initialize auth with new config
    init(config);
}

bool CloudAuthProvider::isReady() const {
    return _initialized && _config.isValid() && 
           Supabase::SupabaseAuth::instance().isInitialized();
}

// ========== Authentication ==========

AuthResult CloudAuthProvider::login(const AuthCredentials& credentials) {
    if (!isReady()) {
        return AuthResult(AuthErrorCodes::NotInitialized);
    }

    if (!isNetworkAvailable()) {
        return AuthResult(AuthErrorCodes::NoNetwork);
    }

    ESP_LOGI(TAG, "Logging in user: %s", credentials.email.c_str());

    auto& auth = Supabase::SupabaseAuth::instance();
    auto response = auth.signInWithPassword(credentials.email, credentials.password);

    AuthResult result = convertToAuthResult(response);

    if (result.isSuccess()) {
        // Enable auto-refresh for this session
        if (_autoRefreshEnabled) {
            auth.setAutoRefresh(true, response.session);
        }

        ESP_LOGI(TAG, "Login successful: %s", result.session.user.email.c_str());
        onAuthComplete.trigger(result);
    } else {
        onAuthError.trigger(result.error);
    }

    return result;
}

void CloudAuthProvider::loginAsync(const AuthCredentials& credentials, AuthCallback callback) {
    auto* taskData = new std::pair<CloudAuthProvider*, std::pair<AuthCredentials, AuthCallback>>(
        this, std::make_pair(credentials, callback)
    );
    
    xTaskCreate([](void* pvParams) {
        auto* data = static_cast<std::pair<CloudAuthProvider*, std::pair<AuthCredentials, AuthCallback>>*>(pvParams);
        auto* self = data->first;
        auto& creds = data->second.first;
        auto& cb = data->second.second;
        
        AuthResult result = self->login(creds);
        if (cb) {
            cb(result);
        }
        
        delete data;
        vTaskDelete(nullptr);
    }, "CloudLoginAsync", 8192, taskData, 5, nullptr);
}

AuthResult CloudAuthProvider::signup(const AuthCredentials& credentials, 
                                     const UserInfo& profile) {
    if (!isReady()) {
        return AuthResult(AuthErrorCodes::NotInitialized);
    }

    if (!isNetworkAvailable()) {
        return AuthResult(AuthErrorCodes::NoNetwork);
    }

    ESP_LOGI(TAG, "Signing up user: %s", credentials.email.c_str());

    // Build signup options with user metadata
    Supabase::SignupOptions options;
    if (!credentials.displayName.empty()) {
        options.userData["display_name"] = credentials.displayName;
    }
    if (!profile.displayName.empty()) {
        options.userData["display_name"] = profile.displayName;
    }

    auto& auth = Supabase::SupabaseAuth::instance();
    auto response = auth.signUp(credentials.email, credentials.password, options);

    AuthResult result = convertToAuthResult(response);

    if (result.isSuccess()) {
        // Create profile in profiles table
        UserInfo newProfile = profile;
        newProfile.id = result.session.user.id;
        newProfile.email = credentials.email;
        
        ErrorCode profileErr = updateProfile(newProfile.id, newProfile);
        if (profileErr != AuthErrorCodes::None) {
            ESP_LOGW(TAG, "Failed to create profile: %s", profileErr.description().c_str());
        }

        ESP_LOGI(TAG, "Signup successful: %s", credentials.email.c_str());
        onAuthComplete.trigger(result);
    } else {
        onAuthError.trigger(result.error);
    }

    return result;
}

void CloudAuthProvider::signupAsync(const AuthCredentials& credentials,
                                    const UserInfo& profile,
                                    AuthCallback callback) {
    auto* taskData = new std::tuple<CloudAuthProvider*, AuthCredentials, UserInfo, AuthCallback>(
        this, credentials, profile, callback
    );
    
    xTaskCreate([](void* pvParams) {
        auto* data = static_cast<std::tuple<CloudAuthProvider*, AuthCredentials, UserInfo, AuthCallback>*>(pvParams);
        auto* self = std::get<0>(*data);
        auto& creds = std::get<1>(*data);
        auto& profile = std::get<2>(*data);
        auto& cb = std::get<3>(*data);
        
        AuthResult result = self->signup(creds, profile);
        if (cb) {
            cb(result);
        }
        
        delete data;
        vTaskDelete(nullptr);
    }, "CloudSignupAsync", 8192, taskData, 5, nullptr);
}

ErrorCode CloudAuthProvider::logout(const std::string& userId) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    auto& auth = Supabase::SupabaseAuth::instance();
    ErrorCode err = auth.signOut();

    ESP_LOGI(TAG, "User logged out");
    return err;
}

AuthResult CloudAuthProvider::refreshToken(const std::string& refreshToken) {
    if (!isReady()) {
        return AuthResult(AuthErrorCodes::NotInitialized);
    }

    if (!isNetworkAvailable()) {
        return AuthResult(AuthErrorCodes::NoNetwork);
    }

    if (refreshToken.empty()) {
        return AuthResult(AuthErrorCodes::InvalidRefreshToken);
    }

    ESP_LOGI(TAG, "Refreshing token...");

    auto& auth = Supabase::SupabaseAuth::instance();
    auto response = auth.refreshSession(refreshToken);

    AuthResult result = convertToAuthResult(response);

    if (result.isSuccess()) {
        ESP_LOGI(TAG, "Token refreshed successfully");
        onTokenRefreshed.trigger(result.session.tokens);
    }

    return result;
}

bool CloudAuthProvider::validateToken(const std::string& accessToken) {
    if (accessToken.empty()) {
        return false;
    }

    auto& auth = Supabase::SupabaseAuth::instance();
    auto response = auth.getUser(accessToken);
    return response.isOk();
}

// ========== Profile Management ==========

ErrorCode CloudAuthProvider::getProfile(const std::string& userId, UserInfo& profile) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    if (!isNetworkAvailable()) {
        return AuthErrorCodes::NoNetwork;
    }

    auto& client = Supabase::SupabaseClient::instance();
    auto resp = client.from("profiles")
        .select("*")
        .eq("id", userId)
        .single()
        .execute();

    if (!resp.isOk()) {
        if (resp.isNotFound()) {
            return AuthErrorCodes::UserNotFound;
        }
        return AuthErrorCodes::CloudRequestFailed;
    }

    return parseUserProfile(resp.body, profile);
}

ErrorCode CloudAuthProvider::updateProfile(const std::string& userId, const UserInfo& profile) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    if (!isNetworkAvailable()) {
        return AuthErrorCodes::NoNetwork;
    }

    std::string body = buildProfileJson(profile);
    
    // Add ID to the body for upsert
    cJSON* root = cJSON_Parse(body.c_str());
    if (root) {
        cJSON_AddStringToObject(root, "id", userId.c_str());
        char* jsonStr = cJSON_PrintUnformatted(root);
        body = std::string(jsonStr);
        free(jsonStr);
        cJSON_Delete(root);
    }

    auto& client = Supabase::SupabaseClient::instance();
    auto resp = client.from("profiles").upsert(body, "id");

    if (!resp.isOk()) {
        ESP_LOGE(TAG, "Failed to update profile: %d - %s", resp.statusCode, resp.error.c_str());
        return AuthErrorCodes::CloudRequestFailed;
    }

    onProfileUpdated.trigger(userId, profile);
    return AuthErrorCodes::None;
}

ErrorCode CloudAuthProvider::deleteUser(const std::string& userId) {
    // Note: Deleting users requires admin/service role in Supabase
    ESP_LOGW(TAG, "Delete user not implemented - requires admin privileges");
    return AuthErrorCodes::PermissionDenied;
}

bool CloudAuthProvider::userExists(const std::string& email) {
    // This would require admin access in Supabase
    // For now, return false
    return false;
}

ErrorCode CloudAuthProvider::changePassword(const std::string& userId,
                                            const std::string& oldPassword,
                                            const std::string& newPassword) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    if (!isNetworkAvailable()) {
        return AuthErrorCodes::NoNetwork;
    }

    auto& auth = Supabase::SupabaseAuth::instance();
    
    // Need current session's access token
    const auto& session = auth.getCurrentSession();
    if (!session.isValid()) {
        return AuthErrorCodes::NotLoggedIn;
    }

    auto response = auth.updateUser(session.tokens.accessToken, "", newPassword);

    if (!response.isOk()) {
        return mapSupabaseError(response.errorCode, response.errorMessage);
    }

    ESP_LOGI(TAG, "Password changed for user: %s", userId.c_str());
    return AuthErrorCodes::None;
}

ErrorCode CloudAuthProvider::requestPasswordReset(const std::string& email) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    if (!isNetworkAvailable()) {
        return AuthErrorCodes::NoNetwork;
    }

    auto& auth = Supabase::SupabaseAuth::instance();
    ErrorCode err = auth.resetPasswordForEmail(email);

    if (err == CommonErrorCodes::None) {
        ESP_LOGI(TAG, "Password reset email sent to: %s", email.c_str());
    }

    return err;
}

// ========== User Management (Admin) ==========

ErrorCode CloudAuthProvider::getAllUsers(std::vector<UserInfo>& users) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    if (!isNetworkAvailable()) {
        return AuthErrorCodes::NoNetwork;
    }

    auto& client = Supabase::SupabaseClient::instance();
    auto resp = client.from("profiles").select("*").execute();

    if (!resp.isOk()) {
        return AuthErrorCodes::CloudRequestFailed;
    }

    // Parse array of profiles
    cJSON* root = cJSON_Parse(resp.body.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return AuthErrorCodes::InvalidCloudResponse;
    }

    users.clear();
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        UserInfo profile;
        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* email = cJSON_GetObjectItem(item, "email");
        cJSON* displayName = cJSON_GetObjectItem(item, "display_name");
        cJSON* role = cJSON_GetObjectItem(item, "role");
        cJSON* isConfirmed = cJSON_GetObjectItem(item, "is_confirmed");
        cJSON* isActive = cJSON_GetObjectItem(item, "is_active");

        if (cJSON_IsString(id)) profile.id = id->valuestring;
        if (cJSON_IsString(email)) profile.email = email->valuestring;
        if (cJSON_IsString(displayName)) profile.displayName = displayName->valuestring;
        if (cJSON_IsString(role)) profile.role = roleFromDbString(role->valuestring);
        if (cJSON_IsBool(isConfirmed)) profile.isConfirmed = cJSON_IsTrue(isConfirmed);
        if (cJSON_IsBool(isActive)) profile.isActive = cJSON_IsTrue(isActive);

        users.push_back(profile);
    }

    cJSON_Delete(root);
    return AuthErrorCodes::None;
}

ErrorCode CloudAuthProvider::getPendingUsers(std::vector<UserInfo>& users) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    if (!isNetworkAvailable()) {
        return AuthErrorCodes::NoNetwork;
    }

    auto& client = Supabase::SupabaseClient::instance();
    auto resp = client.from("profiles")
        .select("*")
        .eq("is_confirmed", "false")
        .execute();

    if (!resp.isOk()) {
        return AuthErrorCodes::CloudRequestFailed;
    }

    // Parse response (same as getAllUsers)
    cJSON* root = cJSON_Parse(resp.body.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return AuthErrorCodes::InvalidCloudResponse;
    }

    users.clear();
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        UserInfo profile;
        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* email = cJSON_GetObjectItem(item, "email");
        cJSON* displayName = cJSON_GetObjectItem(item, "display_name");

        if (cJSON_IsString(id)) profile.id = id->valuestring;
        if (cJSON_IsString(email)) profile.email = email->valuestring;
        if (cJSON_IsString(displayName)) profile.displayName = displayName->valuestring;
        profile.isConfirmed = false;

        users.push_back(profile);
    }

    cJSON_Delete(root);
    return AuthErrorCodes::None;
}

ErrorCode CloudAuthProvider::confirmUser(const std::string& userId) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    auto& client = Supabase::SupabaseClient::instance();
    auto resp = client.from("profiles")
        .update(R"({"is_confirmed": true})")
        .eq("id", userId)
        .execute();

    if (!resp.isOk()) {
        return AuthErrorCodes::CloudRequestFailed;
    }

    return AuthErrorCodes::None;
}

ErrorCode CloudAuthProvider::setUserRole(const std::string& userId, UserRole role) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "role", roleToDbString(role));
    
    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string body(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    auto& client = Supabase::SupabaseClient::instance();
    auto resp = client.from("profiles")
        .update(body)
        .eq("id", userId)
        .execute();

    if (!resp.isOk()) {
        return AuthErrorCodes::CloudRequestFailed;
    }

    ESP_LOGI(TAG, "User role updated: %s -> %s", userId.c_str(), roleToDbString(role));
    return AuthErrorCodes::None;
}

ErrorCode CloudAuthProvider::setUserActive(const std::string& userId, bool active) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "is_active", active);
    
    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string body(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    auto& client = Supabase::SupabaseClient::instance();
    auto resp = client.from("profiles")
        .update(body)
        .eq("id", userId)
        .execute();

    if (!resp.isOk()) {
        return AuthErrorCodes::CloudRequestFailed;
    }

    return AuthErrorCodes::None;
}

// ========== Cloud-Specific Methods ==========

bool CloudAuthProvider::isNetworkAvailable() const {
    return Supabase::SupabaseAuth::instance().isNetworkAvailable();
}

void CloudAuthProvider::setAutoRefresh(bool enabled) {
    _autoRefreshEnabled = enabled;
    auto& auth = Supabase::SupabaseAuth::instance();
    auth.setAutoRefresh(enabled);
}

AuthResult CloudAuthProvider::manualRefresh(const AuthSession& session) {
    return refreshToken(session.tokens.refreshToken);
}

std::unique_ptr<Supabase::SupabaseClient> CloudAuthProvider::getAuthenticatedClient(const AuthSession& session) {
    if (!session.tokens.accessToken.empty()) {
        return Supabase::SupabaseClient::createWithAuth(session.tokens.accessToken);
    }
    return nullptr;
}

ErrorCode CloudAuthProvider::getUserMetadata(const std::string& userId,
                                             std::map<std::string, std::string>& metadata) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    auto& auth = Supabase::SupabaseAuth::instance();
    const auto& session = auth.getCurrentSession();
    
    if (!session.isValid()) {
        return AuthErrorCodes::NotLoggedIn;
    }

    auto response = auth.getUser(session.tokens.accessToken);
    
    if (!response.isOk()) {
        return AuthErrorCodes::CloudRequestFailed;
    }

    metadata = response.session.user.userMetadata;
    return AuthErrorCodes::None;
}

ErrorCode CloudAuthProvider::setUserMetadata(const std::string& userId,
                                             const std::map<std::string, std::string>& metadata) {
    if (!isReady()) {
        return AuthErrorCodes::NotInitialized;
    }

    auto& auth = Supabase::SupabaseAuth::instance();
    const auto& session = auth.getCurrentSession();
    
    if (!session.isValid()) {
        return AuthErrorCodes::NotLoggedIn;
    }

    auto response = auth.updateUser(session.tokens.accessToken, "", "", metadata);
    
    if (!response.isOk()) {
        return mapSupabaseError(response.errorCode, response.errorMessage);
    }

    return AuthErrorCodes::None;
}

// ========== Private Methods ==========

AuthResult CloudAuthProvider::convertToAuthResult(const Supabase::AuthResponse& response) {
    AuthResult result;
    
    if (response.isOk()) {
        result.session.user = convertToUserInfo(response.session.user);
        result.session.tokens = convertToAuthTokens(response.session.tokens);
        result.session.provider = AuthProvider::Cloud;
        result.session.createdAt = time(nullptr);
        result.error = AuthErrorCodes::None;
    } else {
        result.error = mapSupabaseError(response.errorCode, response.errorMessage);
        result.message = response.errorMessage;
    }

    return result;
}

UserInfo CloudAuthProvider::convertToUserInfo(const Supabase::AuthUser& user) {
    UserInfo info;
    info.id = user.id;
    info.email = user.email;
    info.isConfirmed = user.emailConfirmed;
    info.isActive = true;
    info.role = UserRole::User;
    
    // Get display_name from user metadata
    auto it = user.userMetadata.find("display_name");
    if (it != user.userMetadata.end()) {
        info.displayName = it->second;
    }
    
    return info;
}

AuthTokens CloudAuthProvider::convertToAuthTokens(const Supabase::AuthTokens& tokens) {
    AuthTokens result;
    result.accessToken = tokens.accessToken;
    result.refreshToken = tokens.refreshToken;
    result.accessExpiresAt = tokens.expiresAt;
    return result;
}

ErrorCode CloudAuthProvider::parseUserProfile(const std::string& json, UserInfo& profile) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        return AuthErrorCodes::InvalidCloudResponse;
    }

    cJSON* id = cJSON_GetObjectItem(root, "id");
    cJSON* email = cJSON_GetObjectItem(root, "email");
    cJSON* displayName = cJSON_GetObjectItem(root, "display_name");
    cJSON* role = cJSON_GetObjectItem(root, "role");
    cJSON* isConfirmed = cJSON_GetObjectItem(root, "is_confirmed");
    cJSON* isActive = cJSON_GetObjectItem(root, "is_active");

    if (cJSON_IsString(id)) profile.id = id->valuestring;
    if (cJSON_IsString(email)) profile.email = email->valuestring;
    if (cJSON_IsString(displayName)) profile.displayName = displayName->valuestring;
    if (cJSON_IsString(role)) profile.role = roleFromDbString(role->valuestring);
    if (cJSON_IsBool(isConfirmed)) profile.isConfirmed = cJSON_IsTrue(isConfirmed);
    if (cJSON_IsBool(isActive)) profile.isActive = cJSON_IsTrue(isActive);

    cJSON_Delete(root);
    return AuthErrorCodes::None;
}

std::string CloudAuthProvider::buildProfileJson(const UserInfo& profile) {
    cJSON* root = cJSON_CreateObject();
    
    if (!profile.email.empty()) {
        cJSON_AddStringToObject(root, "email", profile.email.c_str());
    }
    if (!profile.displayName.empty()) {
        cJSON_AddStringToObject(root, "display_name", profile.displayName.c_str());
    }
    cJSON_AddStringToObject(root, "role", roleToDbString(profile.role));
    cJSON_AddBoolToObject(root, "is_confirmed", profile.isConfirmed);
    cJSON_AddBoolToObject(root, "is_active", profile.isActive);
    
    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string result(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);
    
    return result;
}

ErrorCode CloudAuthProvider::mapSupabaseError(const std::string& errorCode, const std::string& message) {
    // Map Supabase error codes to our error codes
    if (errorCode == "invalid_grant" || 
        message.find("Invalid login") != std::string::npos) {
        return AuthErrorCodes::WrongPassword;
    }
    if (errorCode == "user_not_found" || 
        message.find("User not found") != std::string::npos) {
        return AuthErrorCodes::UserNotFound;
    }
    if (errorCode == "email_exists" || 
        message.find("already registered") != std::string::npos ||
        message.find("already exists") != std::string::npos) {
        return AuthErrorCodes::EmailAlreadyExists;
    }
    if (message.find("Email not confirmed") != std::string::npos) {
        return AuthErrorCodes::AccountNotConfirmed;
    }
    if (errorCode == "invalid_token") {
        return AuthErrorCodes::InvalidRefreshToken;
    }
    if (errorCode == "network_error") {
        return AuthErrorCodes::NoNetwork;
    }
    
    return AuthErrorCodes::CloudRequestFailed;
}

const char* CloudAuthProvider::roleToDbString(UserRole role) {
    switch (role) {
        case UserRole::Guest: return "guest";
        case UserRole::User: return "user";
        case UserRole::Admin: return "admin";
        case UserRole::SuperAdmin: return "superadmin";
        default: return "user";
    }
}

UserRole CloudAuthProvider::roleFromDbString(const std::string& str) {
    if (str == "guest") return UserRole::Guest;
    if (str == "admin") return UserRole::Admin;
    if (str == "superadmin") return UserRole::SuperAdmin;
    return UserRole::User;
}

void CloudAuthProvider::setupEventHandlers() {
    auto& auth = Supabase::SupabaseAuth::instance();

    // Forward token refresh events
    auth.onTokenRefreshed.addHandler([this](const Supabase::AuthTokens& tokens) {
        AuthTokens ourTokens;
        ourTokens.accessToken = tokens.accessToken;
        ourTokens.refreshToken = tokens.refreshToken;
        ourTokens.accessExpiresAt = tokens.expiresAt;
        onTokenRefreshed.trigger(ourTokens);
    });

    // Forward auth errors
    auth.onAuthError.addHandler([this](const std::string& code, const std::string& msg) {
        ErrorCode err = mapSupabaseError(code, msg);
        onAuthError.trigger(err);
    });

    // Forward session expired
    auth.onSessionExpired.addHandler([this]() {
        ESP_LOGW(TAG, "Session expired - user needs to login again");
    });
}

#endif // AUTH_CLOUD_ENABLED
