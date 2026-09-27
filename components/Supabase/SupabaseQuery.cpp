#include "SupabaseQuery.h"
#include "SupabaseClient.h"
#include <sstream>
#include <iomanip>
#include <cctype>

namespace Supabase {

// ========== Constructors ==========

SupabaseQuery::SupabaseQuery(SupabaseClient& client, const std::string& table, QueryType type)
    : client_(client)
    , table_(table)
    , type_(type)
{
}

SupabaseQuery::SupabaseQuery(SupabaseClient& client, const std::string& table, 
                             QueryType type, const std::string& data)
    : client_(client)
    , table_(table)
    , type_(type)
    , data_(data)
{
}

// ========== Equality Filters ==========

SupabaseQuery& SupabaseQuery::eq(const std::string& column, const std::string& value) {
    addFilter(column, "eq", value);
    return *this;
}

SupabaseQuery& SupabaseQuery::neq(const std::string& column, const std::string& value) {
    addFilter(column, "neq", value);
    return *this;
}

// ========== Comparison Filters ==========

SupabaseQuery& SupabaseQuery::gt(const std::string& column, const std::string& value) {
    addFilter(column, "gt", value);
    return *this;
}

SupabaseQuery& SupabaseQuery::gte(const std::string& column, const std::string& value) {
    addFilter(column, "gte", value);
    return *this;
}

SupabaseQuery& SupabaseQuery::lt(const std::string& column, const std::string& value) {
    addFilter(column, "lt", value);
    return *this;
}

SupabaseQuery& SupabaseQuery::lte(const std::string& column, const std::string& value) {
    addFilter(column, "lte", value);
    return *this;
}

// ========== Text Filters ==========

SupabaseQuery& SupabaseQuery::like(const std::string& column, const std::string& pattern) {
    addFilter(column, "like", pattern);
    return *this;
}

SupabaseQuery& SupabaseQuery::ilike(const std::string& column, const std::string& pattern) {
    addFilter(column, "ilike", pattern);
    return *this;
}

// ========== Null/Boolean Filters ==========

SupabaseQuery& SupabaseQuery::is_(const std::string& column, const std::string& value) {
    addFilter(column, "is", value);
    return *this;
}

SupabaseQuery& SupabaseQuery::isNull(const std::string& column) {
    return is_(column, "null");
}

SupabaseQuery& SupabaseQuery::isNotNull(const std::string& column) {
    addFilter(column, "not.is", "null");
    return *this;
}

// ========== List Filters ==========

SupabaseQuery& SupabaseQuery::in_(const std::string& column, const std::vector<std::string>& values) {
    std::ostringstream oss;
    oss << "(";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << ",";
        oss << values[i];
    }
    oss << ")";
    addFilter(column, "in", oss.str());
    return *this;
}

SupabaseQuery& SupabaseQuery::in_(const std::string& column, const std::string& valuesCsv) {
    std::string formatted = "(" + valuesCsv + ")";
    addFilter(column, "in", formatted);
    return *this;
}

// ========== Array Filters ==========

SupabaseQuery& SupabaseQuery::contains(const std::string& column, const std::string& arrayValue) {
    addFilter(column, "cs", arrayValue);
    return *this;
}

SupabaseQuery& SupabaseQuery::containedBy(const std::string& column, const std::string& arrayValue) {
    addFilter(column, "cd", arrayValue);
    return *this;
}

SupabaseQuery& SupabaseQuery::overlaps(const std::string& column, const std::string& arrayValue) {
    addFilter(column, "ov", arrayValue);
    return *this;
}

// ========== Full-Text Search ==========

SupabaseQuery& SupabaseQuery::textSearch(const std::string& column, const std::string& query,
                                         const std::string& config) {
    // Format: column=fts(config).query
    std::string op = "fts";
    if (!config.empty() && config != "english") {
        op = "fts(" + config + ")";
    }
    addFilter(column, op, query);
    return *this;
}

// ========== Ordering ==========

SupabaseQuery& SupabaseQuery::order(const std::string& column, bool ascending, bool nullsFirst) {
    std::ostringstream oss;
    oss << column;
    oss << (ascending ? ".asc" : ".desc");
    if (nullsFirst) {
        oss << ".nullsfirst";
    }
    
    if (!orderBy_.empty()) {
        orderBy_ += ",";
    }
    orderBy_ += oss.str();
    return *this;
}

// ========== Pagination ==========

SupabaseQuery& SupabaseQuery::limit(int count) {
    limit_ = count;
    return *this;
}

SupabaseQuery& SupabaseQuery::offset(int start) {
    offset_ = start;
    return *this;
}

SupabaseQuery& SupabaseQuery::range(int from, int to) {
    rangeFrom_ = from;
    rangeTo_ = to;
    return *this;
}

// ========== Return Options ==========

SupabaseQuery& SupabaseQuery::single() {
    single_ = true;
    return *this;
}

SupabaseQuery& SupabaseQuery::maybeSingle() {
    maybeSingle_ = true;
    return *this;
}

SupabaseQuery& SupabaseQuery::count(CountOption opt) {
    countOption_ = opt;
    return *this;
}

// ========== Column Selection ==========

SupabaseQuery& SupabaseQuery::columns(const std::string& cols) {
    selectColumns_ = cols;
    return *this;
}

// ========== Execution ==========

SupabaseResponse SupabaseQuery::execute() {
    return client_.executeQuery(*this);
}

// ========== Build Query String ==========

std::string SupabaseQuery::buildQueryString() const {
    std::ostringstream oss;
    bool first = true;
    
    // Add select columns
    if (!selectColumns_.empty() && type_ == QueryType::Select) {
        oss << "select=" << urlEncode(selectColumns_);
        first = false;
    }
    
    // Add filters
    for (const auto& filter : filters_) {
        if (!first) oss << "&";
        oss << urlEncode(filter.column) << "=" << filter.op << "." << urlEncode(filter.value);
        first = false;
    }
    
    // Add order
    if (!orderBy_.empty()) {
        if (!first) oss << "&";
        oss << "order=" << orderBy_;
        first = false;
    }
    
    // Add limit
    if (limit_ >= 0) {
        if (!first) oss << "&";
        oss << "limit=" << limit_;
        first = false;
    }
    
    // Add offset
    if (offset_ >= 0) {
        if (!first) oss << "&";
        oss << "offset=" << offset_;
        first = false;
    }
    
    return oss.str();
}

// ========== Private Methods ==========

void SupabaseQuery::addFilter(const std::string& column, const std::string& op, const std::string& value) {
    Filter filter;
    filter.column = column;
    filter.op = op;
    filter.value = value;
    filters_.push_back(filter);
}

std::string SupabaseQuery::urlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : str) {
        // Keep alphanumeric and other accepted characters intact
        if (std::isalnum(static_cast<unsigned char>(c)) || 
            c == '-' || c == '_' || c == '.' || c == '~' || 
            c == '*' || c == ',') {
            escaped << c;
        }
        // Any other characters are percent-encoded
        else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

} // namespace Supabase
