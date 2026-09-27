#include "HttpClient.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"

#include <cstring>

HttpClient::HttpClient() :
    _responseBody(nullptr),
    _contentLength(-1) {
}

HttpClient::HttpClient(const HttpConfig& config) :
    _config(config),
    _responseBody(nullptr),
    _contentLength(-1) {
}

HttpClient::~HttpClient() {
    // Nothing to cleanup
}

// ========== Configuration ==========

HttpClient& HttpClient::setTimeout(uint32_t timeoutMs) {
    _config.timeoutMs = timeoutMs;
    return *this;
}

HttpClient& HttpClient::setHeader(const std::string& name, const std::string& value) {
    _headers[name] = value;
    return *this;
}

HttpClient& HttpClient::removeHeader(const std::string& name) {
    _headers.erase(name);
    return *this;
}

HttpClient& HttpClient::clearHeaders() {
    _headers.clear();
    return *this;
}

HttpClient& HttpClient::setCertPEM(const std::string& certPem) {
    _certPem = certPem;
    return *this;
}

HttpClient& HttpClient::setCertPEM(const uint8_t* certStart, const uint8_t* certEnd) {
    if (certStart != nullptr && certEnd != nullptr && certEnd > certStart) {
        _certPem = std::string(reinterpret_cast<const char*>(certStart), 
                               certEnd - certStart);
    }
    return *this;
}

HttpClient& HttpClient::setBasicAuth(const std::string& username, const std::string& password) {
    _username = username;
    _password = password;
    return *this;
}

HttpClient& HttpClient::setBearerAuth(const std::string& token) {
    return setHeader("Authorization", "Bearer " + token);
}

HttpClient& HttpClient::setProgressCallback(ProgressCallback callback) {
    _progressCallback = callback;
    return *this;
}

// ========== HTTP Methods ==========

HttpResponse HttpClient::get(const std::string& url) {
    return request(HttpMethod::GET, url, "");
}

HttpResponse HttpClient::post(const std::string& url,
                               const std::string& body,
                               const std::string& contentType) {
    setHeader("Content-Type", contentType);
    return request(HttpMethod::POST, url, body);
}

HttpResponse HttpClient::postJson(const std::string& url, const std::string& json) {
    return post(url, json, "application/json");
}

HttpResponse HttpClient::put(const std::string& url,
                              const std::string& body,
                              const std::string& contentType) {
    setHeader("Content-Type", contentType);
    return request(HttpMethod::PUT, url, body);
}

HttpResponse HttpClient::del(const std::string& url) {
    return request(HttpMethod::DELETE_, url, "");
}

HttpResponse HttpClient::head(const std::string& url) {
    return request(HttpMethod::HEAD, url, "");
}

HttpResponse HttpClient::patch(const std::string& url,
                                const std::string& body,
                                const std::string& contentType) {
    setHeader("Content-Type", contentType);
    return request(HttpMethod::PATCH, url, body);
}

HttpResponse HttpClient::request(HttpMethod method, 
                                  const std::string& url,
                                  const std::string& body) {
    return performRequest(method, url, body);
}

// ========== Internal ==========

int HttpClient::eventHandler(esp_http_client_event_t* event) {
    HttpClient* self = static_cast<HttpClient*>(event->user_data);

    switch (event->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "Header: %s = %s", event->header_key, event->header_value);
            break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", event->data_len);
            if (self != nullptr && self->_responseBody != nullptr && event->data_len > 0) {
                // Append data to response body
                self->_responseBody->append(
                    static_cast<const char*>(event->data), 
                    event->data_len
                );

                // Call progress callback if set
                if (self->_progressCallback) {
                    self->_progressCallback(
                        static_cast<int64_t>(self->_responseBody->size()),
                        self->_contentLength
                    );
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;

        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;

        default:
            break;
    }

    return ESP_OK;
}

int HttpClient::toEspMethod(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET:
            return HTTP_METHOD_GET;
        case HttpMethod::POST:
            return HTTP_METHOD_POST;
        case HttpMethod::PUT:
            return HTTP_METHOD_PUT;
        case HttpMethod::DELETE_:
            return HTTP_METHOD_DELETE;
        case HttpMethod::HEAD:
            return HTTP_METHOD_HEAD;
        case HttpMethod::PATCH:
            return HTTP_METHOD_PATCH;
        default:
            return HTTP_METHOD_GET;
    }
}

HttpResponse HttpClient::performRequest(HttpMethod method,
                                          const std::string& url,
                                          const std::string& body) {
    HttpResponse response;
    std::string responseBody;
    _responseBody = &responseBody;
    _contentLength = -1;

    uint64_t startTime = esp_timer_get_time();

    // Configure HTTP client
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = eventHandler;
    config.user_data = this;
    config.timeout_ms = static_cast<int>(_config.timeoutMs);
    config.buffer_size = static_cast<int>(_config.bufferSize);
    config.buffer_size_tx = static_cast<int>(_config.bufferSizeTx);
    config.disable_auto_redirect = !_config.followRedirects;
    config.max_redirection_count = _config.maxRedirects;
    config.keep_alive_enable = _config.keepAlive;

    // TLS configuration
    if (!_certPem.empty()) {
        config.cert_pem = _certPem.c_str();
        config.cert_len = _certPem.length() + 1;
    } else if (url.find("https://") == 0) {
        // Use bundle for HTTPS if no specific cert provided
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    // Skip verification if explicitly disabled (not recommended)
    if (_config.disableSslVerify) {
        config.skip_cert_common_name_check = true;
    }

    // Basic auth
    if (!_username.empty()) {
        config.username = _username.c_str();
        config.password = _password.c_str();
        config.auth_type = HTTP_AUTH_TYPE_BASIC;
    }

    // Create client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        response.success = false;
        response.errorMessage = "Failed to create HTTP client";
        ESP_LOGE(TAG, "%s", response.errorMessage.c_str());
        onError.trigger(url, response.errorMessage);
        _responseBody = nullptr;
        return response;
    }

    // Set method
    esp_http_client_set_method(client, static_cast<esp_http_client_method_t>(toEspMethod(method)));

    // Set custom headers
    for (const auto& header : _headers) {
        esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
    }

    // Set body for POST/PUT/PATCH
    if (!body.empty() && (method == HttpMethod::POST || 
                          method == HttpMethod::PUT || 
                          method == HttpMethod::PATCH)) {
        esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.length()));
    }

    // Perform request
    esp_err_t err = esp_http_client_perform(client);

    // Get results
    response.statusCode = esp_http_client_get_status_code(client);
    response.contentLength = esp_http_client_get_content_length(client);
    response.body = responseBody;
    response.durationMs = static_cast<uint32_t>((esp_timer_get_time() - startTime) / 1000);

    if (err == ESP_OK) {
        response.success = true;
        ESP_LOGI(TAG, "Request to %s completed: %d (%d bytes in %lu ms)",
                 url.c_str(), response.statusCode, 
                 static_cast<int>(response.body.length()),
                 (unsigned long)response.durationMs);
    } else {
        response.success = false;
        response.errorMessage = esp_err_to_name(err);
        ESP_LOGE(TAG, "Request to %s failed: %s", url.c_str(), response.errorMessage.c_str());
        onError.trigger(url, response.errorMessage);
    }

    // Cleanup
    esp_http_client_cleanup(client);
    _responseBody = nullptr;

    // Trigger completion event
    onComplete.trigger(response);

    return response;
}
