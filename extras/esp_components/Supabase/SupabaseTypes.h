#ifndef SUPABASE_TYPES_H
#define SUPABASE_TYPES_H

#include <string>
#include <cstdint>

/**
 * @file SupabaseTypes.h
 * @brief Common types and enums for Supabase client.
 */

namespace Supabase {

/**
 * @enum ReturnPreference
 * @brief Controls what data is returned after INSERT/UPDATE/DELETE operations.
 */
enum class ReturnPreference {
    Minimal,        ///< Prefer: return=minimal (no data returned, fastest)
    Representation, ///< Prefer: return=representation (returns affected rows)
    Headers         ///< Prefer: return=headers-only (returns headers only)
};

/**
 * @enum ConflictResolution
 * @brief Controls how UPSERT handles conflicts.
 */
enum class ConflictResolution {
    MergeDuplicates,  ///< Prefer: resolution=merge-duplicates (update existing)
    IgnoreDuplicates  ///< Prefer: resolution=ignore-duplicates (skip conflicts)
};

/**
 * @enum CountOption
 * @brief Controls row counting in SELECT queries.
 */
enum class CountOption {
    None,      ///< No counting
    Exact,     ///< Prefer: count=exact (accurate count, slower)
    Planned,   ///< Prefer: count=planned (estimate from query plan)
    Estimated  ///< Prefer: count=estimated (rough estimate, fastest)
};

/**
 * @enum QueryType
 * @brief Type of query being executed.
 */
enum class QueryType {
    Select,
    Insert,
    Update,
    Delete,
    Upsert,
    Rpc
};

/**
 * @struct SupabaseConfig
 * @brief Configuration for SupabaseClient.
 */
struct SupabaseConfig {
    std::string url;         ///< Supabase project URL (e.g., https://xxx.supabase.co)
    std::string apiKey;      ///< Supabase anon key
    std::string tableName;   ///< Current table name
    uint32_t timeoutMs = 10000; ///< Request timeout in milliseconds
    const char* certPem = nullptr; ///< Optional TLS certificate (PEM format)
    size_t certLen = 0;      ///< Certificate length
    
    bool isValid() const {
        return !url.empty() && !apiKey.empty();
    }
};

/**
 * @struct SupabaseResponse
 * @brief Response from Supabase API calls.
 */
struct SupabaseResponse {
    int statusCode = 0;      ///< HTTP status code
    std::string body;        ///< Response body (JSON)
    bool success = false;    ///< True if request succeeded (2xx status)
    std::string error;       ///< Error message if failed
    int count = -1;          ///< Row count (-1 if not requested)
    
    /**
     * @brief Check if response indicates success (2xx status).
     */
    bool isOk() const { 
        return statusCode >= 200 && statusCode < 300; 
    }
    
    /**
     * @brief Check if response is 404 Not Found.
     */
    bool isNotFound() const { 
        return statusCode == 404; 
    }
    
    /**
     * @brief Check if response is unauthorized (401 or 403).
     */
    bool isUnauthorized() const { 
        return statusCode == 401 || statusCode == 403; 
    }
    
    /**
     * @brief Check if response is a server error (5xx status).
     */
    bool isServerError() const {
        return statusCode >= 500 && statusCode < 600;
    }
    
    /**
     * @brief Check if response is a client error (4xx status).
     */
    bool isClientError() const {
        return statusCode >= 400 && statusCode < 500;
    }
};

/**
 * @struct Filter
 * @brief Represents a PostgREST filter condition.
 */
struct Filter {
    std::string column;    ///< Column name
    std::string op;        ///< Operator (eq, neq, gt, gte, lt, lte, like, ilike, is, in, cs, cd, ov, fts)
    std::string value;     ///< Filter value
    
    /**
     * @brief Build query string parameter for this filter.
     */
    std::string toQueryParam() const {
        return column + "=" + op + "." + value;
    }
};

/**
 * @brief Storage keys for credentials persistence.
 */
namespace StorageKeys {
    constexpr const char* URL = "supabase_url";
    constexpr const char* API_KEY = "supabase_key";
    constexpr const char* TABLE = "supabase_tbl";
}

/**
 * @brief Convert ReturnPreference to Prefer header value.
 */
inline const char* toPreferHeader(ReturnPreference pref) {
    switch (pref) {
        case ReturnPreference::Representation:
            return "return=representation";
        case ReturnPreference::Headers:
            return "return=headers-only";
        case ReturnPreference::Minimal:
        default:
            return "return=minimal";
    }
}

/**
 * @brief Convert ConflictResolution to Prefer header value.
 */
inline const char* toPreferHeader(ConflictResolution res) {
    switch (res) {
        case ConflictResolution::IgnoreDuplicates:
            return "resolution=ignore-duplicates";
        case ConflictResolution::MergeDuplicates:
        default:
            return "resolution=merge-duplicates";
    }
}

/**
 * @brief Convert CountOption to Prefer header value.
 */
inline const char* toPreferHeader(CountOption opt) {
    switch (opt) {
        case CountOption::Exact:
            return "count=exact";
        case CountOption::Planned:
            return "count=planned";
        case CountOption::Estimated:
            return "count=estimated";
        case CountOption::None:
        default:
            return nullptr;
    }
}

} // namespace Supabase

#endif // SUPABASE_TYPES_H
