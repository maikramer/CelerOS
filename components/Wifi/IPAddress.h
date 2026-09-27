#ifndef IP_ADDRESS_H
#define IP_ADDRESS_H

#include <esp_netif.h> // Include for esp_ip4_addr_t
#include <string>
#include "CommonErrorCodes.h"

/**
 * @file IPAddress.h
 * @brief This file defines the IPAddress class, a wrapper for esp_ip4_addr_t
 *        providing convenient methods for handling IPv4 addresses in ESP-IDF.
 */

/**
 * @class IPAddress
 * @brief Represents an IPv4 address, wrapping esp_ip4_addr_t for ease of use.
 */
class IPAddress {
public:
    /**
     * @brief Default constructor. Initializes the IP address to 0.0.0.0.
     */
    IPAddress();

    /**
     * @brief Constructor that initializes the IP address from four octets.
     *
     * @param firstOctet First octet of the IP address.
     * @param secondOctet Second octet of the IP address.
     * @param thirdOctet Third octet of the IP address.
     * @param fourthOctet Fourth octet of the IP address.
     */
    IPAddress(uint8_t firstOctet, uint8_t secondOctet, uint8_t thirdOctet, uint8_t fourthOctet);

    /**
     * @brief Constructor that initializes the IP address from an esp_ip4_addr_t.
     *
     * @param address esp_ip4_addr_t structure containing the IP address.
     */
    explicit IPAddress(const esp_ip4_addr_t& address);

    /**
     * @brief Constructor that initializes the IP address from a string representation.
     *
     * @param address String representation of the IP address (e.g., "192.168.1.1").
     */
    explicit IPAddress(const std::string& address);

    /**
     * @brief Destructor.
     */
    ~IPAddress() = default;

    /**
     * @brief Sets the IP address from a string representation.
     *
     * @param address String representation of the IP address.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode fromString(const std::string& address);

    /**
     * @brief Gets the string representation of the IP address.
     *
     * @return String representation of the IP address.
     */
    [[nodiscard]] std::string toString() const;

    /**
     * @brief Gets the IP address as an esp_ip4_addr_t structure.
     *
     * @return esp_ip4_addr_t representation of the IP address.
     */
    [[nodiscard]] esp_ip4_addr_t get() const;

    /**
     * @brief Gets a specific octet (byte) of the IP address.
     *
     * @param index Index of the octet to retrieve (0-3).
     * @return The octet at the specified index.
     */
    uint8_t operator[](int index) const;

    /**
     * @brief Equality comparison operator.
     *
     * @param other Another IPAddress object to compare with.
     * @return True if the two IP addresses are equal, false otherwise.
     */
    bool operator==(const IPAddress& other) const;

    /**
     * @brief Inequality comparison operator.
     *
     * @param other Another IPAddress object to compare with.
     * @return True if the two IP addresses are not equal, false otherwise.
     */
    bool operator!=(const IPAddress& other) const;

private:
    esp_ip4_addr_t _address{}; /**< Internal representation of the IP address using esp_ip4_addr_t. */
};

#endif // IP_ADDRESS_H