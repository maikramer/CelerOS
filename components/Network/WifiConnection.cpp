#include "WifiConnection.h"
#include "GeneralErrorCodes.h"
#include "CommunicationErrorCodes.h"
#include <cstring>
#include <esp_log.h>
#include <algorithm>

EventGroupHandle_t WifiConnection::_wifiEventGroup = nullptr;
const char *WifiConnection::TAG = "WifiConnection";

/**
 * @file WifiConnection.cpp
 * @brief Implementation of the WifiConnection class for managing WiFi connections in station mode.
 */

WifiConnection::WifiConnection() :
        _ipAddress(), _rssi(0), _lastReportedRssi(0),
        _channel(0), _retryNum(0), _maxRetries(5), _connectionTimeout(20000),  // 20 seconds timeout
        _isConnected(false), _initialized(false), _asyncMode(false),
        _scanInProgress(false), _blockingScan(false), _state(WiFiConnectionState::Idle), 
        _scanMutex(nullptr) {
    _scanMutex = xSemaphoreCreateMutex();
}

WifiConnection::~WifiConnection() {
    disconnect(); //NOLINT
    if (_scanMutex != nullptr) {
        vSemaphoreDelete(_scanMutex);
        _scanMutex = nullptr;
    }
}

void WifiConnection::setState(WiFiConnectionState newState) {
    if (_state != newState) {
        WiFiConnectionState oldState = _state;
        _state = newState;
        ESP_LOGD(TAG, "State changed: %d -> %d", static_cast<int>(oldState), static_cast<int>(newState));
        onStateChanged.trigger(this, oldState, newState);
    }
}

ErrorCode WifiConnection::init() {
    if (_initialized) {
        return CommonErrorCodes::None;
    }

    setState(WiFiConnectionState::Initializing);

    // Initialize NVS Flash (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao apagar NVS: %s", esp_err_to_name(ret));
            setState(WiFiConnectionState::Error);
            return CommonErrorCodes::WifiInitFailed;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Erro ao inicializar NVS: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Error);
        return CommonErrorCodes::WifiInitFailed;
    }

    // Create event group
    if (_wifiEventGroup == nullptr) {
        _wifiEventGroup = xEventGroupCreate();
        if (_wifiEventGroup == nullptr) {
            ESP_LOGE(TAG, "Erro ao criar event group");
            setState(WiFiConnectionState::Error);
            return CommonErrorCodes::WifiInitFailed;
        }
    }

    // Initialize netif
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Erro ao inicializar netif: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Error);
        return CommonErrorCodes::WifiInitFailed;
    }

    // Create event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Erro ao criar event loop: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Error);
        return CommonErrorCodes::WifiInitFailed;
    }

    // Create STA interface if not exists
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == nullptr) {
        sta_netif = esp_netif_create_default_wifi_sta();
        if (sta_netif == nullptr) {
            ESP_LOGE(TAG, "Erro ao criar interface STA");
            setState(WiFiConnectionState::Error);
            return CommonErrorCodes::WifiInitFailed;
        }
    }

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Erro ao inicializar WiFi: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Error);
        return CommonErrorCodes::WifiInitFailed;
    }

    // Register event handlers
    static esp_event_handler_instance_t instance_any_id = nullptr;
    static esp_event_handler_instance_t instance_got_ip = nullptr;
    static esp_event_handler_instance_t instance_scan_done = nullptr;

    if (instance_any_id == nullptr) {
        ret = esp_event_handler_instance_register(WIFI_EVENT,
                                                  ESP_EVENT_ANY_ID,
                                                  &eventHandler,
                                                  this,
                                                  &instance_any_id);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao registrar handler WIFI_EVENT: %s", esp_err_to_name(ret));
            setState(WiFiConnectionState::Error);
            return CommonErrorCodes::WifiInitFailed;
        }
    }

    if (instance_got_ip == nullptr) {
        ret = esp_event_handler_instance_register(IP_EVENT,
                                                  IP_EVENT_STA_GOT_IP,
                                                  &eventHandler,
                                                  this,
                                                  &instance_got_ip);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao registrar handler IP_EVENT: %s", esp_err_to_name(ret));
            setState(WiFiConnectionState::Error);
            return CommonErrorCodes::WifiInitFailed;
        }
    }

    // Set WiFi mode
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao configurar modo WiFi: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Error);
        return CommonErrorCodes::WifiInitFailed;
    }

    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Erro ao iniciar WiFi: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Error);
        return CommonErrorCodes::WifiInitFailed;
    }

    _initialized = true;
    setState(WiFiConnectionState::Idle);
    ESP_LOGI(TAG, "WiFi initialized successfully");
    return CommonErrorCodes::None;
}

ErrorCode WifiConnection::ensureInitialized() {
    if (!_initialized) {
        return init();
    }
    return CommonErrorCodes::None;
}

ErrorCode WifiConnection::connect(const std::string &ssid, const std::string &password, bool asyncConnect) {
    ErrorCode err = ensureInitialized();
    if (err != CommonErrorCodes::None) {
        return err;
    }

    _ssid = ssid;
    _password = password;
    _asyncMode = asyncConnect;

    // Clear event bits for new connection attempt
    xEventGroupClearBits(_wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // Configure WiFi connection
    wifi_config_t wifi_config = {};
    // SSID de 32 e PSK de 64 caracteres ocupam o campo inteiro sem '\0' (o IDF
    // aceita); o resto ja e zero pelo {} acima. memcpy limitado = sem truncagem.
    std::memcpy(wifi_config.sta.ssid, ssid.data(), std::min(ssid.size(), sizeof(wifi_config.sta.ssid)));
    std::memcpy(wifi_config.sta.password, password.data(),
                std::min(password.size(), sizeof(wifi_config.sta.password)));

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao configurar WiFi: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Error);
        return CommonErrorCodes::WifiInitFailed;
    }

    // Reset retry counter
    _retryNum = 0;
    _isConnected = false;

    // Update state and trigger event
    setState(WiFiConnectionState::Connecting);
    onConnecting.trigger(this, ssid);

    ESP_LOGI(TAG, "Connecting to WiFi network: %s", ssid.c_str());

    // Clear any previous event bits before starting connection
    xEventGroupClearBits(_wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    _retryNum = 0;

    // Start connection
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar conexão WiFi: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Error);
        onAuthFailed.trigger(this, ssid, CommonErrorCodes::WifiInitFailed);
        return CommonErrorCodes::WifiInitFailed;
    }

    // If async mode, return immediately
    if (asyncConnect) {
        return CommonErrorCodes::None;
    }

    // Wait for connection or failure (blocking mode)
    EventBits_t bits = xEventGroupWaitBits(_wifiEventGroup,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdTRUE,   // Clear bits on exit
                                           pdFALSE,  // Wait for any bit
                                           pdMS_TO_TICKS(_connectionTimeout));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi network: %s", ssid.c_str());
        return CommonErrorCodes::None;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi network: %s", ssid.c_str());
        return CommonErrorCodes::WifiConnectionFailed;
    } else {
        ESP_LOGE(TAG, "Timeout waiting for WiFi connection.");
        setState(WiFiConnectionState::Idle);
        return CommonErrorCodes::Timeout;
    }
}

void WifiConnection::disconnect() {
    if (_state == WiFiConnectionState::Idle) {
        return;
    }

    setState(WiFiConnectionState::Disconnecting);

    if (isConnected()) {
        WiFiConnectionEvent event;
        event.ssid = _ssid;
        event.rssi = _rssi;
        event.ip = _ipAddress;
        event.error = CommonErrorCodes::None;

        esp_wifi_disconnect();
        
        _isConnected = false;
        ESP_LOGI(TAG, "Disconnected from WiFi network.");
        
        onDisconnected.trigger(this, event);
    }
    
    setState(WiFiConnectionState::Idle);
    _ssid.clear();
    _ipAddress = IPAddress();
    _rssi = 0;
    _channel = 0;
}

bool WifiConnection::isConnected() const {
    return _isConnected;
}

WiFiConnectionState WifiConnection::getState() const {
    return _state;
}

std::string WifiConnection::getSSID() const {
    if (!isConnected()) {
        return "";
    }

    wifi_config_t wifi_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
        return std::string(reinterpret_cast<char*>(wifi_config.sta.ssid));
    }
    return _ssid;
}

IPAddress WifiConnection::getIPAddress() const {
    return _ipAddress;
}

int8_t WifiConnection::getRSSI() const {
    if (!isConnected()) {
        return 0;
    }
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return _rssi;
}

uint8_t WifiConnection::getChannel() const {
    return _channel;
}

NetworkInfo WifiConnection::getNetworkInfo() const {
    NetworkInfo info;
    
    if (!isConnected()) {
        return info;
    }

    info.type = NetworkType::WiFi;
    info.ssid = getSSID();
    info.rssi = getRSSI();
    info.ip = _ipAddress.toString();
    info.channel = _channel;

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                 ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
        info.bssid = bssid;
        info.authMode = toWiFiAuthMode(ap_info.authmode);
    }

    // Get gateway and netmask
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != nullptr) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            info.gateway = IPAddress(ip_info.gw).toString();
            info.netmask = IPAddress(ip_info.netmask).toString();
        }
    }

    return info;
}

void WifiConnection::setMaxRetries(uint8_t maxRetries) {
    _maxRetries = maxRetries;
}

void WifiConnection::setConnectionTimeout(uint32_t timeoutMs) {
    _connectionTimeout = timeoutMs;
}

ErrorCode WifiConnection::startScanAsync() {
    ErrorCode err = ensureInitialized();
    if (err != CommonErrorCodes::None) {
        return err;
    }

    // Protect against concurrent scans
    if (_scanMutex == nullptr) {
        ESP_LOGE(TAG, "Scan mutex not initialized");
        return CommonErrorCodes::NotInitialized;
    }

    // Try to acquire mutex with timeout
    if (xSemaphoreTake(_scanMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Scan already in progress, skipping async scan...");
        return CommonErrorCodes::WifiScanFailed;
    }

    if (_scanInProgress) {
        ESP_LOGW(TAG, "Scan already running, aborting async scan...");
        xSemaphoreGive(_scanMutex);
        return CommonErrorCodes::WifiScanFailed;
    }

    _scanInProgress = true;
    setState(WiFiConnectionState::Scanning);
    onScanStarted.trigger(this);

    ESP_LOGI(TAG, "Starting async WiFi scan...");

    // Stop any pending scan first
    esp_wifi_scan_stop();

    wifi_scan_config_t scan_config = {};
    scan_config.ssid = nullptr;
    scan_config.bssid = nullptr;
    scan_config.channel = 0;  // Scan all channels
    scan_config.show_hidden = true;  // Also show hidden networks
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active.min = 120;  // Min 120ms per channel
    scan_config.scan_time.active.max = 300;  // Max 300ms per channel

    // Start non-blocking scan
    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar scan: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Idle);
        _scanInProgress = false;
        xSemaphoreGive(_scanMutex);
        
        WiFiScanResult result;
        result.success = false;
        result.error = CommonErrorCodes::WifiScanFailed;
        result.count = 0;
        onScanCompleted.trigger(this, result);
        
        return CommonErrorCodes::WifiScanFailed;
    }

    // Note: mutex will be released when scan completes (in event handler)
    // For now, release it as we're in async mode
    xSemaphoreGive(_scanMutex);
    
    return CommonErrorCodes::None;
}

int WifiConnection::scan(wifi_ap_record_t* ap_list, uint16_t max_aps) {
    if (ap_list == nullptr || max_aps == 0) {
        ESP_LOGE(TAG, "Parâmetros inválidos para scan");
        return -1;
    }

    ErrorCode err = ensureInitialized();
    if (err != CommonErrorCodes::None) {
        return -1;
    }

    // Protect against concurrent scans
    if (_scanMutex == nullptr) {
        ESP_LOGE(TAG, "Scan mutex not initialized");
        return -1;
    }

    // Try to acquire mutex with timeout
    if (xSemaphoreTake(_scanMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Scan already in progress, skipping...");
        return -1;
    }

    // Check if another scan is still running
    if (_scanInProgress) {
        ESP_LOGW(TAG, "Scan already running, aborting...");
        xSemaphoreGive(_scanMutex);
        return -1;
    }

    _scanInProgress = true;
    _blockingScan = true;  // Mark as blocking scan - event handler should not consume results
    setState(WiFiConnectionState::Scanning);
    onScanStarted.trigger(this);

    ESP_LOGI(TAG, "Iniciando scan WiFi...");

    // Stop any pending scan first
    esp_wifi_scan_stop();

    // Configure scan with proper timing for better detection
    wifi_scan_config_t scan_config = {};
    scan_config.ssid = nullptr;
    scan_config.bssid = nullptr;
    scan_config.channel = 0;  // Scan all channels
    scan_config.show_hidden = true;  // Also show hidden networks
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active.min = 120;  // Min 120ms per channel
    scan_config.scan_time.active.max = 300;  // Max 300ms per channel

    // Start blocking scan
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar scan: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Idle);
        _scanInProgress = false;
        _blockingScan = false;
        xSemaphoreGive(_scanMutex);
        
        WiFiScanResult result;
        result.success = false;
        result.error = CommonErrorCodes::WifiScanFailed;
        result.count = 0;
        onScanCompleted.trigger(this, result);
        
        return -1;
    }

    // Get scan results
    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao obter número de APs: %s", esp_err_to_name(ret));
        setState(WiFiConnectionState::Idle);
        _scanInProgress = false;
        _blockingScan = false;
        xSemaphoreGive(_scanMutex);
        return -1;
    }

    ESP_LOGI(TAG, "Encontradas %d redes WiFi", ap_count);

    if (ap_count > max_aps) {
        ap_count = max_aps;
    }

    if (ap_count > 0) {
        ret = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao obter lista de APs: %s", esp_err_to_name(ret));
            setState(WiFiConnectionState::Idle);
            _scanInProgress = false;
            _blockingScan = false;
            xSemaphoreGive(_scanMutex);
            return -1;
        }
    }

    // Process and cache results
    _lastScanResult.networks.clear();
    _lastScanResult.count = ap_count;
    _lastScanResult.success = true;
    _lastScanResult.error = CommonErrorCodes::None;

    for (uint16_t i = 0; i < ap_count; i++) {
        _lastScanResult.networks.emplace_back(ap_list[i]);
    }

    // Sort by RSSI
    std::sort(_lastScanResult.networks.begin(), _lastScanResult.networks.end(), compareByRssi);

    setState(WiFiConnectionState::Idle);
    _scanInProgress = false;
    _blockingScan = false;
    xSemaphoreGive(_scanMutex);
    
    onScanCompleted.trigger(this, _lastScanResult);

    ESP_LOGI(TAG, "Scan concluído, retornando %d redes", ap_count);
    return static_cast<int>(ap_count);
}

WiFiScanResult WifiConnection::getLastScanResults() const {
    return _lastScanResult;
}

void WifiConnection::processScanResults(WiFiScanResult& result) {
    uint16_t ap_count = 0;
    esp_err_t ret = esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ret != ESP_OK) {
        result.success = false;
        result.error = CommonErrorCodes::WifiScanFailed;
        result.count = 0;
        return;
    }

    result.networks.clear();
    result.count = ap_count;
    result.success = true;
    result.error = CommonErrorCodes::None;

    if (ap_count == 0) {
        return;
    }

    // Allocate temporary buffer for AP records
    std::vector<wifi_ap_record_t> ap_records(ap_count);
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records.data());
    
    if (ret != ESP_OK) {
        result.success = false;
        result.error = CommonErrorCodes::WifiScanFailed;
        return;
    }

    // Convert to ScannedNetwork
    for (const auto& record : ap_records) {
        result.networks.emplace_back(record);
    }

    // Sort by RSSI
    std::sort(result.networks.begin(), result.networks.end(), compareByRssi);
}

void WifiConnection::updateRssi() {
    if (!isConnected()) {
        return;
    }

    int8_t currentRssi = getRSSI();
    int8_t diff = currentRssi - _lastReportedRssi;
    
    if (diff > RSSI_CHANGE_THRESHOLD || diff < -RSSI_CHANGE_THRESHOLD) {
        int8_t oldRssi = _lastReportedRssi;
        _lastReportedRssi = currentRssi;
        _rssi = currentRssi;
        onSignalChanged.trigger(this, oldRssi, currentRssi);
    }
}

void WifiConnection::eventHandler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data) {
    auto *self = static_cast<WifiConnection *>(arg);

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                // WiFi started, don't auto-connect here
                ESP_LOGD(TAG, "WIFI_EVENT_STA_START");
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                auto* disconnect_event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
                ESP_LOGI(TAG, "Disconnected from AP, reason: %d", disconnect_event->reason);

                bool wasConnected = self->_isConnected;
                self->_isConnected = false;

                // Check if this is an auth failure
                bool authFailed = (disconnect_event->reason == WIFI_REASON_AUTH_FAIL ||
                                   disconnect_event->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                   disconnect_event->reason == WIFI_REASON_HANDSHAKE_TIMEOUT);

                if (self->_state == WiFiConnectionState::Connecting) {
                    if (authFailed) {
                        // Authentication failed
                        self->setState(WiFiConnectionState::Idle);
                        xEventGroupSetBits(_wifiEventGroup, WIFI_FAIL_BIT);
                        self->onAuthFailed.trigger(self, self->_ssid, CommonErrorCodes::WifiAuthFailed);
                    } else if (self->_retryNum < self->_maxRetries) {
                        // Retry connection
                        self->_retryNum++;
                        self->onRetrying.trigger(self, self->_retryNum, self->_maxRetries);
                        ESP_LOGI(TAG, "Retrying connection (%d/%d)...", self->_retryNum, self->_maxRetries);
                        esp_wifi_connect();
                    } else {
                        // Max retries reached
                        self->setState(WiFiConnectionState::Idle);
                        xEventGroupSetBits(_wifiEventGroup, WIFI_FAIL_BIT);
                        self->onAuthFailed.trigger(self, self->_ssid, CommonErrorCodes::WifiConnectionFailed);
                    }
                } else if (wasConnected) {
                    // Was connected, now disconnected
                    WiFiConnectionEvent event;
                    event.ssid = self->_ssid;
                    event.rssi = self->_rssi;
                    event.ip = self->_ipAddress;
                    event.error = CommonErrorCodes::WifiConnectionFailed;
                    
                    self->setState(WiFiConnectionState::Idle);
                    self->onDisconnected.trigger(self, event);
                }
                break;
            }

            case WIFI_EVENT_SCAN_DONE: {
                ESP_LOGI(TAG, "WIFI_EVENT_SCAN_DONE");
                
                // Only process results here for async scans
                // For blocking scans, results are processed in scan() function directly
                if (!self->_blockingScan) {
                    // Reset scan in progress flag only for async scans
                    self->_scanInProgress = false;
                    
                    if (self->_state == WiFiConnectionState::Scanning) {
                        self->processScanResults(self->_lastScanResult);
                        self->setState(WiFiConnectionState::Idle);
                        self->onScanCompleted.trigger(self, self->_lastScanResult);
                    }
                }
                // For blocking scans, the scan() function handles everything
                xEventGroupSetBits(_wifiEventGroup, WIFI_SCAN_DONE_BIT);
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        self->_ipAddress = IPAddress(event->ip_info.ip);
        self->_retryNum = 0;
        self->_isConnected = true;

        // Get additional info
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            self->_rssi = ap_info.rssi;
            self->_lastReportedRssi = ap_info.rssi;
            self->_channel = ap_info.primary;
        }

        self->setState(WiFiConnectionState::Connected);
        xEventGroupSetBits(_wifiEventGroup, WIFI_CONNECTED_BIT);

        // Trigger new event
        WiFiConnectionEvent connEvent;
        connEvent.ssid = self->_ssid;
        connEvent.rssi = self->_rssi;
        connEvent.ip = self->_ipAddress;
        connEvent.error = CommonErrorCodes::None;
        connEvent.retryCount = self->_retryNum;
        self->onConnected.trigger(self, connEvent);

        ESP_LOGI(TAG, "Connected! IP: %s, RSSI: %d dBm, Channel: %d",
                 self->_ipAddress.toString().c_str(), self->_rssi, self->_channel);
    }
}
