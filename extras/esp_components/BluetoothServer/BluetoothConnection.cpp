#include "BluetoothConnection.h"
#include "BluetoothServer.h"
#include "Utility.h"
#include <esp_log.h>

#ifdef USER_MANAGEMENT_ENABLED

#include "ConnectedUser.h"
#include "UserManager.h"

#endif

#include "JsonModels.h"
#include "BluetoothErrorCodes.h"
#include "GeneralErrorCodes.h"

/**
 * @file BluetoothConnection.cpp
 * @brief Implementation of the BluetoothConnection class for managing individual Bluetooth connections.
 */

BluetoothConnection::BluetoothConnection()
        : BaseConnection(), _writeCharacteristic(nullptr), _notifyCharacteristic(nullptr), _sendMutex(nullptr),
          _isConnected(false), _connId(0)
#ifdef USER_MANAGEMENT_ENABLED
        , _user(nullptr)
#endif
{}

BluetoothConnection::~BluetoothConnection() {
    disconnect(); // NOLINT
    if (_sendMutex != nullptr) {
        vSemaphoreDelete(_sendMutex);
    }
}

ErrorCode BluetoothConnection::initialize() {
    _writeCharacteristic = BluetoothServer::instance().createPrivateWriteCharacteristic();
    if (!_writeCharacteristic) {
        ESP_LOGE(__FUNCTION__, "Failed to create write characteristic.");
        return CommonErrorCodes::BluetoothCharacteristicCreationFailed;
    }
    _writeCharacteristic->setCallbacks(this);

    _notifyCharacteristic = BluetoothServer::instance().createPrivateNotifyCharacteristic();
    if (!_notifyCharacteristic) {
        ESP_LOGE(__FUNCTION__, "Failed to create notify characteristic.");
        return CommonErrorCodes::BluetoothCharacteristicCreationFailed;
    }
    _notifyCharacteristic->setCallbacks(this);

    _sendMutex = xSemaphoreCreateMutex();
    if (_sendMutex == nullptr) {
        ESP_LOGE(__FUNCTION__, "Error creating mutex.");
        return CommonErrorCodes::MemoryAllocationFailed;
    }

    ESP_LOGI(__FUNCTION__, "BluetoothConnection initialized successfully.");
    return CommonErrorCodes::None;
}

void BluetoothConnection::connect(uint16_t connId) {
    _connId = connId;
    _isFree = false;
    _isConnected = true;
}

void BluetoothConnection::disconnect() {
    if (!_isConnected) {
        return; // Already disconnected
    }

    _isFree = true;
    _isConnected = false;
    _connId = 0;

#ifdef USER_MANAGEMENT_ENABLED
    if (_user != nullptr) {
        UserManager::disconnect(_user);
        _user = nullptr;
    }
#endif

    onDisconnect.trigger(this, nullptr);
}

bool BluetoothConnection::isFree() const {
    return _isFree;
}

uint16_t BluetoothConnection::getId() const {
    return _connId;
}

bool BluetoothConnection::isConnected() const {
    return _isConnected;
}

void BluetoothConnection::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) {
    std::string rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 0) {
        ESP_LOGI(__FUNCTION__, "Received data from connection ID %d: %s", _connId, rxValue.c_str());
        Commander::executeCommand(rxValue, this);
    }
}

std::string BluetoothConnection::getConnectionInfoJson() const {
    JsonModels::UuidInfoJsonData jsonData{};
    jsonData.ServiceUUID = BluetoothServer::instance().getPrivateServiceUUID();
    jsonData.WriteUUID = getWriteUUID();
    jsonData.NotifyUUID = getNotifyUUID();
    return jsonData.toJson();
}

ErrorCode BluetoothConnection::sendRawData(const uint8_t *data, size_t length) const {
    return sendRawData(data, length, true); // Default to notification
}

ErrorCode BluetoothConnection::sendData(const std::vector<uint8_t> &data, bool isNotification) {
    if (!_isConnected) {
        return CommonErrorCodes::ConnectionClosed;
    }
    return sendRawData(data.data(), data.size(), isNotification);
}

std::string BluetoothConnection::getWriteUUID() const {
    return _writeCharacteristic->getUUID().toString();
}

std::string BluetoothConnection::getNotifyUUID() const {
    return _notifyCharacteristic->getUUID().toString();
}

#ifdef USER_MANAGEMENT_ENABLED

void BluetoothConnection::setUser(ConnectedUser *user) {
    _user = user;
}

ConnectedUser *BluetoothConnection::getUser(bool canBeNull, bool canBeEmpty) {
    if (_user == nullptr && !canBeNull) {
        ESP_LOGE(__FUNCTION__, "Returning a null user (not allowed)!");
    } else if (_user != nullptr && _user->User.empty() && !canBeEmpty) {
        ESP_LOGE(__FUNCTION__, "Returning a user with an empty name (not allowed)!");
    }
    return _user;
}

void BluetoothConnection::logoff() {
    _user = nullptr;
}

#endif

// Private method
ErrorCode BluetoothConnection::sendRawData(const uint8_t *data, size_t length, bool isNotification) const {
    if (xSemaphoreTake(_sendMutex, 1000 / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGE(__FUNCTION__, "Failed to acquire mutex.");
        return CommonErrorCodes::Timeout;
    }

    _notifyCharacteristic->setValue(data, length);

    if (isNotification) {
        _notifyCharacteristic->notify();
    } else {
        _notifyCharacteristic->indicate();
    }

    xSemaphoreGive(_sendMutex);
    return CommonErrorCodes::None;
}

void BluetoothConnection::onStatus(NimBLECharacteristic *pCharacteristic, int code) {
    auto str = NimBLEUtils::returnCodeToString(code);
#ifdef LOG_STATUS_SENT
    ESP_LOGI("Status", "%s", str.c_str());
#endif

}
