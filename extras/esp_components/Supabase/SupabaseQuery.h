#ifndef SUPABASE_QUERY_H
#define SUPABASE_QUERY_H

#include <string>
#include <vector>
#include "SupabaseTypes.h"

/**
 * @file SupabaseQuery.h
 * @brief Query builder for PostgREST queries.
 * 
 * Provides a fluent API for building queries with filters,
 * ordering, pagination, and other PostgREST features.
 */

namespace Supabase {

// Forward declaration
class SupabaseClient;

/**
 * @class SupabaseQuery
 * @brief Fluent query builder for PostgREST operations.
 * 
 * Usage:
 * @code
 * auto& supabase = SupabaseClient::instance();
 * 
 * // SELECT with filters
 * auto resp = supabase.from("users")
 *     .select("id,name,email")
 *     .eq("status", "active")
 *     .order("created_at", false)
 *     .limit(10)
 *     .execute();
 * 
 * // UPDATE with filter
 * auto resp = supabase.from("orders")
 *     .update(R"({"status": "shipped"})")
 *     .eq("id", "123")
 *     .execute();
 * @endcode
 */
class SupabaseQuery {
public:
    /**
     * @brief Construct a new query.
     * @param client Reference to SupabaseClient for execution.
     * @param table Table name.
     * @param type Query type (Select, Update, Delete).
     */
    SupabaseQuery(SupabaseClient& client, const std::string& table, QueryType type);
    
    /**
     * @brief Construct a query with data (for UPDATE).
     * @param client Reference to SupabaseClient for execution.
     * @param table Table name.
     * @param type Query type.
     * @param data JSON data for UPDATE.
     */
    SupabaseQuery(SupabaseClient& client, const std::string& table, 
                  QueryType type, const std::string& data);
    
    // ========== Equality Filters ==========
    
    /**
     * @brief Filter where column equals value.
     * @param column Column name.
     * @param value Value to match.
     * @return Reference for chaining.
     */
    SupabaseQuery& eq(const std::string& column, const std::string& value);
    
    /**
     * @brief Filter where column does not equal value.
     */
    SupabaseQuery& neq(const std::string& column, const std::string& value);
    
    // ========== Comparison Filters ==========
    
    /**
     * @brief Filter where column is greater than value.
     */
    SupabaseQuery& gt(const std::string& column, const std::string& value);
    
    /**
     * @brief Filter where column is greater than or equal to value.
     */
    SupabaseQuery& gte(const std::string& column, const std::string& value);
    
    /**
     * @brief Filter where column is less than value.
     */
    SupabaseQuery& lt(const std::string& column, const std::string& value);
    
    /**
     * @brief Filter where column is less than or equal to value.
     */
    SupabaseQuery& lte(const std::string& column, const std::string& value);
    
    // ========== Text Filters ==========
    
    /**
     * @brief Filter where column matches pattern (case-sensitive).
     * @param column Column name.
     * @param pattern Pattern with % as wildcard.
     */
    SupabaseQuery& like(const std::string& column, const std::string& pattern);
    
    /**
     * @brief Filter where column matches pattern (case-insensitive).
     */
    SupabaseQuery& ilike(const std::string& column, const std::string& pattern);
    
    // ========== Null/Boolean Filters ==========
    
    /**
     * @brief Filter where column is a specific value (null, true, false).
     * @param column Column name.
     * @param value "null", "true", or "false".
     */
    SupabaseQuery& is_(const std::string& column, const std::string& value);
    
    /**
     * @brief Filter where column is null.
     */
    SupabaseQuery& isNull(const std::string& column);
    
    /**
     * @brief Filter where column is not null.
     */
    SupabaseQuery& isNotNull(const std::string& column);
    
    // ========== List Filters ==========
    
    /**
     * @brief Filter where column value is in a list.
     * @param column Column name.
     * @param values List of values.
     */
    SupabaseQuery& in_(const std::string& column, const std::vector<std::string>& values);
    
    /**
     * @brief Filter where column value is in a comma-separated list.
     * @param column Column name.
     * @param valuesCsv Comma-separated values (e.g., "a,b,c").
     */
    SupabaseQuery& in_(const std::string& column, const std::string& valuesCsv);
    
    // ========== Array Filters ==========
    
    /**
     * @brief Filter where array column contains value(s).
     * @param column Column name.
     * @param arrayValue Value(s) in format "{a,b,c}".
     */
    SupabaseQuery& contains(const std::string& column, const std::string& arrayValue);
    
    /**
     * @brief Filter where array column is contained by value(s).
     */
    SupabaseQuery& containedBy(const std::string& column, const std::string& arrayValue);
    
    /**
     * @brief Filter where array column overlaps with value(s).
     */
    SupabaseQuery& overlaps(const std::string& column, const std::string& arrayValue);
    
    // ========== Full-Text Search ==========
    
    /**
     * @brief Full-text search on column.
     * @param column Column name.
     * @param query Search query.
     * @param config Language config (default: "english").
     */
    SupabaseQuery& textSearch(const std::string& column, const std::string& query,
                              const std::string& config = "english");
    
    // ========== Ordering ==========
    
    /**
     * @brief Order results by column.
     * @param column Column name.
     * @param ascending True for ASC, false for DESC.
     * @param nullsFirst True to put nulls first.
     */
    SupabaseQuery& order(const std::string& column, bool ascending = true,
                         bool nullsFirst = false);
    
    // ========== Pagination ==========
    
    /**
     * @brief Limit number of results.
     * @param count Maximum number of rows.
     */
    SupabaseQuery& limit(int count);
    
    /**
     * @brief Skip first N results.
     * @param start Number of rows to skip.
     */
    SupabaseQuery& offset(int start);
    
    /**
     * @brief Set range of results (using Range header).
     * @param from Start index (inclusive).
     * @param to End index (inclusive).
     */
    SupabaseQuery& range(int from, int to);
    
    // ========== Return Options ==========
    
    /**
     * @brief Return single object instead of array.
     * Adds Accept: application/vnd.pgrst.object+json header.
     */
    SupabaseQuery& single();
    
    /**
     * @brief Return single object or null if not found.
     */
    SupabaseQuery& maybeSingle();
    
    /**
     * @brief Request row count in response.
     * @param opt Count option (Exact, Planned, Estimated).
     */
    SupabaseQuery& count(CountOption opt = CountOption::Exact);
    
    // ========== Column Selection ==========
    
    /**
     * @brief Select specific columns (for SELECT queries).
     * @param cols Comma-separated column names.
     */
    SupabaseQuery& columns(const std::string& cols);
    
    // ========== Execution ==========
    
    /**
     * @brief Execute the query and return response.
     */
    SupabaseResponse execute();
    
    // ========== Getters (for SupabaseClient) ==========
    
    /**
     * @brief Get the table name.
     */
    const std::string& getTable() const { return table_; }
    
    /**
     * @brief Get the query type.
     */
    QueryType getType() const { return type_; }
    
    /**
     * @brief Get the data (for UPDATE).
     */
    const std::string& getData() const { return data_; }
    
    /**
     * @brief Build the query string.
     */
    std::string buildQueryString() const;
    
    /**
     * @brief Get filters for building URL.
     */
    const std::vector<Filter>& getFilters() const { return filters_; }
    
    /**
     * @brief Check if single object return is requested.
     */
    bool isSingle() const { return single_; }
    
    /**
     * @brief Check if maybe single return is requested.
     */
    bool isMaybeSingle() const { return maybeSingle_; }
    
    /**
     * @brief Get count option.
     */
    CountOption getCountOption() const { return countOption_; }
    
    /**
     * @brief Get range start (-1 if not set).
     */
    int getRangeFrom() const { return rangeFrom_; }
    
    /**
     * @brief Get range end (-1 if not set).
     */
    int getRangeTo() const { return rangeTo_; }

private:
    SupabaseClient& client_;
    std::string table_;
    QueryType type_;
    std::string data_;  // For UPDATE
    
    // Query components
    std::vector<Filter> filters_;
    std::string selectColumns_;
    std::string orderBy_;
    int limit_ = -1;
    int offset_ = -1;
    int rangeFrom_ = -1;
    int rangeTo_ = -1;
    bool single_ = false;
    bool maybeSingle_ = false;
    CountOption countOption_ = CountOption::None;
    
    /**
     * @brief Add a filter to the query.
     */
    void addFilter(const std::string& column, const std::string& op, const std::string& value);
    
    /**
     * @brief URL-encode a string.
     */
    static std::string urlEncode(const std::string& str);
};

} // namespace Supabase

#endif // SUPABASE_QUERY_H
