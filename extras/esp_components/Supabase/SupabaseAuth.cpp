#include "SupabaseAuth.h"
#include "CommonErrorCodes.h"
#include "esp_log.h"
#include "cJSON.h"
#include <ctime>

namespace Supabase {

namespace {
    // Supabase Auth API endpoints
    constexpr const char* EP_SIGNUP = "/auth/v1/signup";
    constexpr const char* EP_TOKEN = "/auth/v1/token";
    constexpr const char* EP_USER = "/auth/v1/user";
    constexpr const char* EP_LOGOUT = "/auth/v1/logout";
    constexpr const char* EP_RECOVER = "/auth/v1/recover";
    constexpr const char* EP_VERIFY = "/auth/v1/verify";
    constexpr const char* EP_RESEND = "/auth/v1/resend";
    
    constexpr uint32_t REFRESH_CHECK_INTERVAL_MS = 60000; // 1 minute
    constexpr uint32_t REFRESH_BUFFER_SEC = 300;          // 5 minutes before expiry
}

// ========== Singleton ==========

SupabaseAuth& SupabaseAuth::instance() {
    static SupabaseAuth auth;
    return auth;
}

SupabaseAuth::SupabaseAuth()
    : _initialized(false)
    , _autoRefreshEnabled(false)
    , _mutex(nullptr)
    , _refreshTaskHandle(nullptr) {
    _mutex = xSemaphoreCreateMutex();
}

SupabaseAuth::~SupabaseAuth() {
    stopRefreshTask();
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
    }
}

// ========== Initialization ==========

ErrorCode SupabaseAuth::init(const SupabaseConfig& config) {
    if (!config.isValid()) {
        ESP_LOGE(TAG, "Invalid configuration");
        return CommonErrorCodes::ArgumentError;
    }

    _config = config;

    // Configure HTTP client
    _httpClient.setTimeout(config.timeoutMs);
    _httpClient.setHeader("apikey", config.apiKey);
    _httpClient.setHeader("Content-Type", "application/json");

    _initialized = true;
    ESP_LOGI(TAG, "SupabaseAuth initialized for: %s", config.url.c_str());

    return CommonErrorCodes::None;
}

ErrorCode SupabaseAuth::init(const std::string& url, const std::string& anonKey) {
    SupabaseConfig config;
    config.url = url;
    config.apiKey = anonKey;
    return init(config);
}

bool SupabaseAuth::isNetworkAvailable() const {
    // TODO: Check actual network connectivity
    return true;
}

// ========== Authentication ==========

AuthResponse SupabaseAuth::signUp(const std::string& email, 
                                  const std::string& password,
                                  const SignupOptions& options) {
    AuthResponse resp;
    
    if (!_initialized) {
        resp.errorCode = "not_initialized";
        resp.errorMessage = "SupabaseAuth not initialized";
        return resp;
    }

    if (!isNetworkAvailable()) {
        resp.errorCode = "network_error";
        resp.errorMessage = "Network not available";
        return resp;
    }

    ESP_LOGI(TAG, "Signing up user: %s", email.c_str());

    std::string body = buildAuthJson(email, password, &options);
    HttpResponse httpResp = makeRequest(HttpMethod::POST, EP_SIGNUP, body);

    resp = parseAuthResponse(httpResp);

    if (resp.isOk()) {
        ESP_LOGI(TAG, "Signup successful: %s", resp.session.user.id.c_str());
        onAuthStateChange.trigger(resp.session);
    } else {
        ESP_LOGW(TAG, "Signup failed: %s - %s", 
                 resp.errorCode.c_str(), resp.errorMessage.c_str());
        onAuthError.trigger(resp.errorCode, resp.errorMessage);
    }

    return resp;
}

AuthResponse SupabaseAuth::signInWithPassword(const std::string& email, 
                                              const std::string& password) {
    AuthResponse resp;
    
    if (!_initialized) {
        resp.errorCode = "not_initialized";
        resp.errorMessage = "SupabaseAuth not initialized";
        return resp;
    }

    if (!isNetworkAvailable()) {
        resp.errorCode = "network_error";
        resp.errorMessage = "Network not available";
        return resp;
    }

    ESP_LOGI(TAG, "Signing in user: %s", email.c_str());

    std::string body = buildAuthJson(email, password);
    std::string endpoint = std::string(EP_TOKEN) + "?grant_type=password";
    
    HttpResponse httpResp = makeRequest(HttpMethod::POST, endpoint, body);

    resp = parseAuthResponse(httpResp);

    if (resp.isOk()) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _currentSession = resp.session;
        xSemaphoreGive(_mutex);

        if (_autoRefreshEnabled) {
            startRefreshTask();
        }

        ESP_LOGI(TAG, "Sign in successful: %s", resp.session.user.email.c_str());
        onAuthStateChange.trigger(resp.session);
    } else {
        ESP_LOGW(TAG, "Sign in failed: %s - %s", 
                 resp.errorCode.c_str(), resp.errorMessage.c_str());
        onAuthError.trigger(resp.errorCode, resp.errorMessage);
    }

    return resp;
}

ErrorCode SupabaseAuth::signOut(const std::string& accessToken) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    std::string token = accessToken.empty() ? _currentSession.tokens.accessToken : accessToken;
    
    if (!token.empty()) {
        HttpResponse httpResp = makeRequest(HttpMethod::POST, EP_LOGOUT, "", token);
        // Ignore response - we'll clear session regardless
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _currentSession = AuthSession();
    xSemaphoreGive(_mutex);

    stopRefreshTask();

    ESP_LOGI(TAG, "Signed out");
    return CommonErrorCodes::None;
}

AuthResponse SupabaseAuth::refreshSession(const std::string& refreshToken) {
    AuthResponse resp;
    
    if (!_initialized) {
        resp.errorCode = "not_initialized";
        resp.errorMessage = "SupabaseAuth not initialized";
        return resp;
    }

    if (refreshToken.empty()) {
        resp.errorCode = "invalid_token";
        resp.errorMessage = "Refresh token is empty";
        return resp;
    }

    ESP_LOGI(TAG, "Refreshing session...");

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "refresh_token", refreshToken.c_str());
    
    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string body(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    std::string endpoint = std::string(EP_TOKEN) + "?grant_type=refresh_token";
    HttpResponse httpResp = makeRequest(HttpMethod::POST, endpoint, body);

    resp = parseAuthResponse(httpResp);

    if (resp.isOk()) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _currentSession = resp.session;
        xSemaphoreGive(_mutex);

        ESP_LOGI(TAG, "Session refreshed successfully");
        onTokenRefreshed.trigger(resp.session.tokens);
    } else {
        ESP_LOGW(TAG, "Session refresh failed: %s", resp.errorMessage.c_str());
        onAuthError.trigger(resp.errorCode, resp.errorMessage);
        
        if (resp.isUnauthorized()) {
            onSessionExpired.trigger();
        }
    }

    return resp;
}

// ========== User Management ==========

AuthResponse SupabaseAuth::getUser(const std::string& accessToken) {
    AuthResponse resp;
    
    if (!_initialized) {
        resp.errorCode = "not_initialized";
        resp.errorMessage = "SupabaseAuth not initialized";
        return resp;
    }

    HttpResponse httpResp = makeRequest(HttpMethod::GET, EP_USER, "", accessToken);

    resp.httpStatus = httpResp.statusCode;
    resp.success = httpResp.isOk();

    if (resp.success) {
        resp.session.user = parseUser(httpResp.body);
    } else {
        parseError(httpResp.body, resp);
    }

    return resp;
}

AuthResponse SupabaseAuth::updateUser(const std::string& accessToken,
                                      const std::string& email,
                                      const std::string& password,
                                      const std::map<std::string, std::string>& userData) {
    AuthResponse resp;
    
    if (!_initialized) {
        resp.errorCode = "not_initialized";
        resp.errorMessage = "SupabaseAuth not initialized";
        return resp;
    }

    std::string body = buildUpdateJson(email, password, userData);
    HttpResponse httpResp = makeRequest(HttpMethod::PUT, EP_USER, body, accessToken);

    resp = parseAuthResponse(httpResp);

    if (resp.isOk()) {
        ESP_LOGI(TAG, "User updated successfully");
    }

    return resp;
}

ErrorCode SupabaseAuth::resetPasswordForEmail(const std::string& email,
                                              const std::string& redirectTo) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "email", email.c_str());
    if (!redirectTo.empty()) {
        cJSON_AddStringToObject(root, "redirect_to", redirectTo.c_str());
    }
    
    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string body(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    HttpResponse httpResp = makeRequest(HttpMethod::POST, EP_RECOVER, body);

    if (httpResp.isOk()) {
        ESP_LOGI(TAG, "Password reset email sent to: %s", email.c_str());
        return CommonErrorCodes::None;
    }

    ESP_LOGW(TAG, "Password reset failed: %d", httpResp.statusCode);
    return CommonErrorCodes::RequestFailed;
}

AuthResponse SupabaseAuth::verifyOtp(const std::string& email,
                                     const std::string& token,
                                     const std::string& type) {
    AuthResponse resp;
    
    if (!_initialized) {
        resp.errorCode = "not_initialized";
        resp.errorMessage = "SupabaseAuth not initialized";
        return resp;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "email", email.c_str());
    cJSON_AddStringToObject(root, "token", token.c_str());
    cJSON_AddStringToObject(root, "type", type.c_str());
    
    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string body(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    HttpResponse httpResp = makeRequest(HttpMethod::POST, EP_VERIFY, body);
    resp = parseAuthResponse(httpResp);

    return resp;
}

ErrorCode SupabaseAuth::resend(const std::string& email, const std::string& type) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "email", email.c_str());
    cJSON_AddStringToObject(root, "type", type.c_str());
    
    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string body(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    HttpResponse httpResp = makeRequest(HttpMethod::POST, EP_RESEND, body);

    if (httpResp.isOk()) {
        ESP_LOGI(TAG, "Resent %s email to: %s", type.c_str(), email.c_str());
        return CommonErrorCodes::None;
    }

    return CommonErrorCodes::RequestFailed;
}

// ========== Token Management ==========

void SupabaseAuth::setAutoRefresh(bool enabled, const AuthSession& session) {
    _autoRefreshEnabled = enabled;
    
    if (enabled && session.isValid()) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _currentSession = session;
        xSemaphoreGive(_mutex);
        startRefreshTask();
    } else if (!enabled) {
        stopRefreshTask();
    }
}

void SupabaseAuth::setCurrentSession(const AuthSession& session) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _currentSession = session;
    xSemaphoreGive(_mutex);
}

void SupabaseAuth::clearSession() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _currentSession = AuthSession();
    xSemaphoreGive(_mutex);
    stopRefreshTask();
}

// ========== Private Methods ==========

HttpResponse SupabaseAuth::makeRequest(HttpMethod method,
                                       const std::string& endpoint,
                                       const std::string& body,
                                       const std::string& accessToken) {
    std::string url = _config.url + endpoint;

    // Set API key header
    _httpClient.setHeader("apikey", _config.apiKey);
    _httpClient.setHeader("Content-Type", "application/json");

    // Set Authorization if we have a token
    if (!accessToken.empty()) {
        _httpClient.setBearerAuth(accessToken);
    } else {
        _httpClient.setHeader("Authorization", "Bearer " + _config.apiKey);
    }

    HttpResponse response;
    
    switch (method) {
        case HttpMethod::GET:
            response = _httpClient.get(url);
            break;
        case HttpMethod::POST:
            response = _httpClient.postJson(url, body);
            break;
        case HttpMethod::PUT:
            response = _httpClient.put(url, body, "application/json");
            break;
        case HttpMethod::DELETE:
            response = _httpClient.del(url);
            break;
        default:
            response.statusCode = 0;
            response.errorMessage = "Unsupported HTTP method";
    }

    return response;
}

AuthResponse SupabaseAuth::parseAuthResponse(const HttpResponse& httpResp) {
    AuthResponse resp;
    resp.httpStatus = httpResp.statusCode;
    resp.success = httpResp.isOk();

    if (!resp.success) {
        parseError(httpResp.body, resp);
        return resp;
    }

    cJSON* root = cJSON_Parse(httpResp.body.c_str());
    if (root == nullptr) {
        resp.success = false;
        resp.errorCode = "parse_error";
        resp.errorMessage = "Failed to parse JSON response";
        return resp;
    }

    // Parse access token
    cJSON* accessToken = cJSON_GetObjectItem(root, "access_token");
    cJSON* refreshToken = cJSON_GetObjectItem(root, "refresh_token");
    cJSON* tokenType = cJSON_GetObjectItem(root, "token_type");
    cJSON* expiresIn = cJSON_GetObjectItem(root, "expires_in");
    cJSON* expiresAt = cJSON_GetObjectItem(root, "expires_at");
    cJSON* user = cJSON_GetObjectItem(root, "user");

    if (cJSON_IsString(accessToken)) {
        resp.session.tokens.accessToken = accessToken->valuestring;
    }
    if (cJSON_IsString(refreshToken)) {
        resp.session.tokens.refreshToken = refreshToken->valuestring;
    }
    if (cJSON_IsString(tokenType)) {
        resp.session.tokens.tokenType = tokenType->valuestring;
    }
    if (cJSON_IsNumber(expiresIn)) {
        resp.session.tokens.expiresIn = static_cast<uint32_t>(expiresIn->valueint);
    }
    if (cJSON_IsNumber(expiresAt)) {
        resp.session.tokens.expiresAt = static_cast<uint64_t>(expiresAt->valuedouble);
    } else if (resp.session.tokens.expiresIn > 0) {
        // Calculate expiresAt from expiresIn
        resp.session.tokens.expiresAt = static_cast<uint64_t>(time(nullptr)) + 
                                        resp.session.tokens.expiresIn;
    }

    // Parse user
    if (user != nullptr) {
        cJSON* id = cJSON_GetObjectItem(user, "id");
        cJSON* email = cJSON_GetObjectItem(user, "email");
        cJSON* phone = cJSON_GetObjectItem(user, "phone");
        cJSON* emailConfirmed = cJSON_GetObjectItem(user, "email_confirmed_at");
        cJSON* phoneConfirmed = cJSON_GetObjectItem(user, "phone_confirmed_at");
        cJSON* createdAt = cJSON_GetObjectItem(user, "created_at");
        cJSON* lastSignIn = cJSON_GetObjectItem(user, "last_sign_in_at");
        cJSON* role = cJSON_GetObjectItem(user, "role");
        cJSON* userMetadata = cJSON_GetObjectItem(user, "user_metadata");
        cJSON* appMetadata = cJSON_GetObjectItem(user, "app_metadata");

        if (cJSON_IsString(id)) {
            resp.session.user.id = id->valuestring;
        }
        if (cJSON_IsString(email)) {
            resp.session.user.email = email->valuestring;
        }
        if (cJSON_IsString(phone)) {
            resp.session.user.phone = phone->valuestring;
        }
        // email_confirmed_at being non-null means confirmed
        resp.session.user.emailConfirmed = (emailConfirmed != nullptr && 
                                            !cJSON_IsNull(emailConfirmed));
        resp.session.user.phoneConfirmed = (phoneConfirmed != nullptr && 
                                            !cJSON_IsNull(phoneConfirmed));
        if (cJSON_IsString(role)) {
            resp.session.user.role = role->valuestring;
        }

        // Parse user_metadata
        if (cJSON_IsObject(userMetadata)) {
            cJSON* item = nullptr;
            cJSON_ArrayForEach(item, userMetadata) {
                if (cJSON_IsString(item)) {
                    resp.session.user.userMetadata[item->string] = item->valuestring;
                }
            }
        }

        // Parse app_metadata
        if (cJSON_IsObject(appMetadata)) {
            cJSON* item = nullptr;
            cJSON_ArrayForEach(item, appMetadata) {
                if (cJSON_IsString(item)) {
                    resp.session.user.appMetadata[item->string] = item->valuestring;
                }
            }
        }
    }

    cJSON_Delete(root);
    return resp;
}

AuthUser SupabaseAuth::parseUser(const std::string& json) {
    AuthUser user;
    
    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        return user;
    }

    cJSON* id = cJSON_GetObjectItem(root, "id");
    cJSON* email = cJSON_GetObjectItem(root, "email");
    cJSON* phone = cJSON_GetObjectItem(root, "phone");
    cJSON* role = cJSON_GetObjectItem(root, "role");
    cJSON* userMetadata = cJSON_GetObjectItem(root, "user_metadata");

    if (cJSON_IsString(id)) user.id = id->valuestring;
    if (cJSON_IsString(email)) user.email = email->valuestring;
    if (cJSON_IsString(phone)) user.phone = phone->valuestring;
    if (cJSON_IsString(role)) user.role = role->valuestring;

    if (cJSON_IsObject(userMetadata)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, userMetadata) {
            if (cJSON_IsString(item)) {
                user.userMetadata[item->string] = item->valuestring;
            }
        }
    }

    cJSON_Delete(root);
    return user;
}

void SupabaseAuth::parseError(const std::string& json, AuthResponse& resp) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        resp.errorMessage = "Unknown error";
        return;
    }

    cJSON* error = cJSON_GetObjectItem(root, "error");
    cJSON* errorCode = cJSON_GetObjectItem(root, "error_code");
    cJSON* errorDesc = cJSON_GetObjectItem(root, "error_description");
    cJSON* message = cJSON_GetObjectItem(root, "message");
    cJSON* msg = cJSON_GetObjectItem(root, "msg");

    if (cJSON_IsString(error)) {
        resp.errorCode = error->valuestring;
    } else if (cJSON_IsString(errorCode)) {
        resp.errorCode = errorCode->valuestring;
    }

    if (cJSON_IsString(errorDesc)) {
        resp.errorMessage = errorDesc->valuestring;
    } else if (cJSON_IsString(message)) {
        resp.errorMessage = message->valuestring;
    } else if (cJSON_IsString(msg)) {
        resp.errorMessage = msg->valuestring;
    } else if (cJSON_IsString(error)) {
        resp.errorMessage = error->valuestring;
    } else {
        resp.errorMessage = "Unknown error";
    }

    cJSON_Delete(root);
}

std::string SupabaseAuth::buildAuthJson(const std::string& email, 
                                        const std::string& password,
                                        const SignupOptions* options) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "email", email.c_str());
    cJSON_AddStringToObject(root, "password", password.c_str());

    if (options != nullptr) {
        // Add user metadata
        if (!options->userData.empty()) {
            cJSON* data = cJSON_CreateObject();
            for (const auto& pair : options->userData) {
                cJSON_AddStringToObject(data, pair.first.c_str(), pair.second.c_str());
            }
            cJSON_AddItemToObject(root, "data", data);
        }

        // Add redirect URL
        if (!options->redirectTo.empty()) {
            cJSON* opts = cJSON_CreateObject();
            cJSON_AddStringToObject(opts, "redirect_to", options->redirectTo.c_str());
            cJSON_AddItemToObject(root, "options", opts);
        }
    }

    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string result(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    return result;
}

std::string SupabaseAuth::buildUpdateJson(const std::string& email,
                                          const std::string& password,
                                          const std::map<std::string, std::string>& userData) {
    cJSON* root = cJSON_CreateObject();
    
    if (!email.empty()) {
        cJSON_AddStringToObject(root, "email", email.c_str());
    }
    if (!password.empty()) {
        cJSON_AddStringToObject(root, "password", password.c_str());
    }
    if (!userData.empty()) {
        cJSON* data = cJSON_CreateObject();
        for (const auto& pair : userData) {
            cJSON_AddStringToObject(data, pair.first.c_str(), pair.second.c_str());
        }
        cJSON_AddItemToObject(root, "data", data);
    }

    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string result(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    return result;
}

void SupabaseAuth::startRefreshTask() {
    if (_refreshTaskHandle != nullptr) {
        return; // Already running
    }

    xTaskCreate(refreshTaskFunc, "SupabaseRefresh", 4096, this, 5, &_refreshTaskHandle);
    ESP_LOGI(TAG, "Started token refresh task");
}

void SupabaseAuth::stopRefreshTask() {
    if (_refreshTaskHandle != nullptr) {
        vTaskDelete(_refreshTaskHandle);
        _refreshTaskHandle = nullptr;
        ESP_LOGI(TAG, "Stopped token refresh task");
    }
}

void SupabaseAuth::refreshTaskFunc(void* pvParameters) {
    auto* self = static_cast<SupabaseAuth*>(pvParameters);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(REFRESH_CHECK_INTERVAL_MS));

        if (!self->_autoRefreshEnabled) {
            continue;
        }

        xSemaphoreTake(self->_mutex, portMAX_DELAY);
        AuthSession session = self->_currentSession;
        xSemaphoreGive(self->_mutex);

        if (!session.isValid()) {
            continue;
        }

        // Check if we need to refresh
        if (session.tokens.isExpired(REFRESH_BUFFER_SEC)) {
            ESP_LOGI(TAG, "Token expiring soon, refreshing...");
            self->refreshSession(session.tokens.refreshToken);
        }
    }
}

} // namespace Supabase
