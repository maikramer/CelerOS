#include "WifiTelnet.h"
#include "GeneralErrorCodes.h"
#include <esp_log.h>
#include <esp_system.h>
#include <algorithm>
#include <sstream>
#include <utility>
#include <esp_chip_info.h>

/**
 * @file WifiTelnet.cpp
 * @brief Implementation for the WifiTelnet class, providing a Telnet server
 *        for remote device control with command management.
 */

WifiTelnet::WifiTelnet() : _isRunning(false) {}

WifiTelnet::~WifiTelnet() {
    stop();
}

ErrorCode WifiTelnet::start(const std::string& ssid, const std::string& password,
                            const IPAddress& ip, uint16_t port) {
    ESP_LOGI("WifiTelnet", "Starting WifiTelnet server...");

    // Check for errors during command registration
    ErrorCode err = registerCommand("help", [this](auto && PH1) { return handleHelpCommand(std::forward<decltype(PH1)>(PH1)); });
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("WifiTelnet", "Failed to register 'help' command: %s", err.description().c_str());
        return err;
    }

    err = registerCommand("ota", [this](auto && PH1) { return handleOtaCommand(std::forward<decltype(PH1)>(PH1)); });
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("WifiTelnet", "Failed to register 'ota' command: %s", err.description().c_str());
        return err;
    }

    err = registerCommand("info", [this](auto && PH1) { return handleInfoCommand(std::forward<decltype(PH1)>(PH1)); });
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("WifiTelnet", "Failed to register 'info' command: %s", err.description().c_str());
        return err;
    }

    err = _telnet.start(ssid, password, ip, port); // Start Telnet server
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("WifiTelnet", "Failed to start Telnet server: %s", err.description().c_str());
        return err;
    }

    _telnet.onMessageReceived.addHandler([this](const std::string& message) {
        auto commandLine = parseCommandLine(message);
        if (!commandLine.empty()) {
            ErrorCode execErr = executeCommand(commandLine[0], std::vector<std::string>(commandLine.begin() + 1, commandLine.end()));
            if (execErr != CommonErrorCodes::None) {
                execErr.log("WifiTelnet", ESP_LOG_WARN);
            }
        }
    });

    _isRunning = true;
    ESP_LOGI("WifiTelnet", "WifiTelnet server started on port %d", port);
    return CommonErrorCodes::None;
}
void WifiTelnet::stop() {
    if (_isRunning) {
        _telnet.stop();
        _isRunning = false;
        ESP_LOGI("WifiTelnet", "WifiTelnet server stopped.");
    }
}

ErrorCode WifiTelnet::registerCommand(const std::string& command,
                                      std::function<ErrorCode(const std::vector<std::string>&)> handler) {
    std::string lowerCaseCommand = command;
    std::transform(lowerCaseCommand.begin(), lowerCaseCommand.end(), lowerCaseCommand.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    if (_commandHandlers.find(lowerCaseCommand) != _commandHandlers.end()) {
        ESP_LOGE("WifiTelnet", "Command '%s' is already registered.", command.c_str());
        return CommonErrorCodes::OperationFailed;
    }

    _commandHandlers[lowerCaseCommand] = std::move(handler);
    return CommonErrorCodes::None;
}

void WifiTelnet::handle() {
    if (_isRunning) {
        _telnet.handle();
    }
}

std::vector<std::string> WifiTelnet::parseCommandLine(const std::string& commandLine) {
    std::vector<std::string> result;
    std::istringstream iss(commandLine);
    std::string token;

    while (std::getline(iss, token, ' ')) {
        result.push_back(token);
    }

    return result;
}

ErrorCode WifiTelnet::executeCommand(const std::string& command, const std::vector<std::string>& args) const {
    std::string lowerCaseCommand = command;
    std::transform(lowerCaseCommand.begin(), lowerCaseCommand.end(), lowerCaseCommand.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    auto it = _commandHandlers.find(lowerCaseCommand);
    if (it != _commandHandlers.end()) {
        return it->second(args);
    } else {
        printMessage("Unknown command: " + command + "\n");
        return CommonErrorCodes::OperationFailed;
    }
}

ErrorCode WifiTelnet::handleHelpCommand([[maybe_unused]] const std::vector<std::string>& args) const {
    printMessage("Available commands:\n");
    for (const auto &handler: _commandHandlers) {
        printMessage("  " + handler.first + "\n");
    }

    return CommonErrorCodes::None;
}

ErrorCode WifiTelnet::handleOtaCommand(const std::vector<std::string>& args) const {
    if (args.empty()) {
        printMessage("OTA update requires a URL.\nUsage: ota <url>\n");
        return CommonErrorCodes::ArgumentError;
    }

    const std::string& otaURL = args[0];
    printMessage("Starting OTA update from URL: " + otaURL + "...\n");

    _ota.startUpdate(otaURL);

    return CommonErrorCodes::None;
}

ErrorCode WifiTelnet::handleInfoCommand([[maybe_unused]] const std::vector<std::string>& args) const {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    std::stringstream infoStream;
    infoStream << "ESP32 System Information:\n";
    infoStream << "  Chip Model: " << chip_info.model << "\n";
    infoStream << "  Chip Revision: " << chip_info.revision << "\n";
    infoStream << "  CPU Cores: " << static_cast<int>(chip_info.cores) << "\n"; // Cast uint8_t to int for printing
    infoStream << "  Free Heap: " << esp_get_free_heap_size() << " bytes\n";
    // ... add more system info as needed ...

    printMessage(infoStream.str());
    return CommonErrorCodes::None;
}

void WifiTelnet::printMessage(const std::string& message) const {
   auto error = _telnet.send(message);
   error.log("WifiTelnet");
}