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

HttpClient& HttpClient::setBufferSize(uint32_t bytes) {
    if (bytes >= 512 && bytes <= 64 * 1024) {
        _config.bufferSize = bytes;
    }
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

HttpClient& HttpClient::setBodySink(BodySink sink) {
    _bodySink = std::move(sink);
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
            // tamanho conhecido: o corpo cresce uma vez so (sem dobrar)
            if (self != nullptr && strcasecmp(event->header_key, "Content-Length") == 0) {
                self->_contentLength = strtoll(event->header_value, nullptr, 10);
            }
            break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", event->data_len);
            if (self != nullptr && self->_bodySink && event->data_len > 0 && !self->_bodyOom) {
                if (!self->_bodySink(static_cast<const char*>(event->data), (size_t)event->data_len)) {
                    self->_bodyOom = true;
                }
                self->_sinkBytes += (size_t)event->data_len;
                break;
            }
            if (self != nullptr && self->_responseBody != nullptr && event->data_len > 0 && !self->_bodyOom) {
                // Crescimento SEM abort: sem excecoes, o realloc do
                // std::string que falha chama abort() (medido: corpo de 8KB
                // num heap fragmentado da CYD derrubava o aparelho). Cresce
                // para o Content-Length (ou dobra) so se o bloco existir;
                // senao a requisicao vira erro limpo.
                std::string* b = self->_responseBody;
                const size_t need = b->size() + (size_t)event->data_len;
                if (need > b->capacity()) {
                    size_t want = b->capacity() * 2 > need ? b->capacity() * 2 : need;
                    if (want < 1024) want = 1024;
                    if (self->_contentLength > 0 && (size_t)self->_contentLength >= need) {
                        want = (size_t)self->_contentLength;
                    }
                    // sonda real: o TLSF pode recusar um bloco pouco maior que
                    // o pedido mesmo com o "maior bloco livre" acima dele
                    auto fits = [](size_t n) {
                        void* p = malloc(n + 1);
                        free(p);
                        return p != nullptr;
                    };
                    if (!fits(want)) want = need;
                    if (!fits(want)) {
                        self->_bodyOom = true;
                        ESP_LOGW(TAG, "corpo nao cabe: %u B", (unsigned)need);
                        break;
                    }
                    b->reserve(want);
                }
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
    _bodyOom = false;
    _sinkBytes = 0;

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
    if (err == ESP_OK && _bodyOom) err = ESP_ERR_NO_MEM;
    response.body = std::move(responseBody);
    response.durationMs = static_cast<uint32_t>((esp_timer_get_time() - startTime) / 1000);

    if (err == ESP_OK) {
        response.success = true;
        ESP_LOGI(TAG, "Request to %s completed: %d (%d bytes in %lu ms)",
                 url.c_str(), response.statusCode, 
                 static_cast<int>(response.body.length() + _sinkBytes),
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

int HttpClient::dlFileEventHandler(esp_http_client_event_t* evt) {
    HttpClient* self = static_cast<HttpClient*>(evt->user_data);
    if (self == nullptr) return 0;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            // esp_http_client ja acumula headers padrao; nada a fazer aqui
            break;
        case HTTP_EVENT_ON_DATA: {
            FILE* f = static_cast<FILE*>(self->_dlFile);
            if (f != nullptr && evt->data != nullptr && evt->data_len > 0) {
                size_t written = fwrite(evt->data, 1, evt->data_len, f);
                self->_dlReceived += (int64_t)written;
                if (self->_progressCallback) {
                    self->_progressCallback(self->_dlReceived, self->_contentLength);
                }
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

HttpResponse HttpClient::downloadToFile(const std::string& url, const std::string& filePath) {
    HttpResponse response;
    _responseBody = nullptr;
    _contentLength = -1;
    _dlReceived = 0;

    uint64_t startTime = esp_timer_get_time();

    FILE* f = fopen(filePath.c_str(), "wb");
    if (f == nullptr) {
        response.errorMessage = "Cannot open " + filePath + " for writing";
        onError.trigger(url, response.errorMessage);
        return response;
    }
    _dlFile = f;

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = dlFileEventHandler;
    config.user_data = this;
    config.timeout_ms = static_cast<int>(_config.timeoutMs);
    config.buffer_size = static_cast<int>(_config.bufferSize);
    config.buffer_size_tx = static_cast<int>(_config.bufferSizeTx);
    config.disable_auto_redirect = !_config.followRedirects;
    config.max_redirection_count = _config.maxRedirects;
    config.keep_alive_enable = _config.keepAlive;

    if (!_certPem.empty()) {
        config.cert_pem = _certPem.c_str();
        config.cert_len = _certPem.length() + 1;
    } else if (url.find("https://") == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    if (_config.disableSslVerify) {
        config.skip_cert_common_name_check = true;
    }
    if (!_username.empty()) {
        config.username = _username.c_str();
        config.password = _password.c_str();
        config.auth_type = HTTP_AUTH_TYPE_BASIC;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = (client != nullptr) ? esp_http_client_perform(client) : ESP_FAIL;

    if (client != nullptr) {
        response.statusCode = esp_http_client_get_status_code(client);
        response.contentLength = esp_http_client_get_content_length(client);
        esp_http_client_cleanup(client);
    }

    fclose(f);
    _dlFile = nullptr;

    response.success = (err == ESP_OK && response.statusCode >= 200 && response.statusCode < 300);
    response.durationMs = (uint32_t)((esp_timer_get_time() - startTime) / 1000);

    if (!response.success) {
        if (err != ESP_OK) {
            response.errorMessage = esp_err_to_name(err);
        } else {
            response.errorMessage = "HTTP status " + std::to_string(response.statusCode);
        }
        remove(filePath.c_str());  // nao deixa arquivo parcial
        onError.trigger(url, response.errorMessage);
        return response;
    }

    onComplete.trigger(response);
    return response;
}
