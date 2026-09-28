#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <map>
#include <functional>
#include "HttpResponse.h"
#include "Event.h"
#include "esp_http_client.h"

/**
 * @file HttpClient.h
 * @brief Simplified HTTP client wrapper for esp_http_client.
 * 
 * Provides a clean C++ interface for making HTTP requests with
 * support for headers, TLS, timeouts, and callbacks.
 */

/**
 * @enum HttpMethod
 * @brief HTTP request methods.
 */
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE_,  // DELETE is a reserved word in some contexts
    HEAD,
    PATCH
};

/**
 * @struct HttpConfig
 * @brief Configuration for HttpClient.
 */
struct HttpConfig {
    uint32_t timeoutMs;         /**< Request timeout in milliseconds */
    uint32_t bufferSize;        /**< Receive buffer size */
    uint32_t bufferSizeTx;      /**< Transmit buffer size */
    bool followRedirects;       /**< Follow HTTP redirects */
    uint8_t maxRedirects;       /**< Maximum redirects to follow */
    bool keepAlive;             /**< Use HTTP keep-alive */
    bool disableSslVerify;      /**< Disable SSL certificate verification (not recommended) */

    HttpConfig() :
        timeoutMs(10000),
        bufferSize(1024),
        bufferSizeTx(1024),
        followRedirects(true),
        maxRedirects(5),
        keepAlive(false),
        disableSslVerify(false) {}
};

/**
 * @typedef ProgressCallback
 * @brief Callback for download/upload progress.
 * @param bytesTransferred Bytes transferred so far.
 * @param totalBytes Total bytes (-1 if unknown).
 */
using ProgressCallback = std::function<void(int64_t bytesTransferred, int64_t totalBytes)>;

/**
 * @class HttpClient
 * @brief HTTP client for making web requests.
 * 
 * Usage:
 * @code
 * HttpClient http;
 * 
 * // Simple GET
 * HttpResponse resp = http.get("https://api.example.com/data");
 * if (resp.isOk()) {
 *     ESP_LOGI(TAG, "Body: %s", resp.body.c_str());
 * }
 * 
 * // POST with JSON
 * http.setHeader("Content-Type", "application/json");
 * resp = http.post("https://api.com/submit", "{\"key\":\"value\"}");
 * 
 * // With authentication
 * http.setHeader("Authorization", "Bearer my-token");
 * resp = http.get("https://api.com/protected");
 * @endcode
 */
class HttpClient {
public:
    /**
     * @brief Constructor with default configuration.
     */
    HttpClient();

    /**
     * @brief Constructor with custom configuration.
     * @param config HttpConfig structure.
     */
    explicit HttpClient(const HttpConfig& config);

    /**
     * @brief Destructor.
     */
    ~HttpClient();

    // Prevent copying
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // ========== Configuration ==========

    /**
     * @brief Set request timeout.
     * @param timeoutMs Timeout in milliseconds.
     * @return Reference to this for chaining.
     */
    HttpClient& setTimeout(uint32_t timeoutMs);

    /**
     * @brief Set the receive buffer size.
     *
     * Bigger buffers mean fewer progress callbacks per download (the
     * callback fires once per received chunk). Applies to the next request.
     * @param bytes Buffer size in bytes (default 1024).
     * @return Reference to this for chaining.
     */
    HttpClient& setBufferSize(uint32_t bytes);

    /**
     * @brief Set a request header.
     * @param name Header name.
     * @param value Header value.
     * @return Reference to this for chaining.
     */
    HttpClient& setHeader(const std::string& name, const std::string& value);

    /**
     * @brief Remove a request header.
     * @param name Header name to remove.
     * @return Reference to this for chaining.
     */
    HttpClient& removeHeader(const std::string& name);

    /**
     * @brief Clear all custom headers.
     * @return Reference to this for chaining.
     */
    HttpClient& clearHeaders();

    /**
     * @brief Set TLS certificate (PEM format).
     * @param certPem Certificate string.
     * @return Reference to this for chaining.
     */
    HttpClient& setCertPEM(const std::string& certPem);

    /**
     * @brief Set TLS certificate from embedded binary.
     * @param certStart Start of certificate data.
     * @param certEnd End of certificate data.
     * @return Reference to this for chaining.
     */
    HttpClient& setCertPEM(const uint8_t* certStart, const uint8_t* certEnd);

    /**
     * @brief Set Basic authentication.
     * @param username Username.
     * @param password Password.
     * @return Reference to this for chaining.
     */
    HttpClient& setBasicAuth(const std::string& username, const std::string& password);

    /**
     * @brief Set Bearer token authentication.
     * @param token Bearer token.
     * @return Reference to this for chaining.
     */
    HttpClient& setBearerAuth(const std::string& token);

    /**
     * @brief Set progress callback.
     * @param callback Progress callback function.
     * @return Reference to this for chaining.
     */
    HttpClient& setProgressCallback(ProgressCallback callback);

    /**
     * @brief Get current configuration.
     * @return HttpConfig structure.
     */
    const HttpConfig& getConfig() const { return _config; }

    // ========== HTTP Methods ==========

    /**
     * @brief Perform GET request.
     * @param url Request URL.
     * @return HttpResponse with result.
     */
    HttpResponse get(const std::string& url);

    /**
     * @brief Perform POST request.
     * @param url Request URL.
     * @param body Request body.
     * @param contentType Content-Type header (default: application/x-www-form-urlencoded).
     * @return HttpResponse with result.
     */
    HttpResponse post(const std::string& url, 
                      const std::string& body = "",
                      const std::string& contentType = "application/x-www-form-urlencoded");

    /**
     * @brief Perform POST request with JSON.
     * @param url Request URL.
     * @param json JSON body string.
     * @return HttpResponse with result.
     */
    HttpResponse postJson(const std::string& url, const std::string& json);

    /**
     * @brief Perform PUT request.
     * @param url Request URL.
     * @param body Request body.
     * @param contentType Content-Type header.
     * @return HttpResponse with result.
     */
    HttpResponse put(const std::string& url,
                     const std::string& body = "",
                     const std::string& contentType = "application/x-www-form-urlencoded");

    /**
     * @brief Perform DELETE request.
     * @param url Request URL.
     * @return HttpResponse with result.
     */
    HttpResponse del(const std::string& url);

    /**
     * @brief Perform HEAD request (get headers only).
     * @param url Request URL.
     * @return HttpResponse with headers (no body).
     */
    HttpResponse head(const std::string& url);

    /**
     * @brief Perform PATCH request.
     * @param url Request URL.
     * @param body Request body.
     * @param contentType Content-Type header.
     * @return HttpResponse with result.
     */
    HttpResponse patch(const std::string& url,
                       const std::string& body = "",
                       const std::string& contentType = "application/json");

    /**
     * @brief Perform generic request.
     * @param method HTTP method.
     * @param url Request URL.
     * @param body Request body (for POST, PUT, PATCH).
     * @return HttpResponse with result.
     */
    HttpResponse request(HttpMethod method, const std::string& url, 
                         const std::string& body = "");

    /**
     * @brief Download a URL to a file, streaming (low memory footprint).
     *
     * Uses the same config (timeout, redirects, TLS, auth) as the other
     * methods. The body is NOT buffered in RAM: HTTP_EVENT_ON_DATA chunks are
     * written straight to the file. Fires the progress callback with
     * (bytesTransferred, totalBytes). On failure the partial file is removed
     * and success=false.
     *
     * @param url Source URL.
     * @param filePath Destination VFS path (e.g. "/local/tmp/app.js").
     * @return HttpResponse (body empty; statusCode/contentLength valid).
     */
    HttpResponse downloadToFile(const std::string& url, const std::string& filePath);

    // ========== Events ==========

    /**
     * @brief Event triggered when request completes.
     * Parameter: HttpResponse
     */
    Event<const HttpResponse&> onComplete;

    /**
     * @brief Event triggered on error.
     * Parameters: URL, error message
     */
    Event<const std::string&, const std::string&> onError;

private:
    /**
     * @brief Internal request implementation.
     */
    HttpResponse performRequest(HttpMethod method, 
                                const std::string& url,
                                const std::string& body);

    /**
     * @brief HTTP event handler callback.
     */
    static int eventHandler(esp_http_client_event_t* evt);
    static int dlFileEventHandler(esp_http_client_event_t* evt);

    /**
     * @brief Convert HttpMethod to esp_http_client method.
     */
    static int toEspMethod(HttpMethod method);

    HttpConfig _config;
    std::map<std::string, std::string> _headers;
    std::string _certPem;
    std::string _username;
    std::string _password;
    ProgressCallback _progressCallback;

    // For event handler
    std::string* _responseBody;
    int64_t _contentLength;

    // For downloadToFile
    void* _dlFile = nullptr;        // FILE* em curso
    int64_t _dlReceived = 0;

    static constexpr const char* TAG = "HttpClient";
};

#endif // HTTP_CLIENT_H
