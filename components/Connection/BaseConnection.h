#ifndef BASECONNECTION_H
#define BASECONNECTION_H

#include <string>
#include <vector>
#include "ErrorCode.h"
#include "Event.h"
#include "CommonErrorCodes.h"
#include "JsonModels.h"

/**
 * @file BaseConnection.h
 * @brief Defines the BaseConnection class, providing a common interface for communication connections. 
 */

/**
 * @enum NotificationNeeds
 * @brief Defines the different states of notification needs for a connection.
 */
enum class NotificationNeeds {
    NoSend,       /**< No notification needs to be sent. */
    SendNormal,   /**< A normal notification needs to be sent. */
    SendImportant /**< An important notification needs to be sent. */
};

/**
 * @class BaseConnection
 * @brief Abstract base class for managing communication connections (e.g., WiFi, Bluetooth).
 */
class BaseConnection {
public:
    /**
     * @brief Default constructor.
     */
    BaseConnection();

    /**
     * @brief Destructor.
     */
    virtual ~BaseConnection() = default;

    /**
     * @brief Disconnects the connection.
     *
     * This method should be implemented by derived classes to handle the specific
     * disconnection logic for their respective connection types.
     */
    virtual void disconnect() = 0;

    /**
     * @brief Checks if the connection is currently active (connected).
     *
     * This method should be implemented by derived classes.
     *
     * @return True if the connection is active, false otherwise.
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Sends a raw byte array over the connection.
     *
     * @param data The data buffer to send.
     * @param length The length of the data buffer in bytes.
     * @return ErrorCode indicating success or failure of the send operation.
     */
    virtual ErrorCode sendRawData(const uint8_t* data, size_t length) const = 0;

    /**
     * @brief Sends a string message over the connection.
     *
     * @param message The string message to send.
     * @return ErrorCode indicating success or failure of the send operation.
     */
    ErrorCode sendMessage(const std::string& message) const;

    /**
     * @brief Sends a JSON string over the connection.
     *
     * @param json The JSON string to send.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode sendJson(const std::string& json) const;

    /**
     * @brief Sends an error message as a JSON string.
     *
     * This method uses the provided template type (TModel) to construct a JSON
     * object containing the error information. The template type must inherit from
     * `JsonModels::BaseJsonDataError`.
     *
     * @tparam TModel The type of JSON data model to use for sending the error.
     * @param errorCode The ErrorCode to send.
     * @return ErrorCode indicating success or failure of the send operation.
     */
    template<typename TModel>
    ErrorCode sendError(ErrorCode errorCode) const;

    /**
     * @brief Sends a list of data as a series of JSON strings.
     *
     * This method iterates through the provided `map` and serializes each key-value
     * pair into a JSON object using the provided template type (TModel). The template
     * type must inherit from `JsonModels::BaseListJsonDataBasic`.
     *
     * @tparam TModel The type of JSON data model to use for sending the list.
     * @tparam TKey The key type of the data map.
     * @tparam TValue The value type of the data map.
     * @param map The map containing the data to send.
     * @param firstItem (Optional) A pointer to a TModel object to send as the first item in the list.
     * @param lastItem (Optional) A pointer to a TModel object to send as the last item in the list.
     * @return ErrorCode indicating success or failure of the send operation.
     */
    template<typename TModel, typename TKey, typename TValue>
    ErrorCode sendList(const std::map<TKey, TValue>& map, TModel* firstItem = nullptr, TModel* lastItem = nullptr) const;

    /**
     * @brief Gets the current notification need state.
     *
     * @return The current notification need state.
     */
    [[nodiscard]] NotificationNeeds getNotificationNeeds() const;

    /**
     * @brief Sets the notification need state.
     *
     * @param needs The desired notification need state.
     */
    void setNotificationNeeds(NotificationNeeds needs);

    /**
     * @brief Event triggered when the connection is disconnected.
     */
    Event<BaseConnection*, void*> onDisconnect;

protected:
    NotificationNeeds _notificationNeeds; /**< The current notification needs of the connection. */
};

// Template Method Implementations
template<typename TModel>
ErrorCode BaseConnection::sendError(ErrorCode errorCode) const {
    static_assert(std::is_base_of<JsonModels::BaseJsonDataError, TModel>::value,
                  "Template type must inherit from JsonModels::BaseJsonDataError");

    if (errorCode != CommonErrorCodes::None) {
        ESP_LOGE(__FUNCTION__, "Error: %s - %s", errorCode.name().c_str(), errorCode.description().c_str());
    }

    TModel jsonData;
    jsonData.ErrorMessage = errorCode;
    if (std::is_base_of<JsonModels::BaseListJsonDataBasic, TModel>::value) {
        dynamic_cast<JsonModels::BaseListJsonDataBasic&>(jsonData).End = true;
    }

    return sendJson(jsonData.toJson());
}

template<typename TModel, typename TKey, typename TValue>
ErrorCode BaseConnection::sendList(const std::map<TKey, TValue>& map, TModel* firstItem, TModel* lastItem) const {
    static_assert(std::is_base_of<JsonModels::BaseListJsonDataBasic, TModel>::value,
                  "Template type must inherit from JsonModels::BaseListJsonDataBasic");

    if (map.empty()) {
        return sendError<TModel>(CommonErrorCodes::ListIsEmpty);
    }

    for (auto it = map.begin(); it != map.end(); ++it) {
        TModel jsonData;

        if (it == map.begin() && firstItem != nullptr) {
            jsonData = *firstItem;
        } else if (std::next(it) == map.end() && lastItem != nullptr) {
            jsonData = *lastItem;
        } else {
            jsonData.fromPair(it->first, it->second);

            if (it == map.begin()) {
                jsonData.Begin = true;
            }
            if (std::next(it) == map.end()) {
                jsonData.End = true;
            }
        }

        ErrorCode err = sendJson(jsonData.toJson());
        if (err != CommonErrorCodes::None) {
            return err;
        }
    }

    return CommonErrorCodes::None;
}

inline NotificationNeeds BaseConnection::getNotificationNeeds() const {
    return _notificationNeeds;
}

inline void BaseConnection::setNotificationNeeds(NotificationNeeds needs) {
    _notificationNeeds = needs;
}

#endif // BASECONNECTION_H