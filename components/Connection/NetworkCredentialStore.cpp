#include "NetworkCredentialStore.h"
#include "NVS.h"
#include "CommonErrorCodes.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstring>

NetworkCredentialStore::NetworkCredentialStore() 
    : _initialized(false) {
}

NetworkCredentialStore& NetworkCredentialStore::instance() {
    static NetworkCredentialStore store;
    return store;
}

ErrorCode NetworkCredentialStore::init() {
    if (_initialized) {
        return CommonErrorCodes::None;
    }

    ESP_LOGI(TAG, "Initializing NetworkCredentialStore...");

    // Initialize NVS if not already done
    ErrorCode err = NVS::initialize();
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", err.description().c_str());
        return err;
    }

    // Load existing networks from NVS
    err = loadFromNvs();
    if (err != CommonErrorCodes::None && err != CommonErrorCodes::FileNotFound && 
        err != CommonErrorCodes::FileIsEmpty) {
        ESP_LOGE(TAG, "Failed to load networks from NVS: %s", err.description().c_str());
        return err;
    }

    _initialized = true;
    ESP_LOGI(TAG, "NetworkCredentialStore initialized with %zu networks", _networks.size());
    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::saveNetwork(const KnownNetwork& network) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    if (strlen(network.ssid) == 0) {
        ESP_LOGE(TAG, "Cannot save network with empty SSID");
        return CommonErrorCodes::ArgumentError;
    }

    // Check if network already exists
    int existingIndex = findNetworkIndex(network.ssid);
    
    if (existingIndex >= 0) {
        // Update existing network
        _networks[existingIndex] = network;
        ESP_LOGI(TAG, "Updated network: %s", network.ssid);
    } else {
        // Check if we have room for more networks
        if (_networks.size() >= NetworkStoreConstants::MAX_STORED_NETWORKS) {
            ESP_LOGE(TAG, "Maximum number of stored networks reached (%zu)", 
                     NetworkStoreConstants::MAX_STORED_NETWORKS);
            return CommonErrorCodes::StorageFull;
        }
        
        // Add new network
        _networks.push_back(network);
        ESP_LOGI(TAG, "Added new network: %s", network.ssid);
    }

    // Persist to NVS
    ErrorCode err = saveToNvs();
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to save networks to NVS: %s", err.description().c_str());
        return err;
    }

    // Trigger event
    onNetworkSaved.trigger(network);

    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::removeNetwork(const std::string& ssid) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    int index = findNetworkIndex(ssid);
    if (index < 0) {
        ESP_LOGW(TAG, "Network not found: %s", ssid.c_str());
        return CommonErrorCodes::FileNotFound;
    }

    // Remove from memory
    _networks.erase(_networks.begin() + index);
    ESP_LOGI(TAG, "Removed network: %s", ssid.c_str());

    // Persist to NVS
    ErrorCode err = saveToNvs();
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to save networks to NVS: %s", err.description().c_str());
        return err;
    }

    // Trigger event
    onNetworkRemoved.trigger(ssid);

    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::clearAllNetworks() {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    ESP_LOGI(TAG, "Clearing all stored networks...");

    // Store SSIDs for events before clearing
    std::vector<std::string> ssids;
    for (const auto& net : _networks) {
        ssids.push_back(net.ssid);
    }

    _networks.clear();

    // Clear NVS
    ErrorCode err = NVS::eraseData();
    if (err != CommonErrorCodes::None) {
        ESP_LOGW(TAG, "Failed to erase NVS data: %s", err.description().c_str());
    }

    // Trigger events for each removed network
    for (const auto& ssid : ssids) {
        onNetworkRemoved.trigger(ssid);
    }

    ESP_LOGI(TAG, "All networks cleared");
    return CommonErrorCodes::None;
}

std::vector<KnownNetwork> NetworkCredentialStore::getKnownNetworks() const {
    return _networks;
}

ErrorCode NetworkCredentialStore::getNetwork(const std::string& ssid, KnownNetwork& network) const {
    int index = findNetworkIndex(ssid);
    if (index < 0) {
        return CommonErrorCodes::FileNotFound;
    }

    network = _networks[index];
    return CommonErrorCodes::None;
}

bool NetworkCredentialStore::isKnownNetwork(const std::string& ssid) const {
    return findNetworkIndex(ssid) >= 0;
}

ErrorCode NetworkCredentialStore::setNetworkPriority(const std::string& ssid, int8_t priority) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    int index = findNetworkIndex(ssid);
    if (index < 0) {
        return CommonErrorCodes::FileNotFound;
    }

    // Clamp priority to valid range
    priority = std::max<int8_t>(0, std::min<int8_t>(100, priority));
    _networks[index].priority = priority;

    ESP_LOGI(TAG, "Set priority for %s to %d", ssid.c_str(), priority);

    return saveToNvs();
}

ErrorCode NetworkCredentialStore::updateLastConnected(const std::string& ssid, uint32_t timestamp) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    int index = findNetworkIndex(ssid);
    if (index < 0) {
        return CommonErrorCodes::FileNotFound;
    }

    // Use current time if timestamp is 0
    if (timestamp == 0) {
        timestamp = static_cast<uint32_t>(esp_timer_get_time() / 1000000);  // Convert to seconds
    }

    _networks[index].lastConnected = timestamp;
    ESP_LOGD(TAG, "Updated lastConnected for %s to %lu", ssid.c_str(), 
             static_cast<unsigned long>(timestamp));

    return saveToNvs();
}

ErrorCode NetworkCredentialStore::updateLastRssi(const std::string& ssid, int8_t rssi) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    int index = findNetworkIndex(ssid);
    if (index < 0) {
        return CommonErrorCodes::FileNotFound;
    }

    _networks[index].lastRssi = rssi;
    
    // Don't save to NVS for RSSI updates (too frequent)
    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::setAutoConnect(const std::string& ssid, bool autoConnect) {
    if (!_initialized) {
        return CommonErrorCodes::NotInitialized;
    }

    int index = findNetworkIndex(ssid);
    if (index < 0) {
        return CommonErrorCodes::FileNotFound;
    }

    _networks[index].autoConnect = autoConnect;
    ESP_LOGI(TAG, "Set autoConnect for %s to %s", ssid.c_str(), autoConnect ? "true" : "false");

    return saveToNvs();
}

size_t NetworkCredentialStore::getNetworkCount() const {
    return _networks.size();
}

std::vector<KnownNetwork> NetworkCredentialStore::getNetworksByPriority() const {
    std::vector<KnownNetwork> sorted = _networks;
    std::sort(sorted.begin(), sorted.end(), compareByPriority);
    return sorted;
}

std::vector<KnownNetwork> NetworkCredentialStore::getAutoConnectNetworks() const {
    std::vector<KnownNetwork> result;
    for (const auto& network : _networks) {
        if (network.autoConnect) {
            result.push_back(network);
        }
    }
    // Sort by priority
    std::sort(result.begin(), result.end(), compareByPriority);
    return result;
}

// Private methods

ErrorCode NetworkCredentialStore::loadFromNvs() {
    _networks.clear();

    // Read the network count
    uint8_t count = 0;
    ErrorCode err = NVS::readValue<uint8_t>(NetworkStoreConstants::NVS_NAMESPACE, 
                                            NetworkStoreConstants::INDEX_KEY, count);
    
    if (err == CommonErrorCodes::FileNotFound) {
        ESP_LOGI(TAG, "No stored networks found");
        return CommonErrorCodes::None;
    }
    
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to read network count: %s", err.description().c_str());
        return err;
    }

    ESP_LOGI(TAG, "Loading %d networks from NVS...", count);

    // Load each network
    for (uint8_t i = 0; i < count; i++) {
        KnownNetwork network;
        err = loadNetworkFromNvs(i, network);
        if (err == CommonErrorCodes::None) {
            _networks.push_back(network);
            ESP_LOGD(TAG, "Loaded network: %s (priority: %d)", network.ssid, network.priority);
        } else {
            ESP_LOGW(TAG, "Failed to load network at index %d: %s", i, err.description().c_str());
        }
    }

    ESP_LOGI(TAG, "Successfully loaded %zu networks", _networks.size());
    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::saveToNvs() {
    // Save the network count
    uint8_t count = static_cast<uint8_t>(_networks.size());
    ErrorCode err = NVS::storeValue<uint8_t>(NetworkStoreConstants::NVS_NAMESPACE,
                                             NetworkStoreConstants::INDEX_KEY, count, true);
    
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to save network count: %s", err.description().c_str());
        return err;
    }

    // Save each network
    for (size_t i = 0; i < _networks.size(); i++) {
        err = saveNetworkToNvs(i, _networks[i]);
        if (err != CommonErrorCodes::None) {
            ESP_LOGE(TAG, "Failed to save network at index %zu: %s", i, err.description().c_str());
            return err;
        }
    }

    ESP_LOGD(TAG, "Saved %zu networks to NVS", _networks.size());
    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::saveNetworkToNvs(size_t index, const KnownNetwork& network) {
    std::string key = getNetworkKey(index);
    
    // Serialize network to a string format: ssid|password|priority|lastRssi|lastConnected|autoConnect|authMode
    // We use a simple format that can be stored as a single NVS string
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s|%s|%d|%d|%lu|%d|%d",
             network.ssid,
             network.password,
             static_cast<int>(network.priority),
             static_cast<int>(network.lastRssi),
             static_cast<unsigned long>(network.lastConnected),
             network.autoConnect ? 1 : 0,
             static_cast<int>(network.authMode));

    std::string serialized(buffer);
    return NVS::storeValue<std::string>(NetworkStoreConstants::NVS_NAMESPACE, key, serialized, true);
}

ErrorCode NetworkCredentialStore::loadNetworkFromNvs(size_t index, KnownNetwork& network) {
    std::string key = getNetworkKey(index);
    std::string serialized;
    
    ErrorCode err = NVS::readValue<std::string>(NetworkStoreConstants::NVS_NAMESPACE, key, serialized);
    if (err != CommonErrorCodes::None) {
        return err;
    }

    // Parse the serialized format: ssid|password|priority|lastRssi|lastConnected|autoConnect|authMode
    // Find delimiters
    size_t pos = 0;
    size_t nextPos;
    int fieldIndex = 0;
    
    while ((nextPos = serialized.find('|', pos)) != std::string::npos || pos < serialized.length()) {
        std::string field;
        if (nextPos != std::string::npos) {
            field = serialized.substr(pos, nextPos - pos);
            pos = nextPos + 1;
        } else {
            field = serialized.substr(pos);
            pos = serialized.length();
        }

        switch (fieldIndex) {
            case 0: // ssid
                strncpy(network.ssid, field.c_str(), sizeof(network.ssid) - 1);
                network.ssid[sizeof(network.ssid) - 1] = '\0';
                break;
            case 1: // password
                strncpy(network.password, field.c_str(), sizeof(network.password) - 1);
                network.password[sizeof(network.password) - 1] = '\0';
                break;
            case 2: // priority
                network.priority = static_cast<int8_t>(std::stoi(field));
                break;
            case 3: // lastRssi
                network.lastRssi = static_cast<int8_t>(std::stoi(field));
                break;
            case 4: // lastConnected
                network.lastConnected = static_cast<uint32_t>(std::stoul(field));
                break;
            case 5: // autoConnect
                network.autoConnect = (field == "1");
                break;
            case 6: // authMode
                network.authMode = static_cast<WiFiAuthMode>(std::stoi(field));
                break;
        }
        fieldIndex++;

        if (nextPos == std::string::npos) break;
    }

    return CommonErrorCodes::None;
}

int NetworkCredentialStore::findNetworkIndex(const std::string& ssid) const {
    for (size_t i = 0; i < _networks.size(); i++) {
        if (_networks[i].matchesSsid(ssid)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string NetworkCredentialStore::getNetworkKey(size_t index) {
    return std::string(NetworkStoreConstants::NETWORK_PREFIX) + std::to_string(index);
}
