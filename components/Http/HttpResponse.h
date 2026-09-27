#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <string>
#include <map>
#include <cstdint>
#include <cstring>

/**
 * @file HttpResponse.h
 * @brief HTTP response structure.
 */

/**
 * @struct HttpResponse
 * @brief Contains the result of an HTTP request.
 */
struct HttpResponse {
    bool success;                               /**< True if request completed without errors */
    int statusCode;                             /**< HTTP status code (200, 404, etc.) */
    std::string body;                           /**< Response body */
    std::map<std::string, std::string> headers; /**< Response headers */
    std::string errorMessage;                   /**< Error message if !success */
    int64_t contentLength;                      /**< Content length (-1 if unknown) */
    uint32_t durationMs;                        /**< Request duration in milliseconds */

    HttpResponse() :
        success(false),
        statusCode(0),
        contentLength(-1),
        durationMs(0) {}

    /**
     * @brief Check if status code indicates success (2xx).
     * @return True if status is 200-299.
     */
    bool isOk() const {
        return success && statusCode >= 200 && statusCode < 300;
    }

    /**
     * @brief Check if response is a redirect (3xx).
     * @return True if status is 300-399.
     */
    bool isRedirect() const {
        return statusCode >= 300 && statusCode < 400;
    }

    /**
     * @brief Check if response is client error (4xx).
     * @return True if status is 400-499.
     */
    bool isClientError() const {
        return statusCode >= 400 && statusCode < 500;
    }

    /**
     * @brief Check if response is server error (5xx).
     * @return True if status is 500-599.
     */
    bool isServerError() const {
        return statusCode >= 500 && statusCode < 600;
    }

    /**
     * @brief Get a header value by name (case-insensitive).
     * @param name Header name.
     * @return Header value or empty string if not found.
     */
    std::string getHeader(const std::string& name) const {
        // Simple case-insensitive search
        for (const auto& pair : headers) {
            if (strcasecmp(pair.first.c_str(), name.c_str()) == 0) {
                return pair.second;
            }
        }
        return "";
    }
};

#endif // HTTP_RESPONSE_H
