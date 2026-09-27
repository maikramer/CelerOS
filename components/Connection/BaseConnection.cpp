#include "BaseConnection.h"
#include <esp_log.h>

/**
 * @file BaseConnection.cpp
 * @brief Implementation of the BaseConnection class, providing common functionality for communication connections.
 */

BaseConnection::BaseConnection() : _notificationNeeds(NotificationNeeds::NoSend) {}

ErrorCode BaseConnection::sendMessage(const std::string& message) const {
    if (!isConnected()) {
        return CommonErrorCodes::ConnectionClosed;
    }

    return sendRawData(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
}

ErrorCode BaseConnection::sendJson(const std::string& json) const {
    if (!isConnected()) {
        return CommonErrorCodes::ConnectionClosed;
    }

    return sendRawData(reinterpret_cast<const uint8_t*>(json.c_str()), json.length());
}