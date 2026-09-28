#ifndef NETWORK_SELECTOR_H
#define NETWORK_SELECTOR_H

#include <vector>
#include <optional>
#include "NetworkTypes.h"

/**
 * @file NetworkSelector.h
 * @brief Intelligent network selection algorithm for choosing the best available network.
 * 
 * The NetworkSelector evaluates available networks against known networks and
 * selects the best one based on multiple criteria: user-defined priority,
 * signal strength (RSSI), authentication security level, and connection history.
 */

/**
 * @struct NetworkScore
 * @brief Represents the calculated score for a network candidate.
 */
struct NetworkScore {
    std::string ssid;           /**< Network SSID */
    int totalScore;             /**< Total calculated score */
    int priorityScore;          /**< Score from user priority (0-100) */
    int rssiScore;              /**< Score from signal strength (0-100) */
    int securityScore;          /**< Score from authentication mode (0-100) */
    int historyScore;           /**< Score from connection history (0-100) */
    bool isKnown;               /**< Whether network is in known list */
    bool hasCredentials;        /**< Whether we have credentials for this network */
    
    NetworkScore() : totalScore(0), priorityScore(0), rssiScore(0), 
                     securityScore(0), historyScore(0), isKnown(false), 
                     hasCredentials(false) {}
};

/**
 * @struct SelectionConfig
 * @brief Configuration for the network selection algorithm.
 */
struct SelectionConfig {
    float priorityWeight;       /**< Weight for user priority (0.0-1.0) */
    float rssiWeight;           /**< Weight for signal strength (0.0-1.0) */
    float securityWeight;       /**< Weight for security level (0.0-1.0) */
    float historyWeight;        /**< Weight for connection history (0.0-1.0) */
    int8_t minimumRssi;         /**< Minimum acceptable RSSI (-100 to 0) */
    bool preferKnownNetworks;   /**< Strongly prefer known networks */
    bool requireSecure;         /**< Require at least WPA authentication */
    
    /**
     * @brief Default configuration with balanced weights.
     */
    SelectionConfig()
        : priorityWeight(0.35f)
        , rssiWeight(0.35f)
        , securityWeight(0.15f)
        , historyWeight(0.15f)
        , minimumRssi(-80)
        , preferKnownNetworks(true)
        , requireSecure(false) {}
        
    /**
     * @brief Normalize weights to sum to 1.0.
     */
    void normalizeWeights() {
        float total = priorityWeight + rssiWeight + securityWeight + historyWeight;
        if (total > 0) {
            priorityWeight /= total;
            rssiWeight /= total;
            securityWeight /= total;
            historyWeight /= total;
        }
    }
};

/**
 * @class NetworkSelector
 * @brief Implements intelligent network selection based on multiple criteria.
 * 
 * The selector uses a weighted scoring system to evaluate networks:
 * - Priority: User-defined preference (higher = better)
 * - RSSI: Signal strength (stronger = better)
 * - Security: Authentication mode (WPA3 > WPA2 > WPA > Open)
 * - History: Recent successful connections (more recent = better)
 */
class NetworkSelector {
public:
    /**
     * @brief Get the singleton instance.
     * @return Reference to the NetworkSelector instance.
     */
    static NetworkSelector& instance();

    /**
     * @brief Select the best network from available options.
     * 
     * @param scannedNetworks Networks found in the current scan.
     * @param knownNetworks List of saved/known networks with credentials.
     * @return Optional containing the best KnownNetwork, or empty if none suitable.
     */
    std::optional<KnownNetwork> selectBestNetwork(
        const std::vector<ScannedNetwork>& scannedNetworks,
        const std::vector<KnownNetwork>& knownNetworks) const;

    /**
     * @brief Evaluate and score all candidate networks.
     * 
     * @param scannedNetworks Networks found in the current scan.
     * @param knownNetworks List of saved/known networks with credentials.
     * @return Vector of NetworkScore sorted by total score (highest first).
     */
    std::vector<NetworkScore> evaluateNetworks(
        const std::vector<ScannedNetwork>& scannedNetworks,
        const std::vector<KnownNetwork>& knownNetworks) const;

    /**
     * @brief Check if a scanned network is better than the current connection.
     * 
     * Used for roaming decisions.
     * 
     * @param currentNetwork The currently connected network info.
     * @param candidateNetwork The potential network to roam to.
     * @param knownNetworks List of known networks.
     * @param rssiThreshold Minimum RSSI improvement required (dB).
     * @return True if candidate is significantly better.
     */
    bool isBetterNetwork(
        const NetworkInfo& currentNetwork,
        const ScannedNetwork& candidateNetwork,
        const std::vector<KnownNetwork>& knownNetworks,
        int8_t rssiThreshold = 10) const;

    /**
     * @brief Get the configuration.
     * @return Current SelectionConfig.
     */
    const SelectionConfig& getConfig() const { return _config; }

    /**
     * @brief Set the configuration.
     * @param config New SelectionConfig to use.
     */
    void setConfig(const SelectionConfig& config);

    /**
     * @brief Set weight for priority scoring.
     * @param weight Weight value (0.0-1.0).
     */
    void setPriorityWeight(float weight);

    /**
     * @brief Set weight for RSSI scoring.
     * @param weight Weight value (0.0-1.0).
     */
    void setRssiWeight(float weight);

    /**
     * @brief Set minimum acceptable RSSI.
     * @param rssi Minimum RSSI in dBm (-100 to 0).
     */
    void setMinimumRssi(int8_t rssi);

private:
    NetworkSelector();
    ~NetworkSelector() = default;
    NetworkSelector(const NetworkSelector&) = delete;
    NetworkSelector& operator=(const NetworkSelector&) = delete;

    /**
     * @brief Calculate RSSI score (0-100).
     * @param rssi Signal strength in dBm.
     * @return Score from 0 to 100.
     */
    int calculateRssiScore(int8_t rssi) const;

    /**
     * @brief Calculate security score (0-100).
     * @param authMode Authentication mode.
     * @return Score from 0 to 100.
     */
    int calculateSecurityScore(WiFiAuthMode authMode) const;

    /**
     * @brief Calculate history score (0-100) based on last connection time.
     * @param lastConnected Timestamp of last connection.
     * @return Score from 0 to 100.
     */
    int calculateHistoryScore(uint32_t lastConnected) const;

    /**
     * @brief Find a known network by SSID.
     * @param ssid SSID to search for.
     * @param knownNetworks List to search in.
     * @return Pointer to KnownNetwork if found, nullptr otherwise.
     */
    const KnownNetwork* findKnownNetwork(
        const std::string& ssid,
        const std::vector<KnownNetwork>& knownNetworks) const;

    SelectionConfig _config;
    
    static constexpr const char* TAG = "NetworkSelector";
};

#endif // NETWORK_SELECTOR_H
