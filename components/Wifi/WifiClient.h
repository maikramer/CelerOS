#ifndef WIFI_CLIENT_H
#define WIFI_CLIENT_H

#include <string>
#include <cstring>
#include <esp_err.h>
#include "IPAddress.h"
#include "CommonErrorCodes.h"
#include "ErrorCode.h"

/**
 * @file WifiClient.h
 * @brief This file defines the WifiClient class, providing an interface for TCP client connections.
 */

/**
 * @class WifiClient
 * @brief Represents a TCP client, allowing for network communication with a remote server.
 */
class WifiClient {
public:
    /**
     * @brief Constructor. Initializes the WifiClient object with a closed socket.
     */
    WifiClient();

    /**
     * @brief Destructor. Closes the socket if it's open.
     */
    ~WifiClient();

    /**
     * @brief Connects to a remote host specified by an IP address and port.
     *
     * @param ip The IP address of the remote host.
     * @param port The port to connect to on the remote host.
     * @return ErrorCode indicating success or failure of the connection.
     */
    ErrorCode connect(const IPAddress& ip, uint16_t port);

    /**
     * @brief Connects to a remote host specified by a hostname and port.
     *
     * @param hostname The hostname of the remote host (e.g., "example.com").
     * @param port The port to connect to on the remote host.
     * @return ErrorCode indicating success or failure of the connection.
     */
    ErrorCode connect(const std::string& hostname, uint16_t port);

    /**
     * @brief Disconnects from the remote host.
     */
    void disconnect();

    /**
     * @brief Checks if the client is currently connected.
     *
     * @return True if connected, false otherwise.
     */
    [[nodiscard]] bool connected() const;

    /**
     * @brief Writes data to the remote host.
     *
     * @param data The data buffer to write.
     * @param size The size of the data buffer in bytes.
     * @return ErrorCode indicating success or failure of the write operation.
     */
    ErrorCode write(const uint8_t* data, size_t size) const;

    /**
     * @brief Reads data from the remote host.
     *
     * @param buffer The buffer to store the read data.
     * @param size The maximum number of bytes to read.
     * @return ErrorCode indicating success or failure of the read operation. If successful,
     *         the number of bytes read is stored in the `bytesRead` output parameter.
     */
    ErrorCode read(uint8_t* buffer, size_t size, size_t& bytesRead) const;

    /**
     * @brief Checks the number of bytes available to read from the remote host.
     *
     * @return ErrorCode indicating success or failure. If successful,
     *         the number of bytes available is stored in the `availableBytes` output parameter.
     */
    ErrorCode available(int& availableBytes) const;

    // Setters and Getters for _socket and _isConnected
    void setSocket(int socket);
    [[nodiscard]] int getSocket() const;
    void setConnected(bool isConnected);

private:
    int _socket; /**< Socket file descriptor. */
    bool _isConnected; /**< Connection status flag. */

    /**
     * @brief Performs the actual connection logic (common for IP and hostname).
     *
     * @param ip The IP address to connect to. This parameter is in network byte order.
     * @param port The port to connect to.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode connectInternal(uint32_t ip, uint16_t port);
};

#endif // WIFI_CLIENT_H