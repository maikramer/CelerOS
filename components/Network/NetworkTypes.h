#ifndef NETWORK_TYPES_H
#define NETWORK_TYPES_H

#include <string>
#include <cstdint>
#include <cstring>
#include <vector>
#include "esp_wifi.h"

// Forward declaration - IPAddress is defined in Wifi component
// Use string representation for IP addresses in this header to avoid circular dependencies
// When IPAddress is needed, include "IPAddress.h" directly in your .cpp file

/**
 * @file NetworkTypes.h
 * @brief Defines common types, enums, and structures for the NetworkManager system.
 * 
 * This file contains all shared definitions used across the network management
 * components including NetworkManager, NetworkCredentialStore, and NetworkSelector.
 */

/**
 * @enum NetworkState
 * @brief Represents the current state of network connectivity.
 */
enum class NetworkState {
    Disconnected,   /**< No active network connection */
    Scanning,       /**< Scanning for available networks */
    Connecting,     /**< Attempting to connect to a network */
    Connected,      /**< Successfully connected to a network */
    Roaming,        /**< Transitioning to a better network */
    Error           /**< An error occurred */
};

/**
 * @enum NetworkType
 * @brief Identifies the type of network connection.
 */
enum class NetworkType {
    None,       /**< No network type */
    WiFi,       /**< WiFi network */
    Bluetooth   /**< Bluetooth network (future support) */
};

/**
 * @enum WiFiAuthMode
 * @brief Simplified WiFi authentication modes for comparison.
 */
enum class WiFiAuthMode {
    Open = 0,       /**< Open network (no authentication) */
    WEP = 1,        /**< WEP authentication (deprecated, insecure) */
    WPA = 2,        /**< WPA authentication */
    WPA2 = 3,       /**< WPA2 authentication */
    WPA3 = 4,       /**< WPA3 authentication */
    Enterprise = 5  /**< Enterprise authentication */
};

/**
 * @brief Converts ESP-IDF wifi_auth_mode_t to simplified WiFiAuthMode.
 * @param authMode The ESP-IDF authentication mode.
 * @return The simplified WiFiAuthMode.
 */
inline WiFiAuthMode toWiFiAuthMode(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN:
            return WiFiAuthMode::Open;
        case WIFI_AUTH_WEP:
            return WiFiAuthMode::WEP;
        case WIFI_AUTH_WPA_PSK:
            return WiFiAuthMode::WPA;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
            return WiFiAuthMode::WPA2;
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return WiFiAuthMode::WPA3;
        case WIFI_AUTH_WPA2_ENTERPRISE:
        case WIFI_AUTH_WPA3_ENTERPRISE:
            return WiFiAuthMode::Enterprise;
        default:
            return WiFiAuthMode::Open;
    }
}

/**
 * @brief Returns a string representation of NetworkState.
 * @param state The network state.
 * @return String representation of the state.
 */
inline const char* networkStateToString(NetworkState state) {
    switch (state) {
        case NetworkState::Disconnected: return "Disconnected";
        case NetworkState::Scanning: return "Scanning";
        case NetworkState::Connecting: return "Connecting";
        case NetworkState::Connected: return "Connected";
        case NetworkState::Roaming: return "Roaming";
        case NetworkState::Error: return "Error";
        default: return "Unknown";
    }
}

/**
 * @brief Returns a string representation of NetworkType.
 * @param type The network type.
 * @return String representation of the type.
 */
inline const char* networkTypeToString(NetworkType type) {
    switch (type) {
        case NetworkType::None: return "None";
        case NetworkType::WiFi: return "WiFi";
        case NetworkType::Bluetooth: return "Bluetooth";
        default: return "Unknown";
    }
}

/**
 * @brief Copia uma string C com truncagem segura: copia ate dstSize-1 bytes
 *        e garante o '\0' final (campos char[N] das structs daqui e do
 *        NetworkCredentialStore).
 * @param dst Buffer destino.
 * @param src String origem.
 * @param dstSize Tamanho TOTAL do destino.
 */
inline void copyTrunc(char* dst, const char* src, size_t dstSize) {
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

/**
 * @struct KnownNetwork
 * @brief Represents a saved/known network with credentials and preferences.
 *
 * This structure is used to persist network configurations in NVS.
 */
struct KnownNetwork {
    char ssid[33];          /**< SSID (max 32 chars + null terminator) */
    char password[65];      /**< Password (max 64 chars + null terminator) */
    int8_t priority;        /**< Connection priority (0-100, higher = preferred) */
    int8_t lastRssi;        /**< Last known signal strength */
    uint32_t lastConnected; /**< Timestamp of last successful connection (epoch seconds) */
    bool autoConnect;       /**< Whether to auto-connect to this network */
    WiFiAuthMode authMode;  /**< Authentication mode */
    
    /**
     * @brief Default constructor initializes all fields.
     */
    KnownNetwork() : priority(50), lastRssi(0), lastConnected(0), 
                     autoConnect(true), authMode(WiFiAuthMode::WPA2) {
        ssid[0] = '\0';
        password[0] = '\0';
    }
    
    /**
     * @brief Constructor with SSID and password.
     * @param ssid The network SSID.
     * @param password The network password.
     * @param priority Connection priority (default 50).
     * @param autoConnect Whether to auto-connect (default true).
     */
    KnownNetwork(const char* ssid, const char* password,
                 int8_t priority = 50, bool autoConnect = true)
        : priority(priority), lastRssi(0), lastConnected(0),
          autoConnect(autoConnect), authMode(WiFiAuthMode::WPA2) {
        copyTrunc(this->ssid, ssid, sizeof(this->ssid));
        copyTrunc(this->password, password, sizeof(this->password));
    }
    
    /**
     * @brief Check if this network matches a given SSID.
     * @param otherSsid The SSID to compare.
     * @return True if SSIDs match.
     */
    bool matchesSsid(const char* otherSsid) const {
        return strcmp(ssid, otherSsid) == 0;
    }
    
    /**
     * @brief Check if this network matches a given SSID.
     * @param otherSsid The SSID to compare.
     * @return True if SSIDs match.
     */
    bool matchesSsid(const std::string& otherSsid) const {
        return otherSsid == ssid;
    }
};

/**
 * @struct ScannedNetwork
 * @brief Represents a network found during a WiFi scan.
 */
struct ScannedNetwork {
    char ssid[33];              /**< SSID of the network */
    char bssid[18];             /**< BSSID (MAC address) as string */
    int8_t rssi;                /**< Signal strength in dBm */
    uint8_t channel;            /**< WiFi channel */
    wifi_auth_mode_t authMode;  /**< ESP-IDF authentication mode */
    bool isKnown;               /**< True if this network is in known networks list */
    bool isConnected;           /**< True if currently connected to this network */
    
    /**
     * @brief Default constructor.
     */
    ScannedNetwork() : rssi(0), channel(0), authMode(WIFI_AUTH_OPEN), 
                       isKnown(false), isConnected(false) {
        ssid[0] = '\0';
        bssid[0] = '\0';
    }
    
    /**
     * @brief Constructor from ESP-IDF wifi_ap_record_t.
     * @param record The AP record from ESP-IDF scan.
     */
    explicit ScannedNetwork(const wifi_ap_record_t& record) 
        : rssi(record.rssi), channel(record.primary), 
          authMode(record.authmode), isKnown(false), isConnected(false) {
        strncpy(ssid, reinterpret_cast<const char*>(record.ssid), sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 record.bssid[0], record.bssid[1], record.bssid[2],
                 record.bssid[3], record.bssid[4], record.bssid[5]);
    }
    
    /**
     * @brief Get simplified authentication mode.
     * @return The WiFiAuthMode enum value.
     */
    WiFiAuthMode getAuthMode() const {
        return toWiFiAuthMode(authMode);
    }
    
    /**
     * @brief Check if this is an open (no password) network.
     * @return True if no authentication required.
     */
    bool isOpen() const {
        return authMode == WIFI_AUTH_OPEN;
    }
};

/**
 * @struct NetworkInfo
 * @brief Represents information about the currently active network.
 * 
 * Note: IP addresses are stored as strings to avoid circular dependencies.
 * For IPAddress class usage, include "IPAddress.h" in your implementation.
 */
struct NetworkInfo {
    NetworkType type;       /**< Type of network (WiFi, Bluetooth, etc.) */
    std::string ssid;       /**< Network SSID/name */
    std::string bssid;      /**< BSSID for WiFi */
    int8_t rssi;            /**< Current signal strength */
    std::string ip;         /**< Assigned IP address as string */
    std::string gateway;    /**< Gateway IP address as string */
    std::string netmask;    /**< Network mask as string */
    uint8_t channel;        /**< WiFi channel */
    WiFiAuthMode authMode;  /**< Authentication mode */
    
    /**
     * @brief Default constructor.
     */
    NetworkInfo() : type(NetworkType::None), rssi(0), channel(0), 
                    authMode(WiFiAuthMode::Open) {}
    
    /**
     * @brief Check if network info is valid (has a connection).
     * @return True if connected to a network.
     */
    bool isValid() const {
        return type != NetworkType::None && !ssid.empty();
    }
    
    /**
     * @brief Clear all network info.
     */
    void clear() {
        type = NetworkType::None;
        ssid.clear();
        bssid.clear();
        rssi = 0;
        ip.clear();
        gateway.clear();
        netmask.clear();
        channel = 0;
        authMode = WiFiAuthMode::Open;
    }
};

/**
 * @struct NetworkConfig
 * @brief Configuration options for the NetworkManager.
 */
struct NetworkConfig {
    bool autoReconnect;             /**< Enable automatic reconnection */
    uint32_t backgroundScanInterval;/**< Interval between background scans in seconds */
    int8_t roamingThreshold;        /**< RSSI difference to trigger roaming (dB) */
    uint8_t maxRetries;             /**< Maximum connection retries before giving up */
    uint32_t connectionTimeout;     /**< Connection timeout in milliseconds */
    bool enableRoaming;             /**< Enable automatic roaming to better networks */
    
    /**
     * @brief Default constructor with sensible defaults.
     */
    NetworkConfig() 
        : autoReconnect(true)
        , backgroundScanInterval(30)    // 30 seconds
        , roamingThreshold(10)          // 10 dB improvement needed
        , maxRetries(5)
        , connectionTimeout(10000)      // 10 seconds
        , enableRoaming(true) {}
};

/**
 * @brief Comparison function for sorting networks by signal strength.
 * @param a First network.
 * @param b Second network.
 * @return True if a has better signal than b.
 */
inline bool compareByRssi(const ScannedNetwork& a, const ScannedNetwork& b) {
    return a.rssi > b.rssi;  // Higher RSSI is better
}

/**
 * @brief Comparison function for sorting known networks by priority.
 * @param a First network.
 * @param b Second network.
 * @return True if a has higher priority than b.
 */
inline bool compareByPriority(const KnownNetwork& a, const KnownNetwork& b) {
    return a.priority > b.priority;  // Higher priority first
}

#endif // NETWORK_TYPES_H
