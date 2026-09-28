#include "BluetoothManager.h"
#include "JsonModels.h"
#include "ConnectionManager.h"
#include "BluetoothErrorCodes.h"
#include "GeneralErrorCodes.h"
#include <esp_random.h>
#include <esp_log.h>

/**
 * @file BluetoothManager.cpp
 * @brief Implementation of the BluetoothManager class, managing Bluetooth Low Energy functionality.
 */

std::list<std::string> BluetoothUtility::_uuidList;
Event<BluetoothManager*, BluetoothConnection*> BluetoothManager::onConnection;

BluetoothManager::BluetoothManager(Singleton<BluetoothManager>::token)
        : _publicService(nullptr), _privateService(nullptr), _publicTxCharacteristic(nullptr),
          _privateServiceUUID(""), _initialized(false)
{}

BluetoothManager::~BluetoothManager() {
    if (_initialized) {
        NimBLEDevice::deinit(true); // Deinitialize NimBLE stack and delete objects
        _initialized = false;
    }
}

ErrorCode BluetoothManager::initialize() {
    if (_initialized) {
        ESP_LOGW("BluetoothManager", "Bluetooth is already initialized.");
        return CommonErrorCodes::None;
    }

    ESP_LOGI("BluetoothManager", "Initializing Bluetooth...");

    // Initialize NimBLE device
    NimBLEDevice::init(DEVICE_NAME);

    // Set BLE power level 
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    // Create BLE server
    BleServer = NimBLEDevice::createServer();

    // Create public service
    _publicService = BleServer->createService(PUBLIC_SERVICE_UUID);
    if (!_publicService) {
        ESP_LOGE("BluetoothManager", "Failed to create public service.");
        return CommonErrorCodes::BluetoothServiceCreationFailed;
    }

    // Create private service with a unique UUID
    _privateServiceUUID = BluetoothUtility::generateUniqueId(false);
    _privateService = BleServer->createService(_privateServiceUUID.c_str());
    if (!_privateService) {
        ESP_LOGE("BluetoothManager", "Failed to create private service.");
        return CommonErrorCodes::BluetoothServiceCreationFailed;
    }

    // Create public TX characteristic (for general information)
    _publicTxCharacteristic = _publicService->createCharacteristic(
            PUBLIC_CHARACTERISTIC_TX_UUID, NIMBLE_PROPERTY::READ);
    if (!_publicTxCharacteristic) {
        ESP_LOGE("BluetoothManager", "Failed to create public TX characteristic.");
        return CommonErrorCodes::BluetoothCharacteristicCreationFailed;
    }

    // Set callbacks for server and characteristic events
    BleServer->setCallbacks(new ServerCallbacks());
    _publicTxCharacteristic->setCallbacks(new SendDataCallbacks());

    // Start services
    _publicService->start();
    _privateService->start();

    // Configure advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(PUBLIC_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x0);

    // Start advertising
    NimBLEDevice::startAdvertising();

    ESP_LOGI("BluetoothManager", "Bluetooth initialized successfully.");
    _initialized = true;
    return CommonErrorCodes::None;
}

std::string BluetoothManager::getPrivateServiceUUID() const {
    return _privateServiceUUID;
}

BluetoothConnection* BluetoothManager::createConnection() {
    auto* connection = new BluetoothConnection();
    ErrorCode err = connection->initialize();
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("BluetoothManager", "Failed to initialize BluetoothConnection: %s", err.description().c_str());
        delete connection;
        return nullptr;
    }

    ConnectionManager::addConnection(connection); // Assuming ConnectionManager handles the list
    return connection;
}

void BluetoothManager::ServerCallbacks::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    (void)pServer; // Parameter not used currently 

    NimBLEDevice::startAdvertising();
    ESP_LOGI("BluetoothManager", "Client connected: %s", connInfo.getAddress().toString().c_str());

    auto* connection = BluetoothManager::instance().createConnection();
    if (connection) {
        connection->connect(connInfo.getAddress().getNative()[5]); // Connect using last byte of address
        BluetoothManager::onConnection.trigger(&BluetoothManager::instance(), connection);
    } else {
        ESP_LOGE("BluetoothManager", "Failed to create a new BluetoothConnection.");
        // Handle error (maybe disconnect the client)
    }
}

void BluetoothManager::ServerCallbacks::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    (void)pServer; // Parameter not used currently 

    ESP_LOGI("BluetoothManager", "Client disconnected: %s, reason: %d - %s",
             connInfo.getAddress().toString().c_str(), reason, NimBLEUtils::returnCodeToString(reason));

    // Assuming ConnectionManager handles disconnection
    ConnectionManager::disconnect(connInfo.getAddress().getNative()[5]);
}

void BluetoothManager::SendDataCallbacks::onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    (void)pCharacteristic; // Parameter not used currently 

    ESP_LOGI("BluetoothManager", "Client read request from: %s", connInfo.getAddress().toString().c_str());

    auto* connection = ConnectionManager::getConnectionById(connInfo.getAddress().getNative()[5]);
    if (connection) {
        std::string json = connection->getConnectionInfoJson();
        ErrorCode err = connection->sendJson(json);
        if (err != CommonErrorCodes::None) {
            err.log("BluetoothManager", ESP_LOG_ERROR, "Failed to send JSON data.");
            // Handle error appropriately 
        }
    } else {
        ESP_LOGE("BluetoothManager", "Connection not found for ID: %d", connInfo.getAddress().getNative()[5]);
        // Handle error (maybe disconnect the client)
    }
}

std::string BluetoothUtility::generateUniqueId(bool isCharacteristic) {
    bool isUnique = false;
    std::string uuidStr;
    while (!isUnique) {
        std::stringstream uuid;
        if (isCharacteristic) {
            uuid << CHARACTERISTIC_UUID_STATE_BASE << esp_random() % 10 << esp_random() % 10;
        } else {
            uuid << SERVICE_UUID_BASE << esp_random() % 10 << esp_random() % 10;
        }
        uuidStr = uuid.str();
        ESP_LOGD("BluetoothUtility", "Generated UUID: %s", uuidStr.c_str());

        isUnique = true;
        for (const auto& item : _uuidList) {
            if (item == uuidStr) {
                isUnique = false;
                break;
            }
        }
    }

    _uuidList.push_back(uuidStr);
    return uuidStr;
}