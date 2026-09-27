#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <string>
#include <vector>
#include <functional>
#include "Event.h"
#include "WifiAP.h"

/**
 * @file CaptivePortal.h
 * @brief Captive Portal for WiFi configuration.
 * 
 * Provides a web-based interface for WiFi configuration. When users
 * connect to the ESP32's AP, they are automatically redirected to
 * a configuration page where they can select and connect to a WiFi network.
 */

/**
 * @enum CaptivePortalState
 * @brief Portal state.
 */
enum class CaptivePortalState {
    Stopped,        /**< Portal not running */
    Starting,       /**< Portal starting up */
    Running,        /**< Portal running and serving requests */
    Stopping,       /**< Portal shutting down */
    Error           /**< Portal error */
};

/**
 * @struct CaptivePortalConfig
 * @brief Configuration for Captive Portal.
 */
struct CaptivePortalConfig {
    std::string apSsid;             /**< Access Point SSID */
    std::string apPassword;         /**< Access Point password (empty for open) */
    uint16_t httpPort;              /**< HTTP server port (default: 80) */
    uint16_t dnsPort;               /**< DNS server port (default: 53) */
    std::string title;              /**< Page title */
    std::string deviceName;         /**< Device name to show on page */
    bool scanOnStart;               /**< Scan for networks when portal starts */
    bool autoStopOnConnect;         /**< Auto-stop portal after successful connection */
    uint32_t connectTimeoutMs;      /**< Connection attempt timeout */

    CaptivePortalConfig() :
        apSsid("ESP32-Setup"),
        apPassword(""),
        httpPort(80),
        dnsPort(53),
        title("WiFi Setup"),
        deviceName("ESP32 Device"),
        scanOnStart(true),
        autoStopOnConnect(true),
        connectTimeoutMs(15000) {}
};

/**
 * @struct WiFiCredentials
 * @brief WiFi credentials received from portal.
 */
struct WiFiCredentials {
    std::string ssid;
    std::string password;
};

/**
 * @class CaptivePortal
 * @brief Captive Portal for WiFi configuration.
 * 
 * Usage:
 * @code
 * CaptivePortal portal;
 * 
 * portal.onCredentialsReceived.subscribe([](const WiFiCredentials& creds) {
 *     ESP_LOGI(TAG, "Received: %s", creds.ssid.c_str());
 *     // Connect to the network
 *     NetworkManager::instance().connect(creds.ssid, creds.password, true);
 * });
 * 
 * portal.start();  // Starts AP + DNS + HTTP server
 * 
 * // Later...
 * portal.stop();
 * @endcode
 */
class CaptivePortal {
public:
    /**
     * @brief Constructor with default configuration.
     */
    CaptivePortal();

    /**
     * @brief Constructor with custom configuration.
     * @param config CaptivePortalConfig structure.
     */
    explicit CaptivePortal(const CaptivePortalConfig& config);

    /**
     * @brief Destructor.
     */
    ~CaptivePortal();

    // Prevent copying
    CaptivePortal(const CaptivePortal&) = delete;
    CaptivePortal& operator=(const CaptivePortal&) = delete;

    /**
     * @brief Start the captive portal.
     * @return True if started successfully.
     */
    bool start();

    /**
     * @brief Start with custom SSID.
     * @param apSsid Access Point SSID.
     * @param apPassword Access Point password (optional).
     * @return True if started successfully.
     */
    bool start(const std::string& apSsid, const std::string& apPassword = "");

    /**
     * @brief Stop the captive portal.
     * @return True if stopped successfully.
     */
    bool stop();

    /**
     * @brief Check if portal is running.
     * @return True if running.
     */
    bool isRunning() const { return _state == CaptivePortalState::Running; }

    /**
     * @brief Get current state.
     * @return Current CaptivePortalState.
     */
    CaptivePortalState getState() const { return _state; }

    /**
     * @brief Trigger a WiFi scan.
     * @return Number of networks found (-1 on error).
     */
    int scanNetworks();

    /**
     * @brief Get list of scanned networks.
     * @return Vector of network SSIDs with signal info.
     */
    const std::vector<std::pair<std::string, int8_t>>& getScannedNetworks() const { 
        return _scannedNetworks; 
    }

    /**
     * @brief Get current configuration.
     * @return CaptivePortalConfig structure.
     */
    const CaptivePortalConfig& getConfig() const { return _config; }

    /**
     * @brief Set configuration.
     * @param config New configuration.
     */
    void setConfig(const CaptivePortalConfig& config) { _config = config; }

    // ========== Events ==========

    /**
     * @brief Event triggered when portal starts.
     */
    Event<> onStarted;

    /**
     * @brief Event triggered when portal stops.
     */
    Event<> onStopped;

    /**
     * @brief Event triggered when credentials are received.
     * Parameter: WiFiCredentials
     */
    Event<const WiFiCredentials&> onCredentialsReceived;

    /**
     * @brief Event triggered when connection attempt starts.
     * Parameter: SSID being connected to
     */
    Event<const std::string&> onConnecting;

    /**
     * @brief Event triggered when connection succeeds.
     * Parameters: SSID, IP address
     */
    Event<const std::string&, const std::string&> onConnected;

    /**
     * @brief Event triggered when connection fails.
     * Parameters: SSID, error message
     */
    Event<const std::string&, const std::string&> onConnectionFailed;

private:
    /**
     * @brief Start DNS server.
     */
    bool startDnsServer();

    /**
     * @brief Stop DNS server.
     */
    void stopDnsServer();

    /**
     * @brief Start HTTP server.
     */
    bool startHttpServer();

    /**
     * @brief Stop HTTP server.
     */
    void stopHttpServer();

    /**
     * @brief DNS task function.
     */
    static void dnsTaskFunc(void* param);

    /**
     * @brief Generate HTML page.
     */
    std::string generateHtml();

    /**
     * @brief Generate scan results JSON.
     */
    std::string generateScanJson();

    /**
     * @brief Handle HTTP request.
     */
    static int httpHandler(void* ctx);

    /**
     * @brief Set state.
     */
    void setState(CaptivePortalState newState);

    /**
     * @brief URL decode string.
     */
    static std::string urlDecode(const std::string& str);

    CaptivePortalConfig _config;
    CaptivePortalState _state;
    std::vector<std::pair<std::string, int8_t>> _scannedNetworks;
    
    void* _httpServer;      // httpd_handle_t
    void* _dnsSocket;       // Socket handle
    void* _dnsTask;         // TaskHandle_t
    volatile bool _dnsRunning;

    static constexpr const char* TAG = "CaptivePortal";
};

#endif // CAPTIVE_PORTAL_H
