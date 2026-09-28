#include "CommandCode.h"
#include <esp_log.h>

/**
 * @file CommandCode.cpp
 * @brief Implementation of the CommandCode class for managing command codes.
 */

std::map<uint8_t, CommandCode> CommandCode::commandDatabase;
bool CommandCode::initialized = false;

CommandCode::CommandCode() : _value(0), _name("Invalid Command") {}

CommandCode::CommandCode(uint8_t value, const std::string& name)
        : _value(value), _name(name) {}

uint8_t CommandCode::value() const {
    return _value;
}

const std::string& CommandCode::name() const {
    return _name;
}

bool CommandCode::isValid() const {
    return !_name.empty();
}

bool CommandCode::operator==(const CommandCode& other) const {
    return _value == other._value;
}

bool CommandCode::operator!=(const CommandCode& other) const {
    return !(*this == other);
}

std::ostream& operator<<(std::ostream& os, const CommandCode& commandCode) {
    os << commandCode.name();
    return os;
}

CommandCode CommandCode::define(uint8_t value, const std::string& name) {
    if (commandDatabase.find(value) != commandDatabase.end()) {
        ESP_LOGE("CommandCode", "Command code with value %u already defined: %s", value, name.c_str());
        return CommandCode(); // Return invalid command code 
    }

    CommandCode newCommandCode(value, name);
    commandDatabase[value] = newCommandCode;
    return newCommandCode;
}

CommandCode CommandCode::get(uint8_t value) {
    auto it = commandDatabase.find(value);
    if (it != commandDatabase.end()) {
        return it->second;
    } else {
        ESP_LOGW("CommandCode", "Command code %u not found in database.", value);
        return CommandCode();
    }
}

void CommandCode::initialize() {
    if (initialized) {
        return;
    }

    // Define your command codes here
    define(100, "SetAdminInfo");
    define(101, "Login");
    define(102, "Logoff");
    define(103, "SignUp");
    define(104, "GetUsersWaiting");
    define(105, "ApproveUser");
    define(106, "ClearUsers");
    define(107, "ScanForWifi");
    // ... Add more command codes as needed ...

    initialized = true;
}