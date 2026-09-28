#ifndef BLUETOOTHMANAGER_H
#define BLUETOOTHMANAGER_H

#include <NimBLEDevice.h>
#include "projectConfig.h"
#include "Singleton.h"
#include "Event.h"
#include "CommonErrorCodes.h"
#include "BluetoothConnection.h"

/**
 * @file BluetoothManager.h
 * @brief Defines the BluetoothManager class for handling the Bluetooth Low Energy (BLE) functionality of the ESP32.
 */

/**
 * @class BluetoothUtility
 * @brief Provides Utility functions for working with Bluetooth, such as generating unique UUIDs.
 */
class BluetoothUtility {
private:
    static std::list<std::string> _uuidList; /**< List of generated UUIDs to ensure uniqueness. */

public:
    /**
     * @brief Generates a unique UUID for either a service or a characteristic.
     *
     * @param isCharacteristic True if the UUID is for a characteristic, false if it's for a service.
     * @return A unique UUID string.
     */
    static std::string generateUniqueId(bool isCharacteristic);
};

/**
 * @class BluetoothManager
 * @brief Singleton class to manage the Bluetooth Low Energy (BLE) functionality of the device.
 *
 * This class is responsible for initializing the BLE stack, creating the BLE server,
 * defining services and characteristics, and handling BLE events.
 */
class BluetoothManager : public Singleton<BluetoothManager> {
private:
    NimBLEService* _publicService;  /**< Public service for general device information. */
    NimBLEService* _privateService; /**< Private service for application-specific data and commands. */
    NimBLECharacteristic* _publicTxCharacteristic; /**< Characteristic for broadcasting data on the public service. */
    std::string _privateServiceUUID; /**< The UUID of the private service. */

    bool _initialized; /**< Flag to track if the BluetoothManager has been initialized. */

public:
    /**
     * @brief Constructor. Private to enforce singleton pattern.
     */
    explicit BluetoothManager(token);

    /**
     * @brief Destructor.
     */
    ~BluetoothManager();

    /**
     * @brief Initializes the Bluetooth Low Energy stack and sets up the BLE server.
     * 
     * This method should be called once at the start of the application.
     * 
     * @return ErrorCode indicating success or failure of the initialization process.
     */
    ErrorCode initialize();

    /**
     * @brief Gets the UUID of the private service.
     *
     * @return The UUID of the private service as a string.
     */
    [[nodiscard]] std::string getPrivateServiceUUID() const;

    /**
     * @brief Creates a new BluetoothConnection object and adds it to the ConnectionManager.
     *
     * @return A pointer to the newly created BluetoothConnection.
     */
    BluetoothConnection* createConnection();

    /**
     * @brief Event triggered when a new BLE connection is established.
     *
     * The event handler will receive a pointer to the BluetoothManager object and
     * a pointer to the newly created BluetoothConnection object. 
     */
    static Event<BluetoothManager*, BluetoothConnection*> onConnection;

private:
    /**
     * @brief Callback class to handle server-level events from the NimBLE stack.
     */
    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        /**
         * @brief Called when a client connects to the server.
         *
         * @param pServer A pointer to the NimBLEServer object.
         * @param connInfo Information about the connection.
         */
        void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;

        /**
         * @brief Called when a client disconnects from the server.
         *
         * @param pServer A pointer to the NimBLEServer object.
         * @param connInfo Information about the connection.
         * @param reason The reason for the disconnection.
         */
        void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
    };

    /**
     * @brief Callback class to handle characteristic-level events for the public TX characteristic.
     */
    class SendDataCallbacks : public NimBLECharacteristicCallbacks {
    public:
        /**
         * @brief Called when a client reads the value of the public TX characteristic.
         *
         * @param pCharacteristic A pointer to the characteristic being read.
         * @param connInfo Information about the connection.
         */
        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
    };
};

#endif // BLUETOOTHMANAGER_H