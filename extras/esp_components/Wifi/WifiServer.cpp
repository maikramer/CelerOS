#include "WifiServer.h"
#include "GeneralErrorCodes.h"
#include "WifiErrorCodes.h"
#include <lwip/sockets.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>

/**
 * @file WifiServer.cpp
 * @brief Implementation of the WifiServer class for creating TCP servers.
 */

WifiServer::WifiServer() : _serverSocket(-1), _isRunning(false) {}

WifiServer::~WifiServer() {
    if (_isRunning) {
        stop();
    }
}

ErrorCode WifiServer::start(const std::string& ssid, const std::string& password,
                            const IPAddress& ip, uint16_t port) {
    ErrorCode err = configureAccessPoint(ssid, password, ip);
    if (err != CommonErrorCodes::None) {
        return err;
    }

    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket < 0) {
        ESP_LOGE("WifiServer", "Failed to create socket: %s", strerror(errno));
        return CommonErrorCodes::SocketCreationFailed;
    }

    _serverAddress.sin_family = AF_INET;
    _serverAddress.sin_addr.s_addr = ip.get().addr;
    _serverAddress.sin_port = htons(port);

    if (bind(_serverSocket, (sockaddr*)&_serverAddress, sizeof(_serverAddress)) < 0) {
        ESP_LOGE("WifiServer", "Failed to bind socket: %s", strerror(errno));
        close(_serverSocket);
        return CommonErrorCodes::SocketBindFailed;
    }

    if (listen(_serverSocket, 5) < 0) {
        ESP_LOGE("WifiServer", "Failed to listen on socket: %s", strerror(errno));
        close(_serverSocket);
        return CommonErrorCodes::SocketListenFailed;
    }

    _isRunning = true;
    ESP_LOGI("WifiServer", "Server started on port %d", port);
    return CommonErrorCodes::None;
}

void WifiServer::stop() {
    if (_isRunning) {
        close(_serverSocket);
        _isRunning = false;
    }
}

bool WifiServer::hasClient() const {
    if (!_isRunning) {
        return false;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(_serverSocket, &readfds);

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int selectResult = select(_serverSocket + 1, &readfds, nullptr, nullptr, &timeout);

    return (selectResult > 0) && FD_ISSET(_serverSocket, &readfds);
}

ErrorCode WifiServer::accept(WifiClient& client) const {
    if (!_isRunning) {
        return CommonErrorCodes::OperationFailed; // Or a more specific error
    }

    sockaddr_in clientAddress{};
    socklen_t clientAddressLength = sizeof(clientAddress);

    int clientSocket = ::accept(_serverSocket, (sockaddr*)&clientAddress, &clientAddressLength);
    if (clientSocket < 0) {
        ESP_LOGE("WifiServer", "Failed to accept client connection: %s", strerror(errno));
        return CommonErrorCodes::SocketAcceptFailed;
    }

    client.setSocket(clientSocket);
    client.setConnected(true);
    return CommonErrorCodes::None;
}

ErrorCode WifiServer::configureAccessPoint(const std::string& ssid, const std::string& password,
                                           const IPAddress& ip) {
    // Initialize netif and event loop if not already done
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("WifiServer", "Failed to initialize netif");
        return CommonErrorCodes::WifiInitFailed;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("WifiServer", "Failed to create event loop");
        return CommonErrorCodes::WifiInitFailed;
    }

    // Stop any existing WiFi mode
    if (esp_wifi_stop() != ESP_OK) {
        ESP_LOGE("WifiServer", "Failed to stop existing WiFi");
        return CommonErrorCodes::WifiAPStartFailed;
    }

    // Initialize WiFi in AP mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE("WifiServer", "Failed to initialize WiFi");
        return CommonErrorCodes::WifiInitFailed;
    }
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        ESP_LOGE("WifiServer", "Failed to set WiFi mode to AP");
        return CommonErrorCodes::WifiAPStartFailed;
    }

    // Configure AP network interface (netif)
    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_config = {};
    std::strncpy((char*)wifi_config.ap.ssid, ssid.c_str(), sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = ssid.length();

    if (!password.empty()) {
        std::strncpy((char*)wifi_config.ap.password, password.c_str(), sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    wifi_config.ap.max_connection = 5;
    wifi_config.ap.channel = 1;

    if (esp_wifi_set_config(WIFI_IF_AP, &wifi_config) != ESP_OK) {
        ESP_LOGE("WifiServer", "Failed to set WiFi config");
        return CommonErrorCodes::WifiAPStartFailed;
    }

    // Set the IP address for the access point
    esp_netif_ip_info_t ipInfo = {};
    ipInfo.ip = ip.get();
    ipInfo.gw = ip.get();
    ipInfo.netmask.addr = htonl((1 << 24) | (1 << 25) | (1 << 26) | (1 << 27));

    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (esp_netif_dhcps_stop(ap_netif) != ESP_OK) {
        ESP_LOGE("WifiServer", "Failed to stop DHCP server");
        return CommonErrorCodes::WifiAPStartFailed;
    }
    if (esp_netif_set_ip_info(ap_netif, &ipInfo) != ESP_OK) {
        ESP_LOGE("WifiServer", "Failed to set IP info");
        return CommonErrorCodes::WifiAPStartFailed;
    }

    // Start WiFi
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE("WifiServer", "Failed to start WiFi");
        return CommonErrorCodes::WifiAPStartFailed;
    }
    ESP_LOGI("WifiServer", "Access Point configured with SSID: %s", ssid.c_str());

    return CommonErrorCodes::None;
}