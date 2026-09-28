#include "SupabaseClient.h"
#include "Storage.h"
#include "CommonErrorCodes.h"
#include "StorageErrorCodes.h"
#include "esp_log.h"
#include <sstream>

namespace Supabase {

// ========== Singleton & Factory ==========

SupabaseClient& SupabaseClient::instance() {
    static SupabaseClient client;
    return client;
}

std::unique_ptr<SupabaseClient> SupabaseClient::createWithAuth(const std::string& accessToken) {
    auto& singleton = instance();
    if (!singleton.isConfigured()) {
        ESP_LOGE(TAG, "Cannot create authenticated client: singleton not initialized");
        return nullptr;
    }
    return std::unique_ptr<SupabaseClient>(
        new SupabaseClient(singleton.config_, accessToken)
    );
}

std::unique_ptr<SupabaseClient> SupabaseClient::create(const SupabaseConfig& config,
                                                        const std::string& accessToken) {
    return std::unique_ptr<SupabaseClient>(
        new SupabaseClient(config, accessToken)
    );
}

SupabaseClient::SupabaseClient() {
    // Default configuration
    config_.timeoutMs = 10000;
}

SupabaseClient::SupabaseClient(const SupabaseConfig& config, const std::string& accessToken)
    : config_(config)
    , accessToken_(accessToken)
    , initialized_(true) {
    configureHttpClient();
}

// ========== Initialization ==========

ErrorCode SupabaseClient::init(const std::string& url, const std::string& apiKey) {
    if (url.empty() || apiKey.empty()) {
        ESP_LOGE(TAG, "URL or API key is empty");
        return CommonErrorCodes::ArgumentError;
    }
    
    config_.url = url;
    config_.apiKey = apiKey;
    
    // Remove trailing slash from URL if present
    if (!config_.url.empty() && config_.url.back() == '/') {
        config_.url.pop_back();
    }
    
    configureHttpClient();
    initialized_ = true;
    
    ESP_LOGI(TAG, "Supabase client initialized for: %s", config_.url.c_str());
    return CommonErrorCodes::None;
}

ErrorCode SupabaseClient::loadCredentials() {
    std::string url, apiKey, table;
    
    // Load URL
    ErrorCode err = Storage::loadConfig(StorageKeys::URL, url);
    if (err != CommonErrorCodes::None) {
        if (err == CommonErrorCodes::FileNotFound || err == CommonErrorCodes::FileIsEmpty) {
            ESP_LOGD(TAG, "No saved credentials found");
            return CommonErrorCodes::FileNotFound;
        }
        ESP_LOGE(TAG, "Failed to load URL: %s", err.description().c_str());
        return CommonErrorCodes::StorageReadError;
    }
    
    // Load API Key
    err = Storage::loadConfig(StorageKeys::API_KEY, apiKey);
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to load API key: %s", err.description().c_str());
        return CommonErrorCodes::StorageReadError;
    }
    
    // Load table (optional)
    err = Storage::loadConfig(StorageKeys::TABLE, table);
    if (err == CommonErrorCodes::None && !table.empty()) {
        config_.tableName = table;
    }
    
    // Initialize with loaded credentials
    return init(url, apiKey);
}

ErrorCode SupabaseClient::saveCredentials() {
    if (!config_.isValid()) {
        ESP_LOGE(TAG, "Cannot save invalid credentials");
        return CommonErrorCodes::NotInitialized;
    }
    
    // Save URL
    ErrorCode err = Storage::storeConfig(StorageKeys::URL, config_.url, true);
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to save URL: %s", err.description().c_str());
        return CommonErrorCodes::StorageWriteError;
    }
    
    // Save API Key
    err = Storage::storeConfig(StorageKeys::API_KEY, config_.apiKey, true);
    if (err != CommonErrorCodes::None) {
        ESP_LOGE(TAG, "Failed to save API key: %s", err.description().c_str());
        return CommonErrorCodes::StorageWriteError;
    }
    
    // Save table if set
    if (!config_.tableName.empty()) {
        err = Storage::storeConfig(StorageKeys::TABLE, config_.tableName, true);
        if (err != CommonErrorCodes::None) {
            ESP_LOGW(TAG, "Failed to save table name: %s", err.description().c_str());
            // Not critical, continue
        }
    }
    
    ESP_LOGI(TAG, "Credentials saved successfully");
    return CommonErrorCodes::None;
}

// ========== Authentication ==========

void SupabaseClient::setAccessToken(const std::string& accessToken) {
    accessToken_ = accessToken;
    updateAuthHeader();
    ESP_LOGD(TAG, "Access token %s", accessToken.empty() ? "cleared" : "set");
}

void SupabaseClient::clearAccessToken() {
    accessToken_.clear();
    updateAuthHeader();
    ESP_LOGD(TAG, "Access token cleared");
}

std::unique_ptr<SupabaseClient> SupabaseClient::withAuth(const std::string& accessToken) const {
    return std::unique_ptr<SupabaseClient>(
        new SupabaseClient(config_, accessToken)
    );
}

// ========== Configuration ==========

SupabaseClient& SupabaseClient::from(const std::string& table) {
    config_.tableName = table;
    return *this;
}

void SupabaseClient::setTimeout(uint32_t ms) {
    config_.timeoutMs = ms;
    httpClient_.setTimeout(ms);
}

void SupabaseClient::setCertificate(const char* pem, size_t len) {
    config_.certPem = pem;
    config_.certLen = len;
    httpClient_.setCertPEM(std::string(pem, len));
}

void SupabaseClient::setCertificate(const uint8_t* certStart, const uint8_t* certEnd) {
    httpClient_.setCertPEM(certStart, certEnd);
}

// ========== CRUD Operations ==========

SupabaseResponse SupabaseClient::insert(const std::string& json, ReturnPreference ret) {
    if (!config_.isValid()) {
        SupabaseResponse resp;
        resp.error = "Client not configured";
        return resp;
    }
    
    if (config_.tableName.empty()) {
        SupabaseResponse resp;
        resp.error = "No table selected. Use from() first.";
        return resp;
    }
    
    std::string url = buildTableUrl(config_.tableName);
    
    // Build Prefer header
    std::string prefer = buildPreferHeader(ret);
    httpClient_.setHeader("Prefer", prefer);
    
    ESP_LOGD(TAG, "INSERT to %s: %s", url.c_str(), json.c_str());
    
    HttpResponse httpResp = httpClient_.postJson(url, json);
    return toSupabaseResponse(httpResp, CountOption::None);
}

SupabaseResponse SupabaseClient::upsert(const std::string& json,
                                         const std::string& onConflict,
                                         ConflictResolution resolution) {
    if (!config_.isValid()) {
        SupabaseResponse resp;
        resp.error = "Client not configured";
        return resp;
    }
    
    if (config_.tableName.empty()) {
        SupabaseResponse resp;
        resp.error = "No table selected. Use from() first.";
        return resp;
    }
    
    std::string url = buildTableUrl(config_.tableName);
    
    // Add on_conflict query parameter if specified
    if (!onConflict.empty()) {
        url += "?on_conflict=" + onConflict;
    }
    
    // Build Prefer header for upsert
    std::string prefer = buildPreferHeader(defaultReturn_, CountOption::None, &resolution);
    httpClient_.setHeader("Prefer", prefer);
    
    ESP_LOGD(TAG, "UPSERT to %s: %s", url.c_str(), json.c_str());
    
    HttpResponse httpResp = httpClient_.postJson(url, json);
    return toSupabaseResponse(httpResp, CountOption::None);
}

SupabaseQuery SupabaseClient::select(const std::string& columns) {
    SupabaseQuery query(*this, config_.tableName, QueryType::Select);
    if (columns != "*") {
        query.columns(columns);
    }
    return query;
}

SupabaseQuery SupabaseClient::update(const std::string& json) {
    return SupabaseQuery(*this, config_.tableName, QueryType::Update, json);
}

SupabaseQuery SupabaseClient::delete_() {
    return SupabaseQuery(*this, config_.tableName, QueryType::Delete);
}

// ========== RPC ==========

SupabaseResponse SupabaseClient::rpc(const std::string& functionName, const std::string& params) {
    if (!config_.isValid()) {
        SupabaseResponse resp;
        resp.error = "Client not configured";
        return resp;
    }
    
    std::string url = buildRpcUrl(functionName);
    
    ESP_LOGD(TAG, "RPC %s: %s", functionName.c_str(), params.c_str());
    
    HttpResponse httpResp = httpClient_.postJson(url, params);
    return toSupabaseResponse(httpResp, CountOption::None);
}

// ========== Utilities ==========

bool SupabaseClient::testConnection() {
    if (!config_.isValid()) {
        ESP_LOGE(TAG, "Client not configured");
        return false;
    }
    
    // Try a simple HEAD request to the root endpoint
    std::string url = config_.url + "/rest/v1/";
    
    HttpResponse resp = httpClient_.head(url);
    
    if (resp.isOk()) {
        ESP_LOGI(TAG, "Connection test successful (status: %d)", resp.statusCode);
        return true;
    } else {
        ESP_LOGW(TAG, "Connection test failed (status: %d): %s", 
                 resp.statusCode, resp.errorMessage.c_str());
        return false;
    }
}

// ========== Query Execution ==========

SupabaseResponse SupabaseClient::executeQuery(const SupabaseQuery& query) {
    if (!config_.isValid()) {
        SupabaseResponse resp;
        resp.error = "Client not configured";
        return resp;
    }
    
    const std::string& table = query.getTable();
    if (table.empty()) {
        SupabaseResponse resp;
        resp.error = "No table specified in query";
        return resp;
    }
    
    // Build URL with query string
    std::string url = buildTableUrl(table);
    std::string queryString = query.buildQueryString();
    if (!queryString.empty()) {
        url += "?" + queryString;
    }
    
    // Set headers based on query options
    if (query.isSingle()) {
        httpClient_.setHeader("Accept", "application/vnd.pgrst.object+json");
    } else if (query.isMaybeSingle()) {
        httpClient_.setHeader("Accept", "application/vnd.pgrst.object+json");
        // maybeSingle is handled in response
    } else {
        httpClient_.setHeader("Accept", "application/json");
    }
    
    // Set Range header if specified
    if (query.getRangeFrom() >= 0 && query.getRangeTo() >= 0) {
        std::ostringstream range;
        range << query.getRangeFrom() << "-" << query.getRangeTo();
        httpClient_.setHeader("Range", range.str());
    }
    
    // Build Prefer header
    CountOption countOpt = query.getCountOption();
    std::string prefer;
    
    HttpResponse httpResp;
    
    switch (query.getType()) {
        case QueryType::Select: {
            if (countOpt != CountOption::None) {
                const char* countHeader = toPreferHeader(countOpt);
                if (countHeader) {
                    httpClient_.setHeader("Prefer", countHeader);
                }
            }
            ESP_LOGD(TAG, "SELECT from %s", url.c_str());
            httpResp = httpClient_.get(url);
            break;
        }
        
        case QueryType::Update: {
            prefer = buildPreferHeader(defaultReturn_, countOpt);
            httpClient_.setHeader("Prefer", prefer);
            ESP_LOGD(TAG, "UPDATE %s: %s", url.c_str(), query.getData().c_str());
            httpResp = httpClient_.patch(url, query.getData(), "application/json");
            break;
        }
        
        case QueryType::Delete: {
            prefer = buildPreferHeader(defaultReturn_, countOpt);
            httpClient_.setHeader("Prefer", prefer);
            ESP_LOGD(TAG, "DELETE from %s", url.c_str());
            httpResp = httpClient_.del(url);
            break;
        }
        
        default: {
            SupabaseResponse resp;
            resp.error = "Unknown query type";
            return resp;
        }
    }
    
    return toSupabaseResponse(httpResp, countOpt);
}

// ========== Private Methods ==========

std::string SupabaseClient::buildTableUrl(const std::string& table) const {
    return config_.url + "/rest/v1/" + table;
}

std::string SupabaseClient::buildRpcUrl(const std::string& function) const {
    return config_.url + "/rest/v1/rpc/" + function;
}

void SupabaseClient::configureHttpClient() {
    httpClient_.setTimeout(config_.timeoutMs);
    
    // Set common headers
    httpClient_.setHeader("apikey", config_.apiKey);
    httpClient_.setHeader("Content-Type", "application/json");
    
    // Set authorization based on token state
    updateAuthHeader();
    
    // Set certificate if provided
    if (config_.certPem != nullptr && config_.certLen > 0) {
        httpClient_.setCertPEM(std::string(config_.certPem, config_.certLen));
    }
}

void SupabaseClient::updateAuthHeader() {
    // Use user access token if available, otherwise use anon key
    const std::string& token = accessToken_.empty() ? config_.apiKey : accessToken_;
    httpClient_.setHeader("Authorization", "Bearer " + token);
}

std::string SupabaseClient::buildPreferHeader(ReturnPreference ret, 
                                               CountOption count,
                                               ConflictResolution* conflict) const {
    std::ostringstream prefer;
    bool first = true;
    
    // Return preference
    const char* retHeader = toPreferHeader(ret);
    if (retHeader) {
        prefer << retHeader;
        first = false;
    }
    
    // Count option
    const char* countHeader = toPreferHeader(count);
    if (countHeader) {
        if (!first) prefer << ",";
        prefer << countHeader;
        first = false;
    }
    
    // Conflict resolution (for upsert)
    if (conflict) {
        if (!first) prefer << ",";
        prefer << toPreferHeader(*conflict);
    }
    
    return prefer.str();
}

SupabaseResponse SupabaseClient::toSupabaseResponse(const HttpResponse& httpResp, 
                                                     CountOption countOpt) const {
    SupabaseResponse resp;
    resp.statusCode = httpResp.statusCode;
    resp.body = httpResp.body;
    resp.success = httpResp.isOk();
    resp.error = httpResp.errorMessage;
    
    // Extract count from headers if requested
    if (countOpt != CountOption::None) {
        resp.count = extractCountFromHeaders(httpResp);
    }
    
    return resp;
}

int SupabaseClient::extractCountFromHeaders(const HttpResponse& resp) const {
    // PostgREST returns count in Content-Range header: 0-24/100
    // or in Preference-Applied header when using count preference
    
    // Try Content-Range first
    auto it = resp.headers.find("Content-Range");
    if (it != resp.headers.end()) {
        const std::string& range = it->second;
        size_t slashPos = range.find('/');
        if (slashPos != std::string::npos) {
            std::string countStr = range.substr(slashPos + 1);
            if (countStr != "*") {
                try {
                    return std::stoi(countStr);
                } catch (...) {
                    // Parse error, return -1
                }
            }
        }
    }
    
    // Also check lowercase (HTTP/2)
    it = resp.headers.find("content-range");
    if (it != resp.headers.end()) {
        const std::string& range = it->second;
        size_t slashPos = range.find('/');
        if (slashPos != std::string::npos) {
            std::string countStr = range.substr(slashPos + 1);
            if (countStr != "*") {
                try {
                    return std::stoi(countStr);
                } catch (...) {
                    // Parse error
                }
            }
        }
    }
    
    return -1;
}

} // namespace Supabase
