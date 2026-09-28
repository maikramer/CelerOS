#ifndef BLUETOOTHCONNECTION_H
#define BLUETOOTHCONNECTION_H

#include <string>
#include <NimBLECharacteristic.h>
#include <functional>
#include "BaseConnection.h" // Inherit from BaseConnection
#include "JsonModels.h"
#include "projectConfig.h"

#ifdef USER_MANAGEMENT_ENABLED
#include "ConnectedUser.h"
#endif

/**
 * @file BluetoothConnection.h
 * @brief This file defines the BluetoothConnection class for managing individual Bluetooth connections.
 */

/**
 * @class BluetoothConnection
 * @brief Represents a single Bluetooth connection, handling data transmission, notifications, and user management (optional).
 *
 * This class inherits from BaseConnection to provide a consistent interface for communication connections.
 */
class BluetoothConnection : public BaseConnection, public NimBLECharacteristicCallbacks {
public:
    /**
     * @brief Constructor.
     */
    BluetoothConnection();

    /**
     * @brief Destructor.
     */
    ~BluetoothConnection() override; // Override the base class destructor

    /**
     * @brief Initializes the BluetoothConnection, creating the necessary characteristics and mutex.
     *
     * @return ErrorCode indicating success or failure of the initialization process.
     */
    ErrorCode initialize();

    /**
     * @brief Connects the BluetoothConnection to a specific connection ID.
     *
     * @param connId The connection ID to associate with this connection.
     */
    void connect(uint16_t connId);

    /**
     * @brief Disconnects the BluetoothConnection, freeing resources and notifying listeners.
     */
    void disconnect() override;

    /**
     * @brief Checks if the BluetoothConnection is currently free (not associated with a connection ID).
     *
     * @return True if the connection is free, false otherwise.
     */
    [[nodiscard]] bool isFree() const;

    /**
     * @brief Gets the connection ID associated with this BluetoothConnection.
     *
     * @return The connection ID.
     */
    [[nodiscard]] uint16_t getId() const;

    /**
     * @brief Handles a write event on the associated write characteristic.
     *
     * @param pCharacteristic A pointer to the characteristic that received the write event.
     * @param connInfo Information about the connection that initiated the write.
     */
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;

    /**
     * @brief Handles a status change event on the associated notify characteristic.
     *
     * @param pCharacteristic A pointer to the characteristic whose status changed.
     * @param code The status code.
     */
    void onStatus(NimBLECharacteristic* pCharacteristic, int code) override;

    /**
     * @brief Sends data as a notification or indication.
     *
     * @param data The data to send.
     * @param isNotification If true, send as a notification; otherwise, send as an indication.
     * @return ErrorCode indicating success or failure of the send operation.
     */
    ErrorCode sendData(const std::vector<uint8_t>& data, bool isNotification);

    /**
     * @brief Gets the UUID of the write characteristic.
     *
     * @return The UUID of the write characteristic.
     */
    [[nodiscard]] std::string getWriteUUID() const;

    /**
     * @brief Gets the UUID of the notify characteristic.
     *
     * @return The UUID of the notify characteristic.
     */
    [[nodiscard]] std::string getNotifyUUID() const;

    /**
     * @brief Gets the JSON string containing connection information.
     *
     * @return The JSON string with connection info (UUIDs of the service and characteristics).
     */
    [[nodiscard]] std::string getConnectionInfoJson() const;

#ifdef USER_MANAGEMENT_ENABLED
    /**
     * @brief Sets the ConnectedUser associated with this connection.
     *
     * @param user A pointer to the ConnectedUser object.
     */
    void setUser(ConnectedUser* user);

    /**
     * @brief Gets the ConnectedUser associated with this connection.
     *
     * @param canBeNull If true, the function can return nullptr if no user is assigned.
     * @param canBeEmpty If true, the function can return a ConnectedUser with an empty user name.
     * @return A pointer to the ConnectedUser, or nullptr if no user is assigned (and `canBeNull` is true).
     */
    ConnectedUser* getUser(bool canBeNull = true, bool canBeEmpty = true);

    /**
     * @brief Logs off the user, clearing the user data and updating notification needs.
     */
    void logoff();
#endif

private:
    NimBLECharacteristic* _writeCharacteristic; /**< Characteristic for receiving write commands. */
    NimBLECharacteristic* _notifyCharacteristic; /**< Characteristic for sending notifications. */
    SemaphoreHandle_t _sendMutex; /**< Mutex to protect concurrent access to send operations. */
    bool _isFree{};
    bool _isConnected; /**< Flag indicating if the connection is active. */
    uint16_t _connId; /**< The connection ID. */

#ifdef USER_MANAGEMENT_ENABLED
    ConnectedUser* _user; /**< Pointer to the associated ConnectedUser object (if user management is enabled). */
#endif

    /**
     * @brief Sends a raw byte array over the notification characteristic.
     *
     * @param data The data to send.
     * @param length The length of the data.
     * @param isNotification If true, send as a notification; otherwise, send as an indication.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode sendRawData(const uint8_t* data, size_t length, bool isNotification) const;

    ErrorCode sendRawData(const uint8_t *data, size_t length) const override;

    [[nodiscard]] bool isConnected() const override;
};

#endif // BLUETOOTHCONNECTION_H