#include "LocalAuthProvider.h"

#if AUTH_LOCAL_ENABLED

#include <algorithm>
#include <cstring>
#include <ctime>
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "NVS.h"

namespace {
    constexpr const char* NVS_KEY_USER_PREFIX = "user_";
    constexpr const char* NVS_KEY_USER_INDEX = "user_idx";
    constexpr uint32_t SALT_LENGTH = 16;
}

LocalAuthProvider::LocalAuthProvider()
    : _mutex(nullptr) {
    _mutex = xSemaphoreCreateMutex();
}

LocalAuthProvider::~LocalAuthProvider() {
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
    }
}

ErrorCode LocalAuthProvider::init() {
    if (_initialized) {
        return AuthErrorCodes::None;
    }

    ESP_LOGI(TAG, "Initializing LocalAuthProvider...");

    // Load existing users from NVS
    ErrorCode err = loadUsersFromNvs();
    if (err != AuthErrorCodes::None && 
        err != CommonErrorCodes::FileNotFound && 
        err != CommonErrorCodes::KeyNotFound) {
        ESP_LOGE(TAG, "Failed to load users: %s", err.description().c_str());
        return err;
    }

    _initialized = true;
    ESP_LOGI(TAG, "LocalAuthProvider initialized. %zu users loaded.", _users.size());
    return AuthErrorCodes::None;
}

bool LocalAuthProvider::isReady() const {
    return _initialized;
}

AuthResult LocalAuthProvider::login(const AuthCredentials& credentials) {
    if (!_initialized) {
        return AuthResult(AuthErrorCodes::NotInitialized);
    }

    if (credentials.email.empty() || credentials.password.empty()) {
        return AuthResult(AuthErrorCodes::InvalidCredentials);
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Check if account is locked
    auto lockIt = _lockoutUntil.find(credentials.email);
    if (lockIt != _lockoutUntil.end()) {
        if (getCurrentTimestamp() < lockIt->second) {
            xSemaphoreGive(_mutex);
            ESP_LOGW(TAG, "Account locked: %s", credentials.email.c_str());
            return AuthResult(AuthErrorCodes::AccountLocked);
        } else {
            // Lockout expired, reset
            _lockoutUntil.erase(lockIt);
            _failedAttempts.erase(credentials.email);
        }
    }

    // Find user
    UserInfo* user = findUserByEmail(credentials.email);
    if (user == nullptr) {
        xSemaphoreGive(_mutex);
        ESP_LOGW(TAG, "User not found: %s", credentials.email.c_str());
        return AuthResult(AuthErrorCodes::UserNotFound);
    }

    // Verify password
    if (!verifyPassword(credentials.email, credentials.password)) {
        // Track failed attempts
        _failedAttempts[credentials.email]++;
        if (_failedAttempts[credentials.email] >= AuthConstants::MAX_LOGIN_RETRIES) {
            _lockoutUntil[credentials.email] = getCurrentTimestamp() + AuthConstants::LOGIN_LOCKOUT_SEC;
            ESP_LOGW(TAG, "Account locked due to failed attempts: %s", credentials.email.c_str());
        }
        xSemaphoreGive(_mutex);
        return AuthResult(AuthErrorCodes::WrongPassword);
    }

    // Clear failed attempts on successful login
    _failedAttempts.erase(credentials.email);

    // Check if account is active
    if (!user->isActive) {
        xSemaphoreGive(_mutex);
        return AuthResult(AuthErrorCodes::AccountDisabled);
    }

    // Check if account needs confirmation (non-admin only)
    if (!user->isAdmin() && !user->isConfirmed) {
        xSemaphoreGive(_mutex);
        AuthResult result(AuthErrorCodes::AccountNotConfirmed);
        result.needsConfirmation = true;
        return result;
    }

    // Update last login time
    user->lastLoginAt = getCurrentTimestamp();
    saveUserToNvs(*user);

    // Create session
    AuthSession session = createSession(*user);
    
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "User logged in: %s", credentials.email.c_str());
    
    AuthResult result(session);
    onAuthComplete.trigger(result);
    return result;
}

AuthResult LocalAuthProvider::signup(const AuthCredentials& credentials, 
                                     const UserInfo& profile) {
    if (!_initialized) {
        return AuthResult(AuthErrorCodes::NotInitialized);
    }

    // Validate email
    if (!isValidEmail(credentials.email)) {
        return AuthResult(AuthErrorCodes::InvalidEmail);
    }

    // Validate password
    ErrorCode pwErr = validatePassword(credentials.password);
    if (pwErr != AuthErrorCodes::None) {
        return AuthResult(pwErr);
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Check if email already exists
    if (findUserByEmail(credentials.email) != nullptr) {
        xSemaphoreGive(_mutex);
        return AuthResult(AuthErrorCodes::EmailAlreadyExists);
    }

    // Create new user
    UserInfo newUser = profile;
    newUser.id = generateUserId();
    newUser.email = credentials.email;
    newUser.displayName = credentials.displayName.empty() ? 
                          credentials.email.substr(0, credentials.email.find('@')) :
                          credentials.displayName;
    newUser.passwordHash = hashPassword(credentials.password);
    newUser.createdAt = getCurrentTimestamp();
    newUser.updatedAt = newUser.createdAt;
    newUser.isActive = true;

    // First user becomes admin
    if (_users.empty()) {
        newUser.role = UserRole::Admin;
        newUser.isConfirmed = true;
        ESP_LOGI(TAG, "First user registered as admin: %s", credentials.email.c_str());
    } else {
        newUser.role = UserRole::User;
        newUser.isConfirmed = false;
    }

    // Save user
    _users[newUser.id] = newUser;
    _emailToId[newUser.email] = newUser.id;
    
    ErrorCode err = saveUserToNvs(newUser);
    if (err != AuthErrorCodes::None) {
        // Rollback
        _users.erase(newUser.id);
        _emailToId.erase(newUser.email);
        xSemaphoreGive(_mutex);
        return AuthResult(err);
    }

    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "User registered: %s (id: %s)", newUser.email.c_str(), newUser.id.c_str());

    AuthResult result;
    result.session = createSession(newUser);
    result.needsConfirmation = !newUser.isConfirmed;
    
    if (result.needsConfirmation) {
        result.error = AuthErrorCodes::AccountNotConfirmed;
        result.message = "Conta criada. Aguarde aprovação do administrador.";
    }

    onAuthComplete.trigger(result);
    return result;
}

ErrorCode LocalAuthProvider::logout(const std::string& userId) {
    // Local provider doesn't maintain active sessions
    // Just return success
    ESP_LOGI(TAG, "User logged out: %s", userId.empty() ? "current" : userId.c_str());
    return AuthErrorCodes::None;
}

ErrorCode LocalAuthProvider::getProfile(const std::string& userId, UserInfo& profile) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserInfo* user = findUserById(userId);
    if (user == nullptr) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::UserNotFound;
    }

    profile = *user;
    // Don't expose password hash
    profile.passwordHash.clear();
    
    xSemaphoreGive(_mutex);
    return AuthErrorCodes::None;
}

ErrorCode LocalAuthProvider::updateProfile(const std::string& userId, const UserInfo& profile) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserInfo* user = findUserById(userId);
    if (user == nullptr) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::UserNotFound;
    }

    // Update allowed fields
    if (!profile.displayName.empty()) {
        user->displayName = profile.displayName;
    }
    user->metadata = profile.metadata;
    user->updatedAt = getCurrentTimestamp();

    ErrorCode err = saveUserToNvs(*user);
    
    xSemaphoreGive(_mutex);

    if (err == AuthErrorCodes::None) {
        onProfileUpdated.trigger(userId, *user);
    }

    return err;
}

ErrorCode LocalAuthProvider::deleteUser(const std::string& userId) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    auto it = _users.find(userId);
    if (it == _users.end()) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::UserNotFound;
    }

    std::string email = it->second.email;
    _emailToId.erase(email);
    _users.erase(it);
    
    ErrorCode err = removeUserFromNvs(userId);
    
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "User deleted: %s", userId.c_str());
    return err;
}

bool LocalAuthProvider::userExists(const std::string& email) {
    if (!_initialized) {
        return false;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool exists = findUserByEmail(email) != nullptr;
    xSemaphoreGive(_mutex);
    
    return exists;
}

ErrorCode LocalAuthProvider::changePassword(const std::string& userId,
                                            const std::string& oldPassword,
                                            const std::string& newPassword) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    // Validate new password
    ErrorCode pwErr = validatePassword(newPassword);
    if (pwErr != AuthErrorCodes::None) {
        return pwErr;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserInfo* user = findUserById(userId);
    if (user == nullptr) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::UserNotFound;
    }

    // Verify old password
    if (!verifyPassword(user->email, oldPassword)) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::WrongPassword;
    }

    // Update password
    user->passwordHash = hashPassword(newPassword);
    user->updatedAt = getCurrentTimestamp();
    
    ErrorCode err = saveUserToNvs(*user);
    
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "Password changed for user: %s", userId.c_str());
    return err;
}

ErrorCode LocalAuthProvider::getAllUsers(std::vector<UserInfo>& users) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    users.clear();
    users.reserve(_users.size());
    
    for (const auto& pair : _users) {
        UserInfo user = pair.second;
        user.passwordHash.clear(); // Don't expose
        users.push_back(user);
    }
    
    xSemaphoreGive(_mutex);
    return AuthErrorCodes::None;
}

ErrorCode LocalAuthProvider::getPendingUsers(std::vector<UserInfo>& users) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    users.clear();
    for (const auto& pair : _users) {
        if (!pair.second.isConfirmed && !pair.second.isAdmin()) {
            UserInfo user = pair.second;
            user.passwordHash.clear();
            users.push_back(user);
        }
    }
    
    xSemaphoreGive(_mutex);
    return AuthErrorCodes::None;
}

ErrorCode LocalAuthProvider::confirmUser(const std::string& userId) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserInfo* user = findUserById(userId);
    if (user == nullptr) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::UserNotFound;
    }

    user->isConfirmed = true;
    user->updatedAt = getCurrentTimestamp();
    
    ErrorCode err = saveUserToNvs(*user);
    
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "User confirmed: %s", userId.c_str());
    return err;
}

ErrorCode LocalAuthProvider::setUserRole(const std::string& userId, UserRole role) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserInfo* user = findUserById(userId);
    if (user == nullptr) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::UserNotFound;
    }

    user->role = role;
    user->updatedAt = getCurrentTimestamp();
    
    ErrorCode err = saveUserToNvs(*user);
    
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "User role changed: %s -> %s", userId.c_str(), userRoleToString(role));
    return err;
}

ErrorCode LocalAuthProvider::setUserActive(const std::string& userId, bool active) {
    if (!_initialized) {
        return AuthErrorCodes::NotInitialized;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserInfo* user = findUserById(userId);
    if (user == nullptr) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::UserNotFound;
    }

    user->isActive = active;
    user->updatedAt = getCurrentTimestamp();
    
    ErrorCode err = saveUserToNvs(*user);
    
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "User %s: %s", active ? "enabled" : "disabled", userId.c_str());
    return err;
}

bool LocalAuthProvider::hasAdmin() const {
    for (const auto& pair : _users) {
        if (pair.second.isAdmin()) {
            return true;
        }
    }
    return false;
}

size_t LocalAuthProvider::getUserCount() const {
    return _users.size();
}

ErrorCode LocalAuthProvider::clearAllUsers() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    _users.clear();
    _emailToId.clear();
    _failedAttempts.clear();
    _lockoutUntil.clear();
    
    // Clear from NVS
    ErrorCode err = NVS::eraseData(AuthConstants::NVS_NAMESPACE, NVS_KEY_USER_INDEX);
    
    xSemaphoreGive(_mutex);

    ESP_LOGW(TAG, "All users cleared");
    return err;
}

ErrorCode LocalAuthProvider::exportUsers(std::map<std::string, UserInfo>& users) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    users = _users;
    xSemaphoreGive(_mutex);
    return AuthErrorCodes::None;
}

ErrorCode LocalAuthProvider::importUsers(const std::map<std::string, UserInfo>& users, 
                                         bool overwrite) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    for (const auto& pair : users) {
        auto existingIt = _users.find(pair.first);
        if (existingIt == _users.end() || overwrite) {
            _users[pair.first] = pair.second;
            _emailToId[pair.second.email] = pair.first;
        }
    }
    
    ErrorCode err = saveUsersToNvs();
    
    xSemaphoreGive(_mutex);
    return err;
}

ErrorCode LocalAuthProvider::getUserByEmail(const std::string& email, UserInfo& user) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    UserInfo* found = findUserByEmail(email);
    if (found == nullptr) {
        xSemaphoreGive(_mutex);
        return AuthErrorCodes::UserNotFound;
    }
    
    user = *found;
    user.passwordHash.clear();
    
    xSemaphoreGive(_mutex);
    return AuthErrorCodes::None;
}

bool LocalAuthProvider::verifyPassword(const std::string& email, const std::string& password) {
    UserInfo* user = findUserByEmail(email);
    if (user == nullptr) {
        return false;
    }

    // Extract salt from stored hash (format: salt$hash)
    size_t delimPos = user->passwordHash.find('$');
    if (delimPos == std::string::npos) {
        return false;
    }

    std::string salt = user->passwordHash.substr(0, delimPos);
    std::string computedHash = hashPassword(password, salt);
    
    return computedHash == user->passwordHash;
}

// ========== Private Methods ==========

ErrorCode LocalAuthProvider::loadUsersFromNvs() {
    std::string indexData;
    ErrorCode err = NVS::readValue(AuthConstants::NVS_NAMESPACE, NVS_KEY_USER_INDEX, indexData);
    
    if (err != CommonErrorCodes::None) {
        return err;
    }

    // Index format: userId1;userId2;userId3;...
    _users.clear();
    _emailToId.clear();
    
    size_t start = 0;
    size_t end = indexData.find(';');
    
    while (end != std::string::npos || start < indexData.size()) {
        if (end == std::string::npos) {
            end = indexData.size();
        }
        
        std::string userId = indexData.substr(start, end - start);
        if (!userId.empty()) {
            std::string userKey = std::string(NVS_KEY_USER_PREFIX) + userId;
            std::string userData;
            
            err = NVS::readValue(AuthConstants::NVS_NAMESPACE, userKey, userData);
            if (err == CommonErrorCodes::None && userData.size() >= sizeof(UserInfo)) {
                // Deserialize user (simplified - in production use proper serialization)
                UserInfo user;
                // Parse JSON or binary format
                // For now, store as blob
                if (userData.size() == sizeof(UserInfo)) {
                    std::memcpy(&user, userData.data(), sizeof(UserInfo));
                    _users[user.id] = user;
                    _emailToId[user.email] = user.id;
                }
            }
        }
        
        start = end + 1;
        end = indexData.find(';', start);
    }

    return AuthErrorCodes::None;
}

ErrorCode LocalAuthProvider::saveUsersToNvs() {
    // Build index
    std::string index;
    for (const auto& pair : _users) {
        if (!index.empty()) {
            index += ";";
        }
        index += pair.first;
        
        // Save individual user
        saveUserToNvs(pair.second);
    }
    
    return NVS::storeValue(AuthConstants::NVS_NAMESPACE, NVS_KEY_USER_INDEX, index, true);
}

ErrorCode LocalAuthProvider::saveUserToNvs(const UserInfo& user) {
    std::string userKey = std::string(NVS_KEY_USER_PREFIX) + user.id;
    
    // Serialize user (simplified - store as blob)
    std::string userData(reinterpret_cast<const char*>(&user), sizeof(UserInfo));
    
    return NVS::storeValue(AuthConstants::NVS_NAMESPACE, userKey, userData, true);
}

ErrorCode LocalAuthProvider::removeUserFromNvs(const std::string& userId) {
    std::string userKey = std::string(NVS_KEY_USER_PREFIX) + userId;
    
    ErrorCode err = NVS::eraseData(AuthConstants::NVS_NAMESPACE, userKey);
    
    // Update index
    if (err == CommonErrorCodes::None || err == CommonErrorCodes::KeyNotFound) {
        saveUsersToNvs();
    }
    
    return AuthErrorCodes::None;
}

std::string LocalAuthProvider::hashPassword(const std::string& password, const std::string& salt) {
    std::string actualSalt = salt.empty() ? generateSalt() : salt;
    std::string toHash = actualSalt + password;
    
    unsigned char hash[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(toHash.c_str()), 
                   toHash.length(), hash, 0);
    
    // Convert to hex string
    char hexHash[65];
    for (int i = 0; i < 32; i++) {
        sprintf(hexHash + i * 2, "%02x", hash[i]);
    }
    hexHash[64] = '\0';
    
    return actualSalt + "$" + std::string(hexHash);
}

std::string LocalAuthProvider::generateSalt() {
    char salt[SALT_LENGTH + 1];
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    
    for (uint32_t i = 0; i < SALT_LENGTH; i++) {
        salt[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    salt[SALT_LENGTH] = '\0';
    
    return std::string(salt);
}

std::string LocalAuthProvider::generateUserId() {
    // Generate a simple UUID-like ID
    char uuid[37];
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    uint32_t r3 = esp_random();
    uint32_t r4 = esp_random();
    
    sprintf(uuid, "%08x-%04x-%04x-%04x-%04x%08x",
            r1,
            (r2 >> 16) & 0xFFFF,
            ((r2 & 0xFFFF) & 0x0FFF) | 0x4000,
            ((r3 >> 16) & 0x3FFF) | 0x8000,
            r3 & 0xFFFF,
            r4);
    
    return std::string(uuid);
}

bool LocalAuthProvider::isValidEmail(const std::string& email) const {
    if (email.length() < 5 || email.length() > 254) {
        return false;
    }
    
    size_t atPos = email.find('@');
    if (atPos == std::string::npos || atPos == 0 || atPos == email.length() - 1) {
        return false;
    }
    
    size_t dotPos = email.rfind('.');
    if (dotPos == std::string::npos || dotPos < atPos || dotPos == email.length() - 1) {
        return false;
    }
    
    return true;
}

ErrorCode LocalAuthProvider::validatePassword(const std::string& password) const {
    if (password.length() < AuthConstants::MIN_PASSWORD_LENGTH) {
        return AuthErrorCodes::PasswordTooShort;
    }
    if (password.length() > AuthConstants::MAX_PASSWORD_LENGTH) {
        return AuthErrorCodes::PasswordTooLong;
    }
    return AuthErrorCodes::None;
}

uint64_t LocalAuthProvider::getCurrentTimestamp() const {
    return static_cast<uint64_t>(time(nullptr));
}

AuthSession LocalAuthProvider::createSession(const UserInfo& user) {
    AuthSession session;
    session.user = user;
    session.user.passwordHash.clear(); // Don't include in session
    session.provider = AuthProvider::Local;
    session.createdAt = getCurrentTimestamp();
    session.isPersistent = true;
    
    // Local provider doesn't use JWT tokens
    // Access token is just for identification
    session.tokens.accessToken = user.id;
    session.tokens.accessExpiresAt = 0; // Never expires for local
    
    return session;
}

UserInfo* LocalAuthProvider::findUserByEmail(const std::string& email) {
    auto idIt = _emailToId.find(email);
    if (idIt == _emailToId.end()) {
        return nullptr;
    }
    
    auto userIt = _users.find(idIt->second);
    if (userIt == _users.end()) {
        return nullptr;
    }
    
    return &userIt->second;
}

UserInfo* LocalAuthProvider::findUserById(const std::string& userId) {
    auto it = _users.find(userId);
    if (it == _users.end()) {
        return nullptr;
    }
    return &it->second;
}

#endif // AUTH_LOCAL_ENABLED
