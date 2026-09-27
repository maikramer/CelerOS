#ifndef BLUETOOTHSERVER_H
#define BLUETOOTHSERVER_H

#include <NimBLEDevice.h>
#include "Commander.h"
#include <Enums.h>
#include <list>
#include <projectConfig.h>
#include "Singleton.h"
#include "CommonErrorCodes.h" // For error handling
#include "BluetoothConnection.h"

/**
 * @file BluetoothServer.h
 * @brief Defines the BluetoothServer class for creating and managing a Bluetooth Low Energy (BLE) server.
 */

/**
 * @class BluetoothServer
 * @brief Singleton class representing the Bluetooth Low Energy server. 
 *
 * Handles BLE server setup, advertising, connection management, and data transmission.
 */
class BluetoothServer : public Singleton<BluetoothServer> {
public:
    NimBLEServer* BleServer = nullptr; /**< Pointer to the NimBLE server instance. */

    /**
     * @brief Constructor. Private to enforce singleton pattern.
     *
     * @param token The token used for singleton instantiation.
     */
    explicit BluetoothServer(token);

    /**
     * @brief Destructor.
     */
    ~BluetoothServer();

    /**
     * @brief Sets up the Bluetooth server.
     *
     * @param deviceName The name of the Bluetooth device to advertise.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode setup(const std::string& deviceName);

    /**
     * @brief Creates a new write characteristic on the private service.
     *
     * @return A pointer to the newly created characteristic, or nullptr on error.
     */
    NimBLECharacteristic* createPrivateWriteCharacteristic();

    /**
     * @brief Creates a new notify characteristic on the private service.
     *
     * @return A pointer to the newly created characteristic, or nullptr on error.
     */
    NimBLECharacteristic* createPrivateNotifyCharacteristic();

    /**
     * @brief Creates a new write characteristic on the specified service.
     *
     * @param service A pointer to the service to add the characteristic to. 
     *                Defaults to the public service if nullptr is passed.
     * @return A pointer to the newly created characteristic, or nullptr on error. 
     */
    NimBLECharacteristic* createWriteCharacteristic(NimBLEService* service = nullptr);

    /**
     * @brief Creates a new notify characteristic on the specified service.
     *
     * @param service A pointer to the service to add the characteristic to. 
     *                Defaults to the public service if nullptr is passed.
     * @return A pointer to the newly created characteristic, or nullptr on error. 
     */
    NimBLECharacteristic* createNotifyCharacteristic(NimBLEService* service = nullptr);

    /**
     * @brief Creates a new private service with a unique UUID.
     *
     * @return A pointer to the newly created service, or nullptr on error.
     */
    NimBLEService* createPrivateService();

    /**
     * @brief Gets the UUID of the private service.
     *
     * @return The UUID of the private service as a string.
     */
    [[nodiscard]] std::string getPrivateServiceUUID() const;

private:
    std::string _privateServiceUUID; /**< The UUID of the private service. */
    NimBLEService* _publicService;  /**< Public service for general device information. */
    NimBLEService* _privateService; /**< Private service for application-specific data and commands. */

    /**
     * @brief Callback class to handle BLE server events.
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
     * @brief Callback class to handle characteristic events on the public TX characteristic.
     */
    class SendDataCallbacks : public NimBLECharacteristicCallbacks {
    public:
        /**
         * @brief Called when a client reads the value of the characteristic.
         *
         * @param pCharacteristic A pointer to the characteristic being read.
         * @param connInfo Information about the connection.
         */
        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
    };

    /**
     * @brief Sends a JSON string as a notification on the specified characteristic.
     *
     * @param pCharacteristic The characteristic to send the notification on.
     * @param json The JSON string to send.
     */
    static void sendJson(NimBLECharacteristic* pCharacteristic, const std::string& json);

    /**
     * @brief Starts a task to periodically send data to connected clients.
     */
    void startSendDataTask();
};

#endif //BLUETOOTHSERVER_H