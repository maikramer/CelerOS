#ifndef SUPABASE_CLIENT_H
#define SUPABASE_CLIENT_H

#include <string>
#include <memory>
#include "SupabaseTypes.h"
#include "SupabaseQuery.h"
#include "HttpClient.h"
#include "ErrorCode.h"

/**
 * @file SupabaseClient.h
 * @brief Supabase client for ESP32 - PostgREST API wrapper.
 * 
 * Provides a complete API for interacting with Supabase:
 * - CRUD operations (INSERT, SELECT, UPDATE, DELETE, UPSERT)
 * - RPC (Remote Procedure Call) for database functions
 * - PostgREST query builder with filters
 * - Credential persistence
 */

namespace Supabase {

/**
 * @class SupabaseClient
 * @brief Main client for interacting with Supabase.
 * 
 * Usage:
 * @code
 * auto& supabase = SupabaseClient::instance();
 * supabase.init("https://xxx.supabase.co", "anon-key");
 * 
 * // INSERT
 * auto resp = supabase.from("users")
 *     .insert(R"({"name": "John", "email": "john@example.com"})");
 * 
 * // SELECT
 * auto resp = supabase.from("users")
 *     .select("id,name")
 *     .eq("status", "active")
 *     .limit(10)
 *     .execute();
 * 
 * // UPDATE
 * auto resp = supabase.from("users")
 *     .update(R"({"status": "inactive"})")
 *     .eq("id", "123")
 *     .execute();
 * 
 * // DELETE
 * auto resp = supabase.from("logs")
 *     .delete_()
 *     .lt("created_at", "2024-01-01")
 *     .execute();
 * 
 * // UPSERT
 * auto resp = supabase.from("devices")
 *     .upsert(R"({"device_id": "ABC", "last_seen": "2025-01-01"})", "device_id");
 * 
 * // RPC
 * auto resp = supabase.rpc("get_stats", R"({"date": "2025-01-01"})");
 * @endcode
 */
class SupabaseClient {
public:
    /**
     * @brief Get singleton instance.
     */
    static SupabaseClient& instance();
    
    /**
     * @brief Create a new authenticated client instance.
     * @param accessToken User's JWT access token.
     * @return Unique pointer to authenticated client.
     */
    static std::unique_ptr<SupabaseClient> createWithAuth(const std::string& accessToken);
    
    /**
     * @brief Create client instance from existing configuration with auth.
     * @param config Supabase configuration.
     * @param accessToken User's JWT access token.
     * @return Unique pointer to authenticated client.
     */
    static std::unique_ptr<SupabaseClient> create(const SupabaseConfig& config, 
                                                   const std::string& accessToken = "");
    
    // Prevent copying
    SupabaseClient(const SupabaseClient&) = delete;
    SupabaseClient& operator=(const SupabaseClient&) = delete;
    
    // Destructor must be public for unique_ptr factory methods
    ~SupabaseClient() = default;
    
    // ========== Initialization ==========
    
    /**
     * @brief Initialize client with URL and API key.
     * @param url Supabase project URL (e.g., https://xxx.supabase.co).
     * @param apiKey Supabase anon key.
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode init(const std::string& url, const std::string& apiKey);
    
    /**
     * @brief Load credentials from Storage (NVS).
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode loadCredentials();
    
    /**
     * @brief Save current credentials to Storage (NVS).
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode saveCredentials();
    
    /**
     * @brief Check if client is properly configured.
     */
    bool isConfigured() const { return config_.isValid(); }
    
    // ========== Configuration ==========
    
    /**
     * @brief Set current table for operations.
     * @param table Table name.
     * @return Reference for chaining.
     */
    SupabaseClient& from(const std::string& table);
    
    /**
     * @brief Set request timeout.
     * @param ms Timeout in milliseconds.
     */
    void setTimeout(uint32_t ms);
    
    /**
     * @brief Set TLS certificate (PEM format).
     * @param pem Certificate string.
     * @param len Certificate length.
     */
    void setCertificate(const char* pem, size_t len);
    
    /**
     * @brief Set TLS certificate from embedded binary.
     * @param certStart Start of certificate data.
     * @param certEnd End of certificate data.
     */
    void setCertificate(const uint8_t* certStart, const uint8_t* certEnd);
    
    /**
     * @brief Set default return preference for mutations.
     * @param pref Return preference.
     */
    void setDefaultReturn(ReturnPreference pref) { defaultReturn_ = pref; }
    
    // ========== CRUD Operations ==========
    
    /**
     * @brief INSERT - Insert a new record.
     * @param json JSON data to insert.
     * @param ret Return preference.
     * @return Response with result.
     */
    SupabaseResponse insert(const std::string& json, 
                            ReturnPreference ret = ReturnPreference::Minimal);
    
    /**
     * @brief UPSERT - Insert or update record (based on unique constraint).
     * @param json JSON data to upsert.
     * @param onConflict Column(s) to use for conflict detection.
     * @param resolution How to handle conflicts.
     * @return Response with result.
     */
    SupabaseResponse upsert(const std::string& json,
                            const std::string& onConflict = "",
                            ConflictResolution resolution = ConflictResolution::MergeDuplicates);
    
    /**
     * @brief SELECT - Start a select query builder.
     * @param columns Columns to select (default: "*").
     * @return Query builder for chaining.
     */
    SupabaseQuery select(const std::string& columns = "*");
    
    /**
     * @brief UPDATE - Start an update query builder.
     * @param json JSON data for update.
     * @return Query builder for chaining (add filters with eq(), etc.).
     */
    SupabaseQuery update(const std::string& json);
    
    /**
     * @brief DELETE - Start a delete query builder.
     * @return Query builder for chaining (add filters with eq(), etc.).
     */
    SupabaseQuery delete_();
    
    // ========== RPC ==========
    
    /**
     * @brief Call a database function (RPC).
     * @param functionName Function name.
     * @param params JSON parameters (default: "{}").
     * @return Response with result.
     */
    SupabaseResponse rpc(const std::string& functionName,
                         const std::string& params = "{}");
    
    // ========== Utilities ==========
    
    /**
     * @brief Test connection to Supabase.
     * @return True if connection successful.
     */
    bool testConnection();
    
    /**
     * @brief Get current project URL.
     */
    const std::string& getUrl() const { return config_.url; }
    
    /**
     * @brief Get current table name.
     */
    const std::string& getCurrentTable() const { return config_.tableName; }
    
    // ========== Authentication ==========
    
    /**
     * @brief Set access token for authenticated requests (RLS).
     * @param accessToken User's JWT access token.
     */
    void setAccessToken(const std::string& accessToken);
    
    /**
     * @brief Clear current access token.
     */
    void clearAccessToken();
    
    /**
     * @brief Create a copy of this client with user authentication.
     * @param accessToken User's JWT access token.
     * @return Unique pointer to authenticated client.
     */
    std::unique_ptr<SupabaseClient> withAuth(const std::string& accessToken) const;
    
    // ========== Internal (for SupabaseQuery) ==========
    
    /**
     * @brief Execute a query (called by SupabaseQuery::execute()).
     * @param query Query to execute.
     * @return Response with result.
     */
    SupabaseResponse executeQuery(const SupabaseQuery& query);

private:
    // Default constructor for singleton
    SupabaseClient();
    
    // Constructor for authenticated instances (used by factory methods)
    SupabaseClient(const SupabaseConfig& config, const std::string& accessToken);
    
    /**
     * @brief Build full URL for table endpoint.
     */
    std::string buildTableUrl(const std::string& table) const;
    
    /**
     * @brief Build full URL for RPC endpoint.
     */
    std::string buildRpcUrl(const std::string& function) const;
    
    /**
     * @brief Configure HTTP client with auth headers.
     */
    void configureHttpClient();
    
    /**
     * @brief Update Authorization header with current token.
     */
    void updateAuthHeader();
    
    /**
     * @brief Build Prefer header from options.
     */
    std::string buildPreferHeader(ReturnPreference ret, 
                                  CountOption count = CountOption::None,
                                  ConflictResolution* conflict = nullptr) const;
    
    /**
     * @brief Convert HttpResponse to SupabaseResponse.
     */
    SupabaseResponse toSupabaseResponse(const HttpResponse& httpResp, CountOption countOpt) const;
    
    /**
     * @brief Extract count from Content-Range header.
     */
    int extractCountFromHeaders(const HttpResponse& resp) const;
    
    SupabaseConfig config_;
    HttpClient httpClient_;
    std::string accessToken_;
    ReturnPreference defaultReturn_ = ReturnPreference::Minimal;
    bool initialized_ = false;
    
    static constexpr const char* TAG = "SupabaseClient";
};

} // namespace Supabase

#endif // SUPABASE_CLIENT_H
