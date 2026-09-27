#include "UserSession.h"
#include <ctime>
#include "esp_log.h"

UserSession::UserSession(const AuthSession& session)
    : _session(session)
    , _isValid(session.isValid())
    , _mutex(nullptr) {
    _mutex = xSemaphoreCreateMutex();
}

UserSession::UserSession()
    : _isValid(false)
    , _mutex(nullptr) {
    _mutex = xSemaphoreCreateMutex();
}

UserSession::~UserSession() {
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
    }
}

UserSession::UserSession(UserSession&& other) noexcept
    : _session(std::move(other._session))
    , _isValid(other._isValid)
    , _mutex(other._mutex) {
    other._mutex = nullptr;
    other._isValid = false;
}

UserSession& UserSession::operator=(UserSession&& other) noexcept {
    if (this != &other) {
        if (_mutex != nullptr) {
            vSemaphoreDelete(_mutex);
        }
        _session = std::move(other._session);
        _isValid = other._isValid;
        _mutex = other._mutex;
        other._mutex = nullptr;
        other._isValid = false;
    }
    return *this;
}

// ========== State ==========

bool UserSession::isValid() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool valid = _isValid && !_session.user.id.empty();
    xSemaphoreGive(_mutex);
    return valid;
}

bool UserSession::isExpired(uint32_t bufferSec) const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    // Local sessions don't expire
    if (_session.provider == AuthProvider::Local) {
        xSemaphoreGive(_mutex);
        return false;
    }
    
    uint64_t expiresAt = _session.tokens.accessExpiresAt;
    xSemaphoreGive(_mutex);
    
    if (expiresAt == 0) {
        return false; // No expiration set
    }
    
    return getCurrentTimestamp() + bufferSec >= expiresAt;
}

bool UserSession::isRefreshExpired() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    if (_session.provider == AuthProvider::Local) {
        xSemaphoreGive(_mutex);
        return false;
    }
    
    uint64_t expiresAt = _session.tokens.refreshExpiresAt;
    xSemaphoreGive(_mutex);
    
    if (expiresAt == 0) {
        return false;
    }
    
    return getCurrentTimestamp() >= expiresAt;
}

bool UserSession::isAdmin() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool admin = _session.user.role == UserRole::Admin || 
                 _session.user.role == UserRole::SuperAdmin;
    xSemaphoreGive(_mutex);
    return admin;
}

bool UserSession::isSuperAdmin() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool superAdmin = _session.user.role == UserRole::SuperAdmin;
    xSemaphoreGive(_mutex);
    return superAdmin;
}

bool UserSession::isConfirmed() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool confirmed = _session.user.isConfirmed;
    xSemaphoreGive(_mutex);
    return confirmed;
}

bool UserSession::needsRefresh() const {
    if (!isValid()) {
        return false;
    }
    
    // Local sessions don't need refresh
    if (getProvider() == AuthProvider::Local) {
        return false;
    }
    
    // Check if within refresh buffer
    return isExpired(300); // 5 minutes buffer
}

// ========== User Info ==========

const std::string& UserSession::getUserId() const {
    return _session.user.id;
}

const std::string& UserSession::getEmail() const {
    return _session.user.email;
}

const std::string& UserSession::getDisplayName() const {
    return _session.user.displayName;
}

UserRole UserSession::getRole() const {
    return _session.user.role;
}

const UserInfo& UserSession::getUser() const {
    return _session.user;
}

const std::map<std::string, std::string>& UserSession::getMetadata() const {
    return _session.user.metadata;
}

std::string UserSession::getMetadataValue(const std::string& key, 
                                          const std::string& defaultValue) const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto it = _session.user.metadata.find(key);
    std::string result = (it != _session.user.metadata.end()) ? it->second : defaultValue;
    xSemaphoreGive(_mutex);
    return result;
}

// ========== Tokens ==========

const std::string& UserSession::getAccessToken() const {
    return _session.tokens.accessToken;
}

const std::string& UserSession::getRefreshToken() const {
    return _session.tokens.refreshToken;
}

uint64_t UserSession::getExpiresAt() const {
    return _session.tokens.accessExpiresAt;
}

uint32_t UserSession::getSecondsUntilExpiry() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    uint64_t expiresAt = _session.tokens.accessExpiresAt;
    xSemaphoreGive(_mutex);
    
    if (expiresAt == 0) {
        return UINT32_MAX; // No expiration
    }
    
    uint64_t now = getCurrentTimestamp();
    if (now >= expiresAt) {
        return 0;
    }
    
    return static_cast<uint32_t>(expiresAt - now);
}

const AuthTokens& UserSession::getTokens() const {
    return _session.tokens;
}

// ========== Session Info ==========

uint64_t UserSession::getCreatedAt() const {
    return _session.createdAt;
}

AuthProvider UserSession::getProvider() const {
    return _session.provider;
}

bool UserSession::isPersistent() const {
    return _session.isPersistent;
}

const AuthSession& UserSession::getSession() const {
    return _session;
}

// ========== Supabase Access ==========

std::unique_ptr<Supabase::SupabaseClient> UserSession::getSupabaseClient() const {
    if (!hasSupabaseAccess()) {
        return nullptr;
    }
    
    xSemaphoreTake(_mutex, portMAX_DELAY);
    std::string token = _session.tokens.accessToken;
    xSemaphoreGive(_mutex);
    
    return Supabase::SupabaseClient::createWithAuth(token);
}

bool UserSession::hasSupabaseAccess() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool hasAccess = _session.provider == AuthProvider::Cloud && 
                     !_session.tokens.accessToken.empty() &&
                     _isValid;
    xSemaphoreGive(_mutex);
    return hasAccess;
}

// ========== Modification ==========

void UserSession::updateTokens(const AuthTokens& newTokens) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _session.tokens = newTokens;
    xSemaphoreGive(_mutex);
    
    ESP_LOGI(TAG, "Tokens updated for session: %s", _session.user.email.c_str());
    onRefreshed.trigger(this);
}

void UserSession::updateUser(const UserInfo& newUser) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    // Preserve password hash if not provided
    std::string oldPasswordHash = _session.user.passwordHash;
    _session.user = newUser;
    if (_session.user.passwordHash.empty()) {
        _session.user.passwordHash = oldPasswordHash;
    }
    
    xSemaphoreGive(_mutex);
    
    ESP_LOGI(TAG, "User info updated for session: %s", _session.user.email.c_str());
    onUserUpdated.trigger(this);
}

void UserSession::setMetadataValue(const std::string& key, const std::string& value) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _session.user.metadata[key] = value;
    xSemaphoreGive(_mutex);
}

void UserSession::invalidate() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _isValid = false;
    _session.tokens.clear();
    xSemaphoreGive(_mutex);
    
    ESP_LOGI(TAG, "Session invalidated: %s", _session.user.email.c_str());
    onInvalidated.trigger(this);
}

// ========== Private ==========

uint64_t UserSession::getCurrentTimestamp() const {
    return static_cast<uint64_t>(time(nullptr));
}
