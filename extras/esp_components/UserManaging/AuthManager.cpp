#include "AuthManager.h"
#include "esp_log.h"
#include "NVS.h"

namespace {
    constexpr const char* NVS_KEY_CURRENT_SESSION = "current_sess";
    constexpr uint32_t DEFAULT_SYNC_INTERVAL_SEC = 300;
    constexpr uint32_t REFRESH_CHECK_INTERVAL_SEC = 60;
}

AuthManager& AuthManager::instance() {
    static AuthManager instance;
    return instance;
}

AuthManager::AuthManager()
    : _state(AuthState::Idle)
    , _initialized(false)
    , _mutex(nullptr)
    , _syncTaskHandle(nullptr)
    , _refreshTaskHandle(nullptr)
    , _syncEnabled(false)
    , _syncIntervalSec(DEFAULT_SYNC_INTERVAL_SEC) {
    _mutex = xSemaphoreCreateMutex();
}

AuthManager::~AuthManager() {
    stopBackgroundTasks();
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
    }
}

ErrorCode AuthManager::init(AuthProvider provider) {
    AuthConfig config;
    config.defaultProvider = provider;
    return init(config);
}

ErrorCode AuthManager::init(const AuthConfig& config) {
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return AuthErrorCodes::None;
    }

    ESP_LOGI(TAG, "Initializing AuthManager with provider: %s", 
             authProviderToString(config.defaultProvider));

    _config = config;

    // Register error codes
    AuthErrorCodes::registerAll();

    // Initialize providers based on configuration
#if AUTH_LOCAL_ENABLED
    if (config.defaultProvider == AuthProvider::Local || 
        config.defaultProvider == AuthProvider::Both) {
        _localProvider = std::make_unique<LocalAuthProvider>();
        ErrorCode err = _localProvider->init();
        if (err != AuthErrorCodes::None) {
            ESP_LOGE(TAG, "Failed to initialize local provider: %s", err.description().c_str());
            return err;
        }
        ESP_LOGI(TAG, "Local provider initialized");
    }
#endif

#if AUTH_CLOUD_ENABLED
    if (config.defaultProvider == AuthProvider::Cloud || 
        config.defaultProvider == AuthProvider::Both) {
        _cloudProvider = std::make_unique<CloudAuthProvider>();
        // Cloud provider will be fully initialized when Supabase config is set
        ESP_LOGI(TAG, "Cloud provider created (pending Supabase config)");
    }
#endif

    // Load persisted sessions
    if (config.persistSessions) {
        loadPersistedSessions();
    }

    // Start background tasks if needed
    if (config.autoSyncEnabled || config.autoRefreshTokens) {
        startBackgroundTasks();
    }

    _initialized = true;
    setState(AuthState::LoggedOut);

    ESP_LOGI(TAG, "AuthManager initialized successfully");
    return AuthErrorCodes::None;
}

void AuthManager::setSupabaseConfig(const std::string& url, const std::string& anonKey) {
    SupabaseConfig config;
    config.url = url;
    config.anonKey = anonKey;
    setSupabaseConfig(config);
}

void AuthManager::setSupabaseConfig(const SupabaseConfig& config) {
    _supabaseConfig = config;
    
#if AUTH_CLOUD_ENABLED
    if (_cloudProvider) {
        _cloudProvider->init(config);
        ESP_LOGI(TAG, "Supabase configured: %s", config.url.c_str());
    }
#endif
}

// ========== Authentication ==========

void AuthManager::login(const std::string& email, const std::string& password, 
                        bool preferCloud) {
    AuthCredentials credentials(email, password);
    
    // Run login in background task
    auto* taskData = new std::tuple<AuthManager*, AuthCredentials, bool>(this, credentials, preferCloud);
    
    xTaskCreate([](void* pvParams) {
        auto* data = static_cast<std::tuple<AuthManager*, AuthCredentials, bool>*>(pvParams);
        auto* self = std::get<0>(*data);
        auto& creds = std::get<1>(*data);
        bool preferCloud = std::get<2>(*data);
        
        self->setState(AuthState::LoggingIn);
        
        IAuthProvider* provider = self->getProvider(preferCloud);
        if (provider == nullptr) {
            self->setState(AuthState::Error);
            self->onLoginFailed.trigger(creds.email, AuthErrorCodes::NotInitialized);
            delete data;
            vTaskDelete(nullptr);
            return;
        }
        
        AuthResult result = provider->login(creds);
        self->handleAuthResult(result, false);
        
        delete data;
        vTaskDelete(nullptr);
    }, "LoginTask", 8192, taskData, 5, nullptr);
}

void AuthManager::login(const AuthCredentials& credentials) {
    login(credentials.email, credentials.password, true);
}

AuthResult AuthManager::loginSync(const AuthCredentials& credentials) {
    setState(AuthState::LoggingIn);
    
    IAuthProvider* provider = getProvider(true);
    if (provider == nullptr) {
        setState(AuthState::Error);
        return AuthResult(AuthErrorCodes::NotInitialized);
    }
    
    AuthResult result = provider->login(credentials);
    handleAuthResult(result, false);
    
    return result;
}

void AuthManager::signup(const std::string& email, const std::string& password,
                         const std::string& displayName) {
    AuthCredentials credentials(email, password);
    credentials.displayName = displayName;
    signup(credentials, UserInfo());
}

void AuthManager::signup(const AuthCredentials& credentials, const UserInfo& profile) {
    auto* taskData = new std::tuple<AuthManager*, AuthCredentials, UserInfo>(this, credentials, profile);
    
    xTaskCreate([](void* pvParams) {
        auto* data = static_cast<std::tuple<AuthManager*, AuthCredentials, UserInfo>*>(pvParams);
        auto* self = std::get<0>(*data);
        auto& creds = std::get<1>(*data);
        auto& profile = std::get<2>(*data);
        
        self->setState(AuthState::SigningUp);
        
        IAuthProvider* provider = self->getProvider(true);
        if (provider == nullptr) {
            self->setState(AuthState::Error);
            self->onSignupFailed.trigger(creds.email, AuthErrorCodes::NotInitialized);
            delete data;
            vTaskDelete(nullptr);
            return;
        }
        
        AuthResult result = provider->signup(creds, profile);
        self->handleAuthResult(result, true);
        
        delete data;
        vTaskDelete(nullptr);
    }, "SignupTask", 8192, taskData, 5, nullptr);
}

AuthResult AuthManager::signupSync(const AuthCredentials& credentials, 
                                   const UserInfo& profile) {
    setState(AuthState::SigningUp);
    
    IAuthProvider* provider = getProvider(true);
    if (provider == nullptr) {
        setState(AuthState::Error);
        return AuthResult(AuthErrorCodes::NotInitialized);
    }
    
    AuthResult result = provider->signup(credentials, profile);
    handleAuthResult(result, true);
    
    return result;
}

void AuthManager::logout() {
    if (_currentSessionId.empty()) {
        return;
    }
    logout(_currentSessionId);
}

void AuthManager::logout(const std::string& userId) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    auto it = _sessions.find(userId);
    if (it != _sessions.end()) {
        UserSession* session = it->second.get();
        
        // Notify provider
        IAuthProvider* provider = getProvider(session->getProvider() == AuthProvider::Cloud);
        if (provider) {
            provider->logout(userId);
        }
        
        // Trigger event before removing
        onLogout.trigger(session);
        
        // Remove session
        if (_currentSessionId == userId) {
            _currentSessionId.clear();
        }
        _sessions.erase(it);
        
        ESP_LOGI(TAG, "User logged out: %s", userId.c_str());
    }
    
    if (_sessions.empty()) {
        setState(AuthState::LoggedOut);
    }
    
    xSemaphoreGive(_mutex);
    
    if (_config.persistSessions) {
        persistSessions();
    }
}

void AuthManager::logoutAll() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    for (auto& pair : _sessions) {
        onLogout.trigger(pair.second.get());
    }
    
    _sessions.clear();
    _currentSessionId.clear();
    
    xSemaphoreGive(_mutex);
    
    setState(AuthState::LoggedOut);
    ESP_LOGI(TAG, "All users logged out");
    
    if (_config.persistSessions) {
        persistSessions();
    }
}

// ========== Sessions ==========

UserSession* AuthManager::getCurrentSession() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserSession* session = nullptr;
    if (!_currentSessionId.empty()) {
        auto it = _sessions.find(_currentSessionId);
        if (it != _sessions.end()) {
            session = it->second.get();
        }
    }
    
    // If no current, return first valid session
    if (session == nullptr && !_sessions.empty()) {
        session = _sessions.begin()->second.get();
        _currentSessionId = session->getUserId();
    }
    
    xSemaphoreGive(_mutex);
    return session;
}

UserSession* AuthManager::getSession(const std::string& userId) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserSession* session = nullptr;
    auto it = _sessions.find(userId);
    if (it != _sessions.end()) {
        session = it->second.get();
    }
    
    xSemaphoreGive(_mutex);
    return session;
}

std::vector<UserSession*> AuthManager::getActiveSessions() {
    std::vector<UserSession*> sessions;
    
    xSemaphoreTake(_mutex, portMAX_DELAY);
    sessions.reserve(_sessions.size());
    for (auto& pair : _sessions) {
        sessions.push_back(pair.second.get());
    }
    xSemaphoreGive(_mutex);
    
    return sessions;
}

size_t AuthManager::getActiveSessionCount() const {
    return _sessions.size();
}

void AuthManager::setCurrentSession(const std::string& userId) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    if (_sessions.find(userId) != _sessions.end()) {
        _currentSessionId = userId;
        ESP_LOGI(TAG, "Current session set to: %s", userId.c_str());
    }
    
    xSemaphoreGive(_mutex);
}

// ========== State ==========

AuthState AuthManager::getState() const {
    return _state;
}

bool AuthManager::isLoggedIn() const {
    return _state == AuthState::LoggedIn && !_sessions.empty();
}

bool AuthManager::isLoggedIn(const std::string& userId) const {
    return _sessions.find(userId) != _sessions.end();
}

// ========== User Management ==========

ErrorCode AuthManager::getAllUsers(std::vector<UserInfo>& users) {
    IAuthProvider* provider = getProvider(true);
    if (provider == nullptr) {
        return AuthErrorCodes::NotInitialized;
    }
    return provider->getAllUsers(users);
}

ErrorCode AuthManager::getPendingUsers(std::vector<UserInfo>& users) {
    IAuthProvider* provider = getProvider(true);
    if (provider == nullptr) {
        return AuthErrorCodes::NotInitialized;
    }
    return provider->getPendingUsers(users);
}

ErrorCode AuthManager::confirmUser(const std::string& userId) {
    IAuthProvider* provider = getProvider(true);
    if (provider == nullptr) {
        return AuthErrorCodes::NotInitialized;
    }
    return provider->confirmUser(userId);
}

ErrorCode AuthManager::setUserRole(const std::string& userId, UserRole role) {
    IAuthProvider* provider = getProvider(true);
    if (provider == nullptr) {
        return AuthErrorCodes::NotInitialized;
    }
    return provider->setUserRole(userId, role);
}

ErrorCode AuthManager::setUserActive(const std::string& userId, bool active) {
    IAuthProvider* provider = getProvider(true);
    if (provider == nullptr) {
        return AuthErrorCodes::NotInitialized;
    }
    return provider->setUserActive(userId, active);
}

ErrorCode AuthManager::deleteUser(const std::string& userId) {
    // First logout if logged in
    logout(userId);
    
    IAuthProvider* provider = getProvider(true);
    if (provider == nullptr) {
        return AuthErrorCodes::NotInitialized;
    }
    return provider->deleteUser(userId);
}

ErrorCode AuthManager::changePassword(const std::string& oldPassword, 
                                      const std::string& newPassword) {
    UserSession* session = getCurrentSession();
    if (session == nullptr) {
        return AuthErrorCodes::NotLoggedIn;
    }
    
    IAuthProvider* provider = getProvider(session->getProvider() == AuthProvider::Cloud);
    if (provider == nullptr) {
        return AuthErrorCodes::NotInitialized;
    }
    
    return provider->changePassword(session->getUserId(), oldPassword, newPassword);
}

ErrorCode AuthManager::requestPasswordReset(const std::string& email) {
#if AUTH_CLOUD_ENABLED
    if (_cloudProvider && _cloudProvider->isReady()) {
        return _cloudProvider->requestPasswordReset(email);
    }
#endif
    return AuthErrorCodes::SupabaseNotConfigured;
}

// ========== Synchronization ==========

void AuthManager::syncWithCloud() {
#if AUTH_LOCAL_ENABLED && AUTH_CLOUD_ENABLED
    if (!_localProvider || !_cloudProvider || !_cloudProvider->isReady()) {
        ESP_LOGW(TAG, "Cannot sync: providers not ready");
        return;
    }

    setState(AuthState::Syncing);
    _syncStatus.inProgress = true;
    _syncStatus.usersUploaded = 0;
    _syncStatus.usersDownloaded = 0;
    _syncStatus.conflicts = 0;

    // Get local users
    std::map<std::string, UserInfo> localUsers;
    _localProvider->exportUsers(localUsers);

    // Get cloud users
    std::vector<UserInfo> cloudUsers;
    ErrorCode err = _cloudProvider->getAllUsers(cloudUsers);
    
    if (err != AuthErrorCodes::None) {
        _syncStatus.inProgress = false;
        _syncStatus.lastError = err.description();
        onSyncFailed.trigger(err);
        setState(AuthState::LoggedIn);
        return;
    }

    // Simple sync: cloud wins
    std::map<std::string, UserInfo> cloudUsersMap;
    for (const auto& user : cloudUsers) {
        cloudUsersMap[user.id] = user;
    }

    // Import cloud users to local
    _localProvider->importUsers(cloudUsersMap, true);
    _syncStatus.usersDownloaded = cloudUsers.size();

    _syncStatus.inProgress = false;
    _syncStatus.lastSyncAt = static_cast<uint64_t>(time(nullptr));

    ESP_LOGI(TAG, "Sync completed: %u downloaded", _syncStatus.usersDownloaded);
    onSyncComplete.trigger(_syncStatus);
    setState(AuthState::LoggedIn);
#else
    ESP_LOGW(TAG, "Sync not available (requires both local and cloud providers)");
#endif
}

void AuthManager::setAutoSync(bool enabled, uint32_t intervalSec) {
    _syncEnabled = enabled;
    _syncIntervalSec = intervalSec;
    _config.autoSyncEnabled = enabled;
    _config.autoSyncIntervalSec = intervalSec;
}

void AuthManager::setAutoRefresh(bool enabled) {
    _config.autoRefreshTokens = enabled;
}

void AuthManager::setMaxSessions(uint32_t max) {
    _config.maxConcurrentSessions = max;
}

void AuthManager::setPersistSessions(bool persist) {
    _config.persistSessions = persist;
}

// ========== Private Methods ==========

void AuthManager::setState(AuthState newState) {
    if (_state != newState) {
        AuthState oldState = _state;
        _state = newState;
        ESP_LOGI(TAG, "State changed: %s -> %s", 
                 authStateToString(oldState), authStateToString(newState));
        onStateChanged.trigger(oldState, newState);
    }
}

IAuthProvider* AuthManager::getProvider(bool preferCloud) {
#if AUTH_CLOUD_ENABLED && AUTH_LOCAL_ENABLED
    if (_config.defaultProvider == AuthProvider::Both) {
        if (preferCloud && _cloudProvider && _cloudProvider->isReady()) {
            return _cloudProvider.get();
        }
        if (_localProvider && _localProvider->isReady()) {
            return _localProvider.get();
        }
        if (_cloudProvider && _cloudProvider->isReady()) {
            return _cloudProvider.get();
        }
    }
#endif

#if AUTH_CLOUD_ENABLED
    if (_config.defaultProvider == AuthProvider::Cloud && _cloudProvider) {
        return _cloudProvider.get();
    }
#endif

#if AUTH_LOCAL_ENABLED
    if (_config.defaultProvider == AuthProvider::Local && _localProvider) {
        return _localProvider.get();
    }
    // Fallback to local
    if (_localProvider) {
        return _localProvider.get();
    }
#endif

    return nullptr;
}

UserSession* AuthManager::createSession(const AuthSession& authSession) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    // Check max sessions
    if (_sessions.size() >= _config.maxConcurrentSessions) {
        // Remove oldest session
        if (!_sessions.empty()) {
            auto oldest = _sessions.begin();
            onLogout.trigger(oldest->second.get());
            _sessions.erase(oldest);
        }
    }
    
    auto session = std::make_unique<UserSession>(authSession);
    UserSession* sessionPtr = session.get();
    
    // Setup session events
    session->onExpired.addHandler([this](UserSession* s) {
        onSessionExpired.trigger(s);
    });
    
    session->onRefreshed.addHandler([this](UserSession* s) {
        onTokenRefreshed.trigger(s);
    });
    
    _sessions[authSession.user.id] = std::move(session);
    _currentSessionId = authSession.user.id;
    
    xSemaphoreGive(_mutex);
    
    return sessionPtr;
}

void AuthManager::removeSession(const std::string& userId) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    _sessions.erase(userId);
    if (_currentSessionId == userId) {
        _currentSessionId = _sessions.empty() ? "" : _sessions.begin()->first;
    }
    
    xSemaphoreGive(_mutex);
}

void AuthManager::handleAuthResult(const AuthResult& result, bool isSignup) {
    if (result.isSuccess()) {
        UserSession* session = createSession(result.session);
        setState(AuthState::LoggedIn);
        
        if (isSignup) {
            onSignup.trigger(session);
        } else {
            onLogin.trigger(session);
        }
        
        if (_config.persistSessions) {
            persistSessions();
        }
        
        ESP_LOGI(TAG, "%s successful: %s", 
                 isSignup ? "Signup" : "Login",
                 result.session.user.email.c_str());
    } else {
        setState(AuthState::Error);
        
        if (isSignup) {
            onSignupFailed.trigger(result.session.user.email, result.error);
        } else {
            onLoginFailed.trigger(result.session.user.email, result.error);
        }
        
        // Return to logged out state after error
        vTaskDelay(pdMS_TO_TICKS(100));
        if (_sessions.empty()) {
            setState(AuthState::LoggedOut);
        } else {
            setState(AuthState::LoggedIn);
        }
    }
}

ErrorCode AuthManager::loadPersistedSessions() {
    // Load current session ID
    std::string sessionId;
    ErrorCode err = NVS::readValue(AuthConstants::NVS_NAMESPACE, 
                                   NVS_KEY_CURRENT_SESSION, sessionId);
    if (err == CommonErrorCodes::None && !sessionId.empty()) {
        _currentSessionId = sessionId;
        ESP_LOGI(TAG, "Loaded persisted session: %s", sessionId.c_str());
    }
    return AuthErrorCodes::None;
}

ErrorCode AuthManager::persistSessions() {
    if (_currentSessionId.empty()) {
        NVS::eraseData(AuthConstants::NVS_NAMESPACE, NVS_KEY_CURRENT_SESSION);
    } else {
        NVS::storeValue(AuthConstants::NVS_NAMESPACE, 
                       NVS_KEY_CURRENT_SESSION, _currentSessionId, true);
    }
    return AuthErrorCodes::None;
}

void AuthManager::startBackgroundTasks() {
    if (_config.autoSyncEnabled && _syncTaskHandle == nullptr) {
        xTaskCreate(syncTaskFunc, "AuthSync", 4096, this, 5, &_syncTaskHandle);
    }
    
    if (_config.autoRefreshTokens && _refreshTaskHandle == nullptr) {
        xTaskCreate(refreshTaskFunc, "AuthRefresh", 4096, this, 5, &_refreshTaskHandle);
    }
}

void AuthManager::stopBackgroundTasks() {
    if (_syncTaskHandle != nullptr) {
        vTaskDelete(_syncTaskHandle);
        _syncTaskHandle = nullptr;
    }
    
    if (_refreshTaskHandle != nullptr) {
        vTaskDelete(_refreshTaskHandle);
        _refreshTaskHandle = nullptr;
    }
}

void AuthManager::syncTaskFunc(void* pvParameters) {
    auto* self = static_cast<AuthManager*>(pvParameters);
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(self->_syncIntervalSec * 1000));
        
        if (self->_syncEnabled && self->isLoggedIn()) {
            self->syncWithCloud();
        }
    }
}

void AuthManager::refreshTaskFunc(void* pvParameters) {
    auto* self = static_cast<AuthManager*>(pvParameters);
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(REFRESH_CHECK_INTERVAL_SEC * 1000));
        
        if (!self->_config.autoRefreshTokens) {
            continue;
        }
        
        // Check each session for refresh
        auto sessions = self->getActiveSessions();
        for (UserSession* session : sessions) {
            if (session->needsRefresh() && session->getProvider() == AuthProvider::Cloud) {
#if AUTH_CLOUD_ENABLED
                if (self->_cloudProvider && self->_cloudProvider->isReady()) {
                    AuthResult result = self->_cloudProvider->refreshToken(
                        session->getRefreshToken());
                    if (result.isSuccess()) {
                        session->updateTokens(result.session.tokens);
                    }
                }
#endif
            }
        }
    }
}
