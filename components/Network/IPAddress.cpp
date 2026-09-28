#include "IPAddress.h"
#include <lwip/sockets.h> // For inet_pton and inet_ntop
#include <esp_log.h>

/**
 * @file IPAddress.cpp
 * @brief Implementation for the IPAddress class, a wrapper for esp_ip4_addr_t for easier IPv4 address handling.
 */

// Default constructor
IPAddress::IPAddress() {
    _address.addr = 0;
}

// Constructor from 4 octets
IPAddress::IPAddress(uint8_t firstOctet, uint8_t secondOctet, uint8_t thirdOctet, uint8_t fourthOctet) {
    _address.addr = (firstOctet << 24) | (secondOctet << 16) | (thirdOctet << 8) | fourthOctet;
}

// Constructor from esp_ip4_addr_t
IPAddress::IPAddress(const esp_ip4_addr_t& address) : _address(address) {}

// Constructor from string
IPAddress::IPAddress(const std::string& address) {
    fromString(address);
}

// Set IP from string
ErrorCode IPAddress::fromString(const std::string& address) {
    if (inet_pton(AF_INET, address.c_str(), &_address.addr) != 1) {
        ESP_LOGE("IPAddress", "Invalid IP address: %s", address.c_str());
        return CommonErrorCodes::ArgumentError;
    }
    return CommonErrorCodes::None;
}

// Get IP as string
std::string IPAddress::toString() const {
    char buffer[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &_address.addr, buffer, INET_ADDRSTRLEN) == nullptr) {
        ESP_LOGE("IPAddress", "Failed to convert IP address to string.");
        return "Invalid Address";
    }
    return {buffer};
}

// Get IP as esp_ip4_addr_t
esp_ip4_addr_t IPAddress::get() const {
    return _address;
}

// Access octet by index
uint8_t IPAddress::operator[](int index) const {
    return ((_address.addr >> ((3 - index) * 8)) & 0xFF);
}

// Equality comparison
bool IPAddress::operator==(const IPAddress& other) const {
    return (_address.addr == other._address.addr);
}

// Inequality comparison
bool IPAddress::operator!=(const IPAddress& other) const {
    return !(*this == other);
}