#include "ErrorCode.h"
#include <esp_log.h>

#include <utility>

// Use "Construct On First Use" idiom to avoid Static Initialization Order Fiasco
std::map<std::string, ErrorCode>& ErrorCode::getErrorDatabase() {
    static std::map<std::string, ErrorCode> database;
    return database;
}

bool ErrorCode::initialized = false;

ErrorCode::ErrorCode() : _description("Invalid Error Code"), _type(ErrorCodeType::General) {}

ErrorCode::ErrorCode(std::string  name, std::string  description, ErrorCodeType type)
        : _name(std::move(name)), _description(std::move(description)), _type(type) {}

const std::string& ErrorCode::description() const {
    return _description;
}

const std::string& ErrorCode::name() const {
    return _name;
}

ErrorCodeType ErrorCode::type() const {
    return _type;
}

bool ErrorCode::isValid() const {
    return !_name.empty();
}

bool ErrorCode::operator==(const ErrorCode& other) const {
    return _name == other._name;
}

bool ErrorCode::operator!=(const ErrorCode& other) const {
    return !(*this == other);
}

std::ostream& operator<<(std::ostream& os, const ErrorCode& errorCode) {
    os << errorCode.description();
    return os;
}

ErrorCode ErrorCode::define(const std::string& name, const std::string& description, ErrorCodeType type) {
    auto& database = getErrorDatabase();
    if (database.find(name) != database.end()) {
        ESP_LOGE("ErrorCode", "Error code with name '%s' already defined.", name.c_str());
        return {}; // Return an invalid error code
    }

    ErrorCode newErrorCode(name, description, type);
    database[name] = newErrorCode;
    return newErrorCode;
}

ErrorCode ErrorCode::get(const std::string& name) {
    auto& database = getErrorDatabase();
    auto it = database.find(name);
    if (it != database.end()) {
        return it->second;
    } else {
        ESP_LOGW("ErrorCode", "Error code '%s' not found in database.", name.c_str());
        return {}; // Return an invalid error code
    }
}

void ErrorCode::initialize() {
    if (initialized) {
        return;
    }

    // Common error codes are defined in CommonErrorCodes.cpp,
    // so we don't need to define them here.

    initialized = true;
}

void ErrorCode::log(const char* tag, esp_log_level_t level, const std::string& additionalMessage) const {
    if (!isValid()) {
        ESP_LOGW(tag, "Invalid ErrorCode: Attempting to log an invalid error code.");
        return;
    }

    std::string logMessage = "[" + name() + "] " + description();

    if (!additionalMessage.empty()) {
        logMessage += " - " + additionalMessage;
    }

    switch (level) {
        case ESP_LOG_ERROR:
            ESP_LOGE(tag, "%s", logMessage.c_str());
            break;
        case ESP_LOG_WARN:
            ESP_LOGW(tag, "%s", logMessage.c_str());
            break;
        case ESP_LOG_INFO:
            ESP_LOGI(tag, "%s", logMessage.c_str());
            break;
        case ESP_LOG_DEBUG:
            ESP_LOGD(tag, "%s", logMessage.c_str());
            break;
        case ESP_LOG_VERBOSE:
            ESP_LOGV(tag, "%s", logMessage.c_str());
            break;
        default:
            ESP_LOGW(tag, "Invalid log level provided: %d. Using ESP_LOG_ERROR.", level);
            ESP_LOGE(tag, "%s", logMessage.c_str());
            break;
    }
}