#ifndef TELNET_H
#define TELNET_H

#include <string>
#include "WifiServer.h"
#include "WifiClient.h"
#include "Event.h"
#include "CommonErrorCodes.h"

/**
 * @file Telnet.h
 * @brief This file defines the Telnet class, which provides a Telnet server over WiFi.
 */

/**
 * @class Telnet
 * @brief Represents a Telnet server, allowing remote clients to connect and interact with the device.
 */
class Telnet {
public:
    /**
     * @brief Constructor.
     */
    Telnet();

    /**
     * @brief Destructor. Stops the Telnet server and closes the connection.
     */
    ~Telnet();

    /**
     * @brief Initializes and starts the Telnet server.
     *
     * @param ssid The SSID (name) of the WiFi access point to create for the Telnet server.
     * @param password The password for the WiFi access point (optional, can be nullptr for an open AP).
     * @param ip The IP address to assign to the access point.
     * @param port The port number to listen for Telnet connections (default: 23).
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode start(const std::string& ssid, const std::string& password,
                    const IPAddress& ip, uint16_t port = 23);

    /**
     * @brief Stops the Telnet server and closes the connection.
     */
    void stop();

    /**
     * @brief Handles incoming Telnet client connections and data.
     *
     * This method should be called periodically in your main loop.
     */
    void handle();

    /**
     * @brief Sends a string message to the connected Telnet client.
     *
     * @param message The message string to send.
     * @return ErrorCode indicating success or failure.
     */
    [[nodiscard]] ErrorCode send(const std::string& message) const;

    /**
     * @brief Sends a formatted string to the connected Telnet client (like printf).
     *
     * @param format A format string (printf-style).
     * @param ... Variable arguments to be formatted.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode printf(const char* format, ...) const;

    /**
     * @brief Reads a line of text from the Telnet client (like gets).
     *
     * @param buffer A buffer to store the received line of text.
     * @param bufferSize The size of the buffer in bytes.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode gets(char* buffer, size_t bufferSize) const;

    /**
     * @brief Reads a single character from the Telnet client (like getchar).
     *
     * @param receivedChar A reference to a char to store the received character.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode getChar(char& receivedChar) const;

    /**
     * @brief Event triggered when a client connects to the Telnet server.
     */
    Event<> onClientConnected;

    /**
     * @brief Event triggered when a message is received from the Telnet client.
     */
    Event<const std::string&> onMessageReceived;

    /**
     * @brief Event triggered when a client disconnects from the Telnet server.
     */
    Event<> onClientDisconnected;

private:
    WifiServer _server; /**< Instance of WifiServer to handle network connections. */
    WifiClient _client; /**< Instance of WifiClient to handle communication with the connected client. */
    bool _isRunning;  /**< Flag to indicate whether the Telnet server is running. */

    /**
     * @brief Internal method to handle data received from the Telnet client.
     */
    void handleReceivedData();

    /**
     * @brief Helper function to send a raw byte array to the Telnet client.
     *
     * @param data Pointer to the byte array.
     * @param len The length of the byte array.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode sendRaw(const uint8_t* data, size_t len) const;
};

#endif // TELNET_H