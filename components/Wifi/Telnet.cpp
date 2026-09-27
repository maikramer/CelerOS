#include "Telnet.h"
#include <esp_log.h>
#include <cstdarg>
#include <algorithm> // For std::min

/**
 * @file Telnet.cpp
 * @brief Implementation of the Telnet class for Telnet server functionality over WiFi.
 */

Telnet::Telnet() : _isRunning(false) {}

Telnet::~Telnet() {
    stop();
}

ErrorCode Telnet::start(const std::string& ssid, const std::string& password, const IPAddress& ip, uint16_t port) {
    ESP_LOGI("Telnet", "Starting Telnet server...");

    ErrorCode err = _server.start(ssid, password, ip, port);
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("Telnet", "Failed to start server: %s", err.description().c_str());
        return err;
    }

    _isRunning = true;
    ESP_LOGI("Telnet", "Telnet server started on port %d", port);
    return CommonErrorCodes::None;
}

void Telnet::stop() {
    if (_isRunning) {
        _server.stop();
        _client.disconnect();
        _isRunning = false;
        ESP_LOGI("Telnet", "Telnet server stopped.");
    }
}

void Telnet::handle() {
    if (!_isRunning) {
        return;
    }

    if (_server.hasClient()) {
        ErrorCode acceptErr = _server.accept(_client);
        if (acceptErr != CommonErrorCodes::None) {
            acceptErr.log("Telnet", ESP_LOG_ERROR, "Failed to accept Telnet client");
            return;
        }
        ESP_LOGI("Telnet", "Client connected.");
        onClientConnected.trigger();
    }

    if (_client.connected()) {
        handleReceivedData();
    } else {
        onClientDisconnected.trigger();
    }
}

ErrorCode Telnet::send(const std::string& message) const {
    if (!_client.connected()) {
        return CommonErrorCodes::SocketClosed;
    }
    return sendRaw((const uint8_t*)message.c_str(), message.length());
}

ErrorCode Telnet::printf(const char* format, ...) const {
    if (!_client.connected()) {
        return CommonErrorCodes::SocketClosed;
    }

    char buffer[256]; // Adjust buffer size if needed
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    return sendRaw((const uint8_t*)buffer, strlen(buffer));
}

ErrorCode Telnet::gets(char* buffer, size_t bufferSize) const {
    if (!_client.connected()) {
        return CommonErrorCodes::SocketClosed;
    }

    size_t index = 0;
    while (index < bufferSize - 1) { // Leave space for null terminator
        size_t bytesRead;
        ErrorCode err = _client.read((uint8_t*)&buffer[index], 1, bytesRead);
        if (err != CommonErrorCodes::None) {
            return err;
        }

        if (buffer[index] == '\n' || buffer[index] == '\r') {
            buffer[index] = '\0'; // Replace newline/carriage return with null terminator
            return CommonErrorCodes::None;
        }

        index++;
    }

    buffer[bufferSize - 1] = '\0'; // Ensure null termination in case buffer is filled
    return CommonErrorCodes::OperationFailed; // Or a more specific "Buffer Full" error
}

ErrorCode Telnet::getChar(char& receivedChar) const {
    if (!_client.connected()) {
        return CommonErrorCodes::SocketClosed;
    }

    size_t bytesRead;
    ErrorCode err = _client.read((uint8_t*)&receivedChar, 1, bytesRead);
    if (err != CommonErrorCodes::None) {
        return err;
    }

    if (bytesRead == 0) {
        return CommonErrorCodes::SocketClosed;
    }

    return CommonErrorCodes::None;
}

void Telnet::handleReceivedData() {
    const size_t bufferSize = 128;
    uint8_t buffer[bufferSize];
    size_t bytesRead;

    ErrorCode err = _client.read(buffer, bufferSize, bytesRead);
    if (err != CommonErrorCodes::None) {
        err.log("Telnet", ESP_LOG_ERROR, "Failed to read data from Telnet client");
        return;
    }

    if (bytesRead > 0) {
        std::string message((char*)buffer, bytesRead);
        ESP_LOGI("Telnet", "Received: %s", message.c_str());
        onMessageReceived.trigger(message);
    }
}

ErrorCode Telnet::sendRaw(const uint8_t* data, size_t len) const {
    ErrorCode err = _client.write(data, len);
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("Telnet", "Failed to send data: %s", err.description().c_str());
        return err;
    }
    return CommonErrorCodes::None;
}