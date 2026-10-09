#include "NetworkCredentialStore.h"
#include "NetworkClock.h"
#include "NVS.h"
#include "CommonErrorCodes.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

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
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", err.description());
        return err;
    }

    // Load existing networks from NVS
    err = loadFromNvs();
    if (err != CommonErrorCodes::None && err != CommonErrorCodes::FileNotFound && 
        err != CommonErrorCodes::FileIsEmpty) {
        ESP_LOGE(TAG, "Failed to load networks from NVS: %s", err.description());
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
        ESP_LOGI(TAG, "Added network: %s", network.ssid);
    }

    // Persist to NVS
    ErrorCode err = saveToNvs();
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to save networks to NVS: %s", err.description());
        return err;
    }

    // Trigger event
    onNetworkSaved.trigger(network);

    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::removeNetwork(const std::string& ssid) {
    ErrorCode err = CommonErrorCodes::None;
    KnownNetwork* net = findNetwork(ssid, err);
    if (net == nullptr) {
        if (err == CommonErrorCodes::FileNotFound) {
            ESP_LOGW(TAG, "Network not found: %s", ssid.c_str());
        }
        return err;
    }

    // Remove from memory (indice no cache via aritmetica de ponteiro)
    _networks.erase(_networks.begin() + (net - _networks.data()));
    ESP_LOGI(TAG, "Removed network: %s", ssid.c_str());

    // Persist to NVS
    err = saveToNvs();
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to save networks to NVS: %s", err.description());
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

    // Apaga apenas as chaves deste namespace. NAO usar NVS::eraseData():
    // ele apaga a particao NVS inteira e levaria junto dados de outros
    // namespaces (webauth/pin do "celer", otadata, ...).
    ErrorCode err = saveToNvs();  // count=0 + limpeza das keys residuais
    if (err != CommonErrorCodes::None) {
        ESP_LOGW(TAG, "Failed to clear NVS keys: %s", err.description());
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
    ErrorCode err = CommonErrorCodes::None;
    const KnownNetwork* net = findNetwork(ssid, err);
    if (net == nullptr) {
        return err;
    }

    network = *net;
    return CommonErrorCodes::None;
}

bool NetworkCredentialStore::isKnownNetwork(const std::string& ssid) const {
    return findNetworkIndex(ssid) >= 0;
}

ErrorCode NetworkCredentialStore::setNetworkPriority(const std::string& ssid, int8_t priority) {
    ErrorCode err = CommonErrorCodes::None;
    KnownNetwork* net = findNetwork(ssid, err);
    if (net == nullptr) {
        return err;
    }

    // Clamp priority to valid range
    priority = std::max<int8_t>(0, std::min<int8_t>(100, priority));
    net->priority = priority;

    ESP_LOGI(TAG, "Set priority for %s to %d", ssid.c_str(), priority);

    return saveToNvs();
}

ErrorCode NetworkCredentialStore::updateLastConnected(const std::string& ssid, uint32_t timestamp) {
    ErrorCode err = CommonErrorCodes::None;
    KnownNetwork* net = findNetwork(ssid, err);
    if (net == nullptr) {
        return err;
    }

    // Use current time if timestamp is 0
    if (timestamp == 0) {
        timestamp = nowSeconds();  // Convert to seconds
    }

    net->lastConnected = timestamp;
    ESP_LOGD(TAG, "Updated lastConnected for %s to %lu", ssid.c_str(), 
             static_cast<unsigned long>(timestamp));

    // So em memoria (como o lastRssi): o timestamp e uptime (esp_timer), sem
    // sentido depois de um reboot, e cada conexao chamava isto DUAS vezes
    // (connect + evento) — cada saveToNvs reescrevia a lista inteira com
    // ~160 open/commit de NVS no caminho da conexao, desgastando a flash a
    // cada religada do WiFi (o watch religa a cada vez que a tela acende).
    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::updateLastRssi(const std::string& ssid, int8_t rssi) {
    ErrorCode err = CommonErrorCodes::None;
    KnownNetwork* net = findNetwork(ssid, err);
    if (net == nullptr) {
        return err;
    }

    net->lastRssi = rssi;

    // Don't save to NVS for RSSI updates (too frequent)
    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::setAutoConnect(const std::string& ssid, bool autoConnect) {
    ErrorCode err = CommonErrorCodes::None;
    KnownNetwork* net = findNetwork(ssid, err);
    if (net == nullptr) {
        return err;
    }

    net->autoConnect = autoConnect;
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
        ESP_LOGE(TAG, "Failed to read network count: %s", err.description());
        return err;
    }

    ESP_LOGI(TAG, "Loading %d networks from NVS...", count);

    // Load each network
    bool sawLegacy = false;
    for (uint8_t i = 0; i < count && i < NetworkStoreConstants::MAX_STORED_NETWORKS; i++) {
        KnownNetwork network;
        bool legacy = false;
        err = loadNetworkFromNvs(i, network, &legacy);
        if (legacy) sawLegacy = true;
        if (err == CommonErrorCodes::None) {
            _networks.push_back(network);
            ESP_LOGD(TAG, "Loaded network: %s (priority: %d)", network.ssid, network.priority);
        } else {
            ESP_LOGW(TAG, "Failed to load network at index %d: %s", i, err.description());
        }
    }

    // Entrada vinda do formato pipe legado: regrava tudo no formato por
    // campo (migracao transparente, uma unica vez)
    if (sawLegacy && !_networks.empty()) {
        ESP_LOGI(TAG, "Migrating legacy pipe format to per-field keys...");
        saveToNvs();
    }

    ESP_LOGI(TAG, "Successfully loaded %zu networks", _networks.size());
    return CommonErrorCodes::None;
}

// Keys por campo do formato v2: "net<i>" + sufixo curto (limite NVS: 15
// chars). Campos ausentes assumem default — redes salvas por versoes antigas
// ganham os campos novos sem invalidar o registro.
static std::string fieldKey(size_t index, const char* suffix) {
    char key[16];
    snprintf(key, sizeof(key), "net%u%s", (unsigned)index, suffix);
    return std::string(key);
}

static const char* F_SSID = "s";
static const char* F_PASS = "w";
static const char* F_PRIO = "pr";
static const char* F_RSSI = "r";
static const char* F_LASTC = "lc";
static const char* F_AUTO = "ac";
static const char* F_AUTH = "am";

ErrorCode NetworkCredentialStore::saveToNvs() {
    // Save the network count
    uint8_t count = static_cast<uint8_t>(_networks.size());
    ErrorCode err = NVS::storeValue<uint8_t>(NetworkStoreConstants::NVS_NAMESPACE,
                                             NetworkStoreConstants::INDEX_KEY, count, true);

    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to save network count: %s", err.description());
        return err;
    }

    // Save each network
    for (size_t i = 0; i < _networks.size(); i++) {
        err = saveNetworkToNvs(i, _networks[i]);
        if (err != CommonErrorCodes::None) {
            ESP_LOGE(TAG, "Failed to save network at index %zu: %s", i, err.description());
            return err;
        }
    }

    // Chaves residuais de redes removidas (indices >= count): sem isso, uma
    // rede apagada deixava senha/ssid fantasma no NVS para sempre.
    for (size_t i = _networks.size(); i < NetworkStoreConstants::MAX_STORED_NETWORKS; i++) {
        NVS::eraseKey(NetworkStoreConstants::NVS_NAMESPACE, fieldKey(i, F_SSID));
        NVS::eraseKey(NetworkStoreConstants::NVS_NAMESPACE, fieldKey(i, F_PASS));
        NVS::eraseKey(NetworkStoreConstants::NVS_NAMESPACE, fieldKey(i, F_PRIO));
        NVS::eraseKey(NetworkStoreConstants::NVS_NAMESPACE, fieldKey(i, F_RSSI));
        NVS::eraseKey(NetworkStoreConstants::NVS_NAMESPACE, fieldKey(i, F_LASTC));
        NVS::eraseKey(NetworkStoreConstants::NVS_NAMESPACE, fieldKey(i, F_AUTO));
        NVS::eraseKey(NetworkStoreConstants::NVS_NAMESPACE, fieldKey(i, F_AUTH));
        NVS::eraseKey(NetworkStoreConstants::NVS_NAMESPACE,
                      getNetworkKey(i));  // chave do formato pipe legado
    }

    ESP_LOGD(TAG, "Saved %zu networks to NVS", _networks.size());
    return CommonErrorCodes::None;
}

ErrorCode NetworkCredentialStore::saveNetworkToNvs(size_t index, const KnownNetwork& network) {
    const std::string ns = NetworkStoreConstants::NVS_NAMESPACE;
    ErrorCode err = NVS::storeValue<std::string>(ns, fieldKey(index, F_SSID), network.ssid, true);
    if (err != CommonErrorCodes::None) return err;
    err = NVS::storeValue<std::string>(ns, fieldKey(index, F_PASS), network.password, true);
    if (err != CommonErrorCodes::None) return err;
    err = NVS::storeValue<int8_t>(ns, fieldKey(index, F_PRIO), network.priority, true);
    if (err != CommonErrorCodes::None) return err;
    err = NVS::storeValue<int8_t>(ns, fieldKey(index, F_RSSI), network.lastRssi, true);
    if (err != CommonErrorCodes::None) return err;
    err = NVS::storeValue<uint32_t>(ns, fieldKey(index, F_LASTC), network.lastConnected, true);
    if (err != CommonErrorCodes::None) return err;
    err = NVS::storeValue<uint8_t>(ns, fieldKey(index, F_AUTO),
                                   network.autoConnect ? 1 : 0, true);
    if (err != CommonErrorCodes::None) return err;
    return NVS::storeValue<uint8_t>(ns, fieldKey(index, F_AUTH),
                                    static_cast<uint8_t>(network.authMode), true);
}

ErrorCode NetworkCredentialStore::loadNetworkFromNvs(size_t index, KnownNetwork& network,
                                                     bool* legacyUsed) {
    if (legacyUsed != nullptr) *legacyUsed = false;
    const std::string ns = NetworkStoreConstants::NVS_NAMESPACE;

    std::string ssid;
    ErrorCode err = NVS::readValue<std::string>(ns, fieldKey(index, F_SSID), ssid);
    if (err == CommonErrorCodes::None) {
        strncpy(network.ssid, ssid.c_str(), sizeof(network.ssid) - 1);
        network.ssid[sizeof(network.ssid) - 1] = '\0';

        std::string pass;
        if (NVS::readValue<std::string>(ns, fieldKey(index, F_PASS), pass) ==
            CommonErrorCodes::None) {
            strncpy(network.password, pass.c_str(), sizeof(network.password) - 1);
            network.password[sizeof(network.password) - 1] = '\0';
        } else {
            network.password[0] = '\0';
        }

        int8_t prio = 0, rssi = 0;
        uint32_t lastC = 0;
        uint8_t autoC = 1, auth = 0;
        NVS::readValue<int8_t>(ns, fieldKey(index, F_PRIO), prio);
        NVS::readValue<int8_t>(ns, fieldKey(index, F_RSSI), rssi);
        NVS::readValue<uint32_t>(ns, fieldKey(index, F_LASTC), lastC);
        NVS::readValue<uint8_t>(ns, fieldKey(index, F_AUTO), autoC);
        NVS::readValue<uint8_t>(ns, fieldKey(index, F_AUTH), auth);
        network.priority = prio;
        network.lastRssi = rssi;
        network.lastConnected = lastC;
        network.autoConnect = autoC != 0;
        network.authMode = static_cast<WiFiAuthMode>(auth);
        return CommonErrorCodes::None;
    }
    if (err != CommonErrorCodes::FileNotFound) return err;

    // Formato legado: "ssid|password|priority|lastRssi|lastConnected|autoConnect|authMode"
    // em uma unica string. Quebrava com '|' no SSID/senha e parseava numeros
    // sem try/catch. Mantido so para leitura + migracao.
    std::string serialized;
    err = NVS::readValue<std::string>(ns, getNetworkKey(index), serialized);
    if (err != CommonErrorCodes::None) return err;
    if (legacyUsed != nullptr) *legacyUsed = true;

    // Sem excecoes (o firmware compila com -fno-exceptions): numero invalido
    // marca a entrada como corrompida em vez de abortar.
    auto parseNum = [](const std::string& s, long& out) {
        if (s.empty()) return false;
        char* end = nullptr;
        out = strtol(s.c_str(), &end, 10);
        return end != nullptr && *end == '\0';
    };

    size_t pos = 0;
    int fieldIndex = 0;
    bool ok = true;
    while (ok && pos <= serialized.length()) {
        size_t bar = serialized.find('|', pos);
        std::string field = (bar == std::string::npos)
                                ? serialized.substr(pos)
                                : serialized.substr(pos, bar - pos);
        long num = 0;
        switch (fieldIndex) {
            case 0:
                strncpy(network.ssid, field.c_str(), sizeof(network.ssid) - 1);
                network.ssid[sizeof(network.ssid) - 1] = '\0';
                break;
            case 1:
                strncpy(network.password, field.c_str(), sizeof(network.password) - 1);
                network.password[sizeof(network.password) - 1] = '\0';
                break;
            case 2:
                ok = parseNum(field, num);
                network.priority = static_cast<int8_t>(num);
                break;
            case 3:
                ok = parseNum(field, num);
                network.lastRssi = static_cast<int8_t>(num);
                break;
            case 4:
                ok = parseNum(field, num);
                network.lastConnected = static_cast<uint32_t>(num);
                break;
            case 5:
                network.autoConnect = (field == "1");
                break;
            case 6:
                ok = parseNum(field, num);
                network.authMode = static_cast<WiFiAuthMode>(num);
                break;
        }
        fieldIndex++;
        if (bar == std::string::npos) break;
        pos = bar + 1;
    }
    if (!ok) {
        ESP_LOGE(TAG, "Corrupt legacy network entry at index %zu", index);
        return CommonErrorCodes::OperationFailed;
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

// Guarda compartilhada dos metodos que operam numa rede conhecida: valida o
// init e resolve o SSID para o registro no cache (nullptr = err diz o que
// houve: NotInitialized/FileNotFound). Substituia o par
// "if (!_initialized)... findNetworkIndex < 0..." repetido em 7 metodos.
KnownNetwork* NetworkCredentialStore::findNetwork(const std::string& ssid, ErrorCode& err) {
    if (!_initialized) {
        err = CommonErrorCodes::NotInitialized;
        return nullptr;
    }
    int index = findNetworkIndex(ssid);
    if (index < 0) {
        err = CommonErrorCodes::FileNotFound;
        return nullptr;
    }
    err = CommonErrorCodes::None;
    return &_networks[static_cast<size_t>(index)];
}

const KnownNetwork* NetworkCredentialStore::findNetwork(const std::string& ssid, ErrorCode& err) const {
    // delega ao nao-const: nenhum caminho muta o cache aqui
    return const_cast<NetworkCredentialStore*>(this)->findNetwork(ssid, err);
}

std::string NetworkCredentialStore::getNetworkKey(size_t index) {
    return std::string(NetworkStoreConstants::NETWORK_PREFIX) + std::to_string(index);
}
