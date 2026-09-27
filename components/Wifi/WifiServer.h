#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#include <string>
#include <esp_err.h>
#include <lwip/sockets.h>
#include "WifiClient.h"
#include "IPAddress.h"
#include "esp_wifi.h"
#include "ErrorCode.h"

/**
 * @file WifiServer.h
 * @brief This file defines the WifiServer class for creating a TCP server.
 */

/**
 * @class WifiServer
 * @brief Represents a TCP server, listening for incoming client connections.
 */
class WifiServer {
public:
    /**
     * @brief Constructor. Initializes the WifiServer with a closed socket.
     */
    WifiServer();

    /**
     * @brief Destructor. Stops the server and closes the socket.
     */
    ~WifiServer();

    /**
     * @brief Starts the server in access point (AP) mode.
     *
     * @param ssid The SSID (name) for the access point.
     * @param password The password for the access point (optional, can be nullptr for an open AP).
     * @param ip The IP address for the access point.
     * @param port The port to listen on.
     * @return ErrorCode indicating the success or failure of starting the server.
     */
    ErrorCode start(const std::string& ssid, const std::string& password,
                    const IPAddress& ip, uint16_t port);

    /**
     * @brief Stops the server and closes the socket.
     */
    void stop();

    /**
     * @brief Checks if a client has connected.
     *
     * @return True if a client is waiting to be accepted, false otherwise.
     */
    [[nodiscard]] bool hasClient() const;

    /**
     * @brief Accepts an incoming client connection.
     *
     * @return ErrorCode indicating success or failure. If successful, the accepted
     *         WifiClient is returned through the `client` output parameter.
     */
    ErrorCode accept(WifiClient& client) const;

private:
    int _serverSocket; /**< Socket file descriptor for the server. */
    sockaddr_in _serverAddress{}; /**< Server address structure. */
    bool _isRunning; /**< Flag indicating if the server is running. */

    /**
     * @brief Configures and starts the ESP32 as an access point.
     *
     * @param ssid The SSID for the access point.
     * @param password The password for the access point (optional).
     * @param ip The IP address to assign to the access point.
     * @return ErrorCode indicating success or failure.
     */
    static ErrorCode configureAccessPoint(const std::string& ssid, const std::string& password, const IPAddress& ip);
};

#endif // WIFI_SERVER_H