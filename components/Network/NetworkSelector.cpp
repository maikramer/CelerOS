#include "NetworkSelector.h"
#include "NetworkClock.h"
#include "esp_log.h"
#include <algorithm>
#include <cmath>

NetworkSelector::NetworkSelector() {
    _config.normalizeWeights();
}

NetworkSelector& NetworkSelector::instance() {
    static NetworkSelector selector;
    return selector;
}

void NetworkSelector::setConfig(const SelectionConfig& config) {
    _config = config;
    _config.normalizeWeights();
}

void NetworkSelector::setPriorityWeight(float weight) {
    _config.priorityWeight = weight;
    _config.normalizeWeights();
}

void NetworkSelector::setRssiWeight(float weight) {
    _config.rssiWeight = weight;
    _config.normalizeWeights();
}

void NetworkSelector::setMinimumRssi(int8_t rssi) {
    _config.minimumRssi = rssi;
}

std::optional<KnownNetwork> NetworkSelector::selectBestNetwork(
    const std::vector<ScannedNetwork>& scannedNetworks,
    const std::vector<KnownNetwork>& knownNetworks) const {
    
    if (scannedNetworks.empty() || knownNetworks.empty()) {
        ESP_LOGD(TAG, "No networks to evaluate (scanned: %zu, known: %zu)",
                 scannedNetworks.size(), knownNetworks.size());
        return std::nullopt;
    }

    // Evaluate all networks
    std::vector<NetworkScore> scores = evaluateNetworks(scannedNetworks, knownNetworks);
    
    if (scores.empty()) {
        ESP_LOGD(TAG, "No suitable networks found after evaluation");
        return std::nullopt;
    }

    // Find the best scoring network that we have credentials for
    for (const auto& score : scores) {
        if (score.hasCredentials && score.totalScore > 0) {
            // Find and return the corresponding KnownNetwork
            const KnownNetwork* known = findKnownNetwork(score.ssid, knownNetworks);
            if (known != nullptr) {
                ESP_LOGI(TAG, "Selected network: %s (score: %d, RSSI: %d, priority: %d)",
                         score.ssid.c_str(), score.totalScore, score.rssiScore, score.priorityScore);
                return *known;
            }
        }
    }

    ESP_LOGD(TAG, "No network with valid credentials found");
    return std::nullopt;
}

std::vector<NetworkScore> NetworkSelector::evaluateNetworks(
    const std::vector<ScannedNetwork>& scannedNetworks,
    const std::vector<KnownNetwork>& knownNetworks) const {
    
    std::vector<NetworkScore> scores;
    scores.reserve(scannedNetworks.size());

    for (const auto& scanned : scannedNetworks) {
        // Skip networks with signal too weak
        if (scanned.rssi < _config.minimumRssi) {
            ESP_LOGD(TAG, "Skipping %s: RSSI %d below minimum %d",
                     scanned.ssid, scanned.rssi, _config.minimumRssi);
            continue;
        }

        // Skip open networks if secure required
        if (_config.requireSecure && scanned.isOpen()) {
            ESP_LOGD(TAG, "Skipping %s: open network not allowed", scanned.ssid);
            continue;
        }

        NetworkScore score;
        score.ssid = scanned.ssid;
        score.isKnown = false;
        score.hasCredentials = false;

        // Find if this is a known network
        const KnownNetwork* known = findKnownNetwork(scanned.ssid, knownNetworks);
        
        if (known != nullptr) {
            score.isKnown = true;
            score.hasCredentials = true;
            
            // Calculate priority score (user-defined priority 0-100)
            score.priorityScore = known->priority;
            
            // Calculate history score
            score.historyScore = calculateHistoryScore(known->lastConnected);
            
            // Check if auto-connect is disabled
            if (!known->autoConnect) {
                ESP_LOGD(TAG, "Skipping %s: auto-connect disabled", scanned.ssid);
                continue;
            }
        } else {
            // Unknown network - can't connect without credentials
            if (_config.preferKnownNetworks) {
                continue;  // Skip unknown networks if preferring known
            }
            score.priorityScore = 0;
            score.historyScore = 0;
        }

        // Calculate RSSI score
        score.rssiScore = calculateRssiScore(scanned.rssi);

        // Calculate security score
        score.securityScore = calculateSecurityScore(scanned.getAuthMode());

        // Calculate total weighted score
        score.totalScore = static_cast<int>(
            score.priorityScore * _config.priorityWeight * 100 +
            score.rssiScore * _config.rssiWeight +
            score.securityScore * _config.securityWeight +
            score.historyScore * _config.historyWeight
        );

        // Bonus for known networks
        if (score.isKnown && _config.preferKnownNetworks) {
            score.totalScore += 50;  // Significant bonus for known networks
        }

        ESP_LOGD(TAG, "Network %s: total=%d (priority=%d, rssi=%d, security=%d, history=%d)",
                 score.ssid.c_str(), score.totalScore, score.priorityScore,
                 score.rssiScore, score.securityScore, score.historyScore);

        scores.push_back(score);
    }

    // Sort by total score (highest first)
    std::sort(scores.begin(), scores.end(), [](const NetworkScore& a, const NetworkScore& b) {
        return a.totalScore > b.totalScore;
    });

    return scores;
}

bool NetworkSelector::isBetterNetwork(
    const NetworkInfo& currentNetwork,
    const ScannedNetwork& candidateNetwork,
    const std::vector<KnownNetwork>& knownNetworks,
    int8_t rssiThreshold) const {
    
    // Can't roam to unknown network
    const KnownNetwork* known = findKnownNetwork(candidateNetwork.ssid, knownNetworks);
    if (known == nullptr) {
        return false;
    }

    // Don't roam to same network
    if (currentNetwork.ssid == candidateNetwork.ssid) {
        return false;
    }

    // Check if candidate has significantly better signal
    int8_t rssiDiff = candidateNetwork.rssi - currentNetwork.rssi;
    if (rssiDiff < rssiThreshold) {
        return false;
    }

    // Check if candidate has higher or equal priority
    const KnownNetwork* currentKnown = findKnownNetwork(currentNetwork.ssid, knownNetworks);
    if (currentKnown != nullptr && known->priority < currentKnown->priority) {
        // Candidate has lower priority - require even better signal
        if (rssiDiff < rssiThreshold * 2) {
            return false;
        }
    }

    ESP_LOGI(TAG, "Better network found: %s (RSSI: %d vs %d, diff: %d)",
             candidateNetwork.ssid, candidateNetwork.rssi, currentNetwork.rssi, rssiDiff);

    return true;
}

int NetworkSelector::calculateRssiScore(int8_t rssi) const {
    // RSSI typically ranges from -100 (worst) to -30 (best) dBm
    // Convert to 0-100 score
    
    if (rssi >= -30) {
        return 100;  // Excellent signal
    } else if (rssi <= -100) {
        return 0;    // Very poor signal
    }
    
    // Linear interpolation between -100 and -30
    // Score = (rssi + 100) * 100 / 70
    return static_cast<int>((rssi + 100) * 100 / 70);
}

int NetworkSelector::calculateSecurityScore(WiFiAuthMode authMode) const {
    // Higher security = higher score
    switch (authMode) {
        case WiFiAuthMode::WPA3:
            return 100;
        case WiFiAuthMode::Enterprise:
            return 95;
        case WiFiAuthMode::WPA2:
            return 80;
        case WiFiAuthMode::WPA:
            return 60;
        case WiFiAuthMode::WEP:
            return 30;  // WEP is deprecated and insecure
        case WiFiAuthMode::Open:
            return 10;  // Open networks get low score
        default:
            return 0;
    }
}

int NetworkSelector::calculateHistoryScore(uint32_t lastConnected) const {
    if (lastConnected == 0) {
        return 0;  // Never connected
    }

    uint32_t currentTime = nowSeconds();
    
    if (lastConnected > currentTime) {
        return 50;  // Invalid timestamp, give neutral score
    }

    uint32_t ageSeconds = currentTime - lastConnected;
    
    // Score decays over time
    // - Connected in last hour: 100
    // - Connected in last day: 80
    // - Connected in last week: 60
    // - Connected in last month: 40
    // - Older: 20
    
    constexpr uint32_t HOUR = 3600;
    constexpr uint32_t DAY = 86400;
    constexpr uint32_t WEEK = 604800;
    constexpr uint32_t MONTH = 2592000;
    
    if (ageSeconds < HOUR) {
        return 100;
    } else if (ageSeconds < DAY) {
        return 80;
    } else if (ageSeconds < WEEK) {
        return 60;
    } else if (ageSeconds < MONTH) {
        return 40;
    } else {
        return 20;
    }
}

const KnownNetwork* NetworkSelector::findKnownNetwork(
    const std::string& ssid,
    const std::vector<KnownNetwork>& knownNetworks) const {
    
    for (const auto& known : knownNetworks) {
        if (known.matchesSsid(ssid)) {
            return &known;
        }
    }
    return nullptr;
}
