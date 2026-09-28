#include "WifiClient.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <esp_log.h>

/**
 * @file WifiClient.cpp
 * @brief Implementation of the WifiClient class for TCP client connections.
 */

WifiClient::WifiClient() : _socket(-1), _isConnected(false) {}

WifiClient::~WifiClient() {
    disconnect();
}

ErrorCode WifiClient::connect(const IPAddress& ip, uint16_t port) {
    return connectInternal(ip.get().addr, port);
}

ErrorCode WifiClient::connect(const std::string& hostname, uint16_t port) {
    struct hostent* he = gethostbyname(hostname.c_str());
    if (he == nullptr) {
        ESP_LOGE("WifiClient", "Failed to resolve hostname: %s", hostname.c_str());
        return CommonErrorCodes::HostUnreachable;
    }

    uint32_t ip = *(uint32_t*)he->h_addr_list[0];
    return connectInternal(ip, port);
}

void WifiClient::disconnect() {
    if (_isConnected) {
        close(_socket);
        _isConnected = false;
    }
}

bool WifiClient::connected() const {
    return _isConnected;
}

ErrorCode WifiClient::write(const uint8_t* data, size_t size) const {
    if (!_isConnected) {
        return CommonErrorCodes::SocketClosed;
    }

    ssize_t bytesWritten = send(_socket, data, size, 0);
    if (bytesWritten < 0) {
        ESP_LOGE("WifiClient", "Failed to write data: %s", strerror(errno));
        return CommonErrorCodes::SocketSendFailed;
    }

    return CommonErrorCodes::None;
}

ErrorCode WifiClient::read(uint8_t* buffer, size_t size, size_t& bytesRead) const {
    if (!_isConnected) {
        return CommonErrorCodes::SocketClosed;
    }

    ssize_t result = recv(_socket, buffer, size, 0);
    if (result < 0) {
        ESP_LOGE("WifiClient", "Failed to read data: %s", strerror(errno));
        return CommonErrorCodes::SocketReceiveFailed;
    }

    bytesRead = result; // Set the number of bytes read
    return CommonErrorCodes::None;
}

ErrorCode WifiClient::available(int& availableBytes) const {
    if (!_isConnected) {
        return CommonErrorCodes::SocketClosed;
    }

    int result = ioctl(_socket, FIONREAD, &availableBytes);
    if (result < 0) {
        ESP_LOGE("WifiClient", "Failed to check available bytes: %s", strerror(errno));
        return CommonErrorCodes::OperationFailed;
    }
    return CommonErrorCodes::None;
}


void WifiClient::setSocket(int socket) {
    _socket = socket;
}

void WifiClient::setConnected(bool isConnected) {
    _isConnected = isConnected;
}

int WifiClient::getSocket() const {
    return _socket;
}

// Internal connection logic
ErrorCode WifiClient::connectInternal(uint32_t ip, uint16_t port) {
    if (_isConnected) {
        disconnect();
    }

    _socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_socket < 0) {
        ESP_LOGE("WifiClient", "Failed to create socket: %s", strerror(errno));
        return CommonErrorCodes::SocketCreationFailed;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = ip;

    if (::connect(_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        ESP_LOGE("WifiClient", "Failed to connect to server: %s", strerror(errno));
        close(_socket);
        return CommonErrorCodes::SocketConnectFailed;
    }

    _isConnected = true;
    return CommonErrorCodes::None;
}