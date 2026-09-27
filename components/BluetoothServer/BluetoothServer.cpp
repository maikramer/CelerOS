#include "BluetoothServer.h"
#include <cstdint>
#include <esp_log.h>
#include "ConnectionManager.h"
#include "Utility.h"
#include "ConnectedUser.h"
#include "esp_random.h"

#define DELAY500MS (pdMS_TO_TICKS(500))
#define SEND_DATARATE DELAY500MS

/**
 * @file BluetoothServer.cpp
 * @brief Implementation of the BluetoothServer class for setting up and managing the BLE server.
 */

BluetoothServer::BluetoothServer(token)
        : _privateServiceUUID(""), _publicService(nullptr), _privateService(nullptr) {}

BluetoothServer::~BluetoothServer() {
    // Stop advertising and services
    NimBLEDevice::stopAdvertising();
    if (_publicService) {
        _publicService->stop();
    }
    if (_privateService) {
        _privateService->stop();
    }

    // Delete services
    if (_publicService) {
        delete _publicService;
    }
    if (_privateService) {
        delete _privateService;
    }

    // Delete NimBLE server
    if (BleServer) {
        NimBLEDevice::deinit(true); // This will delete the server and all its objects
    }
}

ErrorCode BluetoothServer::setup(const std::string& deviceName) {
    ESP_LOGI("BluetoothServer", "Setting up Bluetooth server...");

    // Initialize NimBLE device
    NimBLEDevice::init(deviceName);

    // Set BLE power level (adjust if needed)
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    // Create BLE server
    BleServer = NimBLEDevice::createServer();
    if (!BleServer) {
        ESP_LOGE("BluetoothServer", "Failed to create BLE server.");
        return CommonErrorCodes::BluetoothServerCreationFailed;
    }

    // Create public service
    _publicService = BleServer->createService(PUBLIC_SERVICE_UUID);
    if (!_publicService) {
        ESP_LOGE("BluetoothServer", "Failed to create public service.");
        return CommonErrorCodes::BluetoothServiceCreationFailed;
    }

    // Create private service
    _privateService = createPrivateService();
    if (!_privateService) {
        ESP_LOGE("BluetoothServer", "Failed to create private service.");
        return CommonErrorCodes::BluetoothServiceCreationFailed;
    }

    // Set server callbacks
    BleServer->setCallbacks(new ServerCallbacks());

    // Start the services
    _publicService->start();
    _privateService->start();

    // Start advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(PUBLIC_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x0);
    NimBLEDevice::startAdvertising();

    // Start the data sending task
    startSendDataTask();

    ESP_LOGI("BluetoothServer", "Bluetooth server setup completed.");
    return CommonErrorCodes::None;
}

NimBLECharacteristic* BluetoothServer::createPrivateWriteCharacteristic() {
    return createWriteCharacteristic(_privateService);
}

NimBLECharacteristic* BluetoothServer::createPrivateNotifyCharacteristic() {
    return createNotifyCharacteristic(_privateService);
}

NimBLECharacteristic* BluetoothServer::createWriteCharacteristic(NimBLEService* service) {
    if (service == nullptr) {
        service = _publicService;
    }

    auto* characteristic = service->createCharacteristic(BluetoothUtility::generateUniqueId(true).c_str(),
                                                         NIMBLE_PROPERTY::WRITE);
    if (!characteristic) {
        ESP_LOGE("BluetoothServer", "Failed to create write characteristic.");
    }

    return characteristic;
}

NimBLECharacteristic* BluetoothServer::createNotifyCharacteristic(NimBLEService* service) {
    if (service == nullptr) {
        service = _publicService;
    }

    auto* characteristic = service->createCharacteristic(BluetoothUtility::generateUniqueId(true).c_str(),
                                                         NIMBLE_PROPERTY::NOTIFY);
    if (!characteristic) {
        ESP_LOGE("BluetoothServer", "Failed to create notify characteristic.");
    }

    return characteristic;
}

NimBLEService* BluetoothServer::createPrivateService() {
    _privateServiceUUID = BluetoothUtility::generateUniqueId(false);
    auto* service = BleServer->createService(_privateServiceUUID.c_str());
    if (!service) {
        ESP_LOGE("BluetoothServer", "Failed to create private service.");
    }
    return service;
}

std::string BluetoothServer::getPrivateServiceUUID() const {
    return _privateServiceUUID;
}

void BluetoothServer::sendJson(NimBLECharacteristic* pCharacteristic, const std::string& json) {
    if (json.empty()) {
        ESP_LOGE("BluetoothServer", "Cannot send empty JSON.");
        return;
    }

    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(json.c_str()));
    size_t length = json.length();
    pCharacteristic->setValue(data, length);
    pCharacteristic->notify();

    ESP_LOGI("BluetoothServer", "Sent JSON: %s", json.c_str());
}

void BluetoothServer::ServerCallbacks::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    // Delegate connection handling to the ConnectionManager
    BluetoothManager::instance().onConnection.trigger(&BluetoothManager::instance(),
                                                      BluetoothManager::instance().createConnection());
}

void BluetoothServer::ServerCallbacks::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    // Delegate disconnection handling to the ConnectionManager
    ConnectionManager::disconnect(connInfo.getAddress().getNative()[5]);
}

void BluetoothServer::SendDataCallbacks::onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    auto address = connInfo.getAddress();
    auto *conn = ConnectionManager::getConnectionById(address.getNative()[5]);
    if (conn) {
        auto json = conn->getConnectionInfoJson();
        sendJson(pCharacteristic, json);
    } else {
        ESP_LOGE("BluetoothServer", "Connection not found for ID: %d", address.getNative()[5]);
    }
}


void BluetoothServer::startSendDataTask() {
    // Create a task to handle sending notification data
    xTaskCreate(
            [](void* parameter) {
                TickType_t xLastWakeTime = xTaskGetTickCount();
                for (;;) {
                    ConnectionManager::sendNotifications(); // Send notifications to connected clients
                    xTaskDelayUntil(&xLastWakeTime, SEND_DATARATE);
                }
            },
            "SendDataTask",
            4096,
            nullptr,
            HIGH_PRIORITY,
            nullptr);
}