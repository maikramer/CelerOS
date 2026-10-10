#ifndef WIFI_AP_H
#define WIFI_AP_H

#include <string>
#include <vector>
#include <cstdint>
#include "IPAddress.h"

/**
 * @file WifiAP.h
 * @brief WiFi Access Point mode for ESP32.
 * 
 * Provides soft-AP functionality allowing ESP32 to act as a WiFi access point
 * for configuration or direct communication.
 */

/**
 * @enum WifiAPState
 * @brief Access point state.
 */
enum class WifiAPState {
    Stopped,        /**< AP is not running */
    Starting,       /**< AP is starting */
    Running,        /**< AP is running and accepting connections */
    Stopping,       /**< AP is stopping */
    Error           /**< AP encountered an error */
};

/**
 * @struct WifiAPConfig
 * @brief Configuration for WiFi Access Point.
 */
struct WifiAPConfig {
    std::string ssid;               /**< AP SSID (network name) */
    std::string password;           /**< AP password (empty for open network) */
    uint8_t channel;                /**< WiFi channel (1-13) */
    uint8_t maxConnections;         /**< Maximum simultaneous connections */
    bool hidden;                    /**< Hide SSID (not broadcast) */
    uint16_t beaconInterval;        /**< Beacon interval in ms */
    
    // Network configuration
    std::string ipAddress;          /**< AP IP address */
    std::string gateway;            /**< Gateway address */
    std::string netmask;            /**< Subnet mask */

    WifiAPConfig() :
        ssid("ESP32-AP"),
        password(""),
        channel(1),
        maxConnections(4),
        hidden(false),
        beaconInterval(100),
        ipAddress("192.168.4.1"),
        gateway("192.168.4.1"),
        netmask("255.255.255.0") {}
};

/**
 * @struct WifiAPClientInfo
 * @brief Information about a connected client.
 */
struct WifiAPClientInfo {
    uint8_t mac[6];                 /**< Client MAC address */
    std::string macString;          /**< MAC as string */
    int8_t rssi;                    /**< Signal strength */
};

/**
 * @class WifiAP
 * @brief Singleton class for WiFi Access Point functionality.
 * 
 * Usage:
 * @code
 * auto& ap = WifiAP::instance();
 * 
 * // Start open AP
 * ap.start("MyESP32");
 * 
 * // Or with password
 * ap.start("MyESP32", "password123");
 * 
 * // Check connected clients (polling: os eventos de cliente foram removidos)
 * int n = ap.getConnectedClients();
 * 
 * // Stop AP
 * ap.stop();
 * @endcode
 */
class WifiAP {
public:
    /**
     * @brief Get the singleton instance.
     * @return Reference to the WifiAP instance.
     */
    static WifiAP& instance();

    /**
     * @brief Start AP with SSID only (open network).
     * @param ssid Network name.
     * @return True if started successfully.
     */
    bool start(const std::string& ssid);

    /**
     * @brief Start AP with SSID and password.
     * @param ssid Network name.
     * @param password Network password (min 8 chars for WPA2).
     * @return True if started successfully.
     */
    bool start(const std::string& ssid, const std::string& password);

    /**
     * @brief Start AP with full configuration.
     * @param config WifiAPConfig structure.
     * @return True if started successfully.
     */
    bool start(const WifiAPConfig& config);

    /**
     * @brief Stop the access point.
     * @return True if stopped successfully.
     */
    bool stop();

    /**
     * @brief Check if AP is running.
     * @return True if running.
     */
    bool isRunning() const { return _state == WifiAPState::Running; }

    /**
     * @brief Get current state.
     * @return Current WifiAPState.
     */
    WifiAPState getState() const { return _state; }

    /**
     * @brief Get number of connected clients.
     * @return Number of clients.
     */
    int getConnectedClients() const;

    /**
     * @brief Get list of connected clients.
     * @return Vector of WifiAPClientInfo.
     */
    std::vector<WifiAPClientInfo> getClientList() const;

    /**
     * @brief Get AP IP address.
     * @return IP address string.
     */
    std::string getIPAddress() const;

    /**
     * @brief Get current SSID.
     * @return SSID string.
     */
    std::string getSSID() const { return _config.ssid; }

    /**
     * @brief Get current configuration.
     * @return WifiAPConfig structure.
     */
    const WifiAPConfig& getConfig() const { return _config; }

    /**
     * @brief Disconnect a specific client.
     * @param mac MAC address (6 bytes).
     * @return True if disconnected.
     */
    bool disconnectClient(const uint8_t* mac);

    // Sem eventos (onStarted/onStopped/onClientConnected/onClientDisconnected):
    // triagem de 2026-10 nao achou UM assinante em todo o firmware — os
    // objetos Event custavam heap a toa. Quem precisar dos clientes usa
    // getClientList()/getConnectedClients().

private:
    WifiAP();
    ~WifiAP();
    WifiAP(const WifiAP&) = delete;
    WifiAP& operator=(const WifiAP&) = delete;

    /**
     * @brief Initialize WiFi in AP mode.
     */
    bool initWifi();

    /**
     * @brief Configure network interface.
     */
    bool configureNetwork();

    /**
     * @brief WiFi event handler.
     */
    static void eventHandler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data);

    /**
     * @brief Set state and log.
     */
    void setState(WifiAPState newState);

    WifiAPConfig _config;
    WifiAPState _state;
    void* _netif;  // esp_netif_t*
    bool _wifiInitialized;

    static WifiAP* _instance;  // For event handler
    static constexpr const char* TAG = "WifiAP";
};

#endif // WIFI_AP_H
