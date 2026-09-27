# HTTP Client para ESP32

Cliente HTTP simplificado para ESP32 com interface C++ moderna, suporte a TLS, autenticação e eventos.

## Índice

- [Características](#características)
- [Uso Básico](#uso-básico)
- [Configuração](#configuração)
- [Métodos HTTP](#métodos-http)
- [Autenticação](#autenticação)
- [TLS/HTTPS](#tlshttps)
- [Eventos](#eventos)
- [Exemplos](#exemplos)

---

## Características

- **Interface Fluente**: Métodos encadeáveis para configuração
- **Todos os Métodos HTTP**: GET, POST, PUT, DELETE, HEAD, PATCH
- **TLS/HTTPS**: Suporte a certificados PEM
- **Autenticação**: Basic Auth e Bearer Token
- **Headers**: Gerenciamento completo de headers
- **Eventos**: Callbacks para conclusão e erro
- **Progress**: Callback para progresso de download/upload

---

## Uso Básico

```cpp
#include "HttpClient.h"

HttpClient http;

// GET simples
HttpResponse resp = http.get("https://api.example.com/data");

if (resp.isOk()) {
    ESP_LOGI("HTTP", "Status: %d", resp.statusCode);
    ESP_LOGI("HTTP", "Body: %s", resp.body.c_str());
}
```

---

## Configuração

### HttpConfig

```cpp
HttpConfig config;
config.timeoutMs = 15000;        // 15 segundos
config.bufferSize = 2048;        // Buffer de recepção
config.followRedirects = true;   // Seguir redirects
config.maxRedirects = 5;         // Máximo de redirects
config.keepAlive = true;         // Manter conexão

HttpClient http(config);
```

### Configuração em Runtime

```cpp
HttpClient http;

// Timeout
http.setTimeout(10000);

// Headers
http.setHeader("Content-Type", "application/json")
    .setHeader("X-API-Key", "my-secret-key")
    .setHeader("Accept", "application/json");

// Limpar headers
http.clearHeaders();
```

---

## Métodos HTTP

### GET

```cpp
HttpResponse resp = http.get("https://api.example.com/users");

if (resp.isOk()) {
    // Processar resposta
}
```

### POST

```cpp
// POST com form data
HttpResponse resp = http.post(
    "https://api.example.com/login",
    "username=admin&password=123"
);

// POST com JSON
HttpResponse resp = http.postJson(
    "https://api.example.com/data",
    R"({"key": "value", "count": 42})"
);
```

### PUT

```cpp
HttpResponse resp = http.put(
    "https://api.example.com/users/1",
    R"({"name": "New Name"})",
    "application/json"
);
```

### DELETE

```cpp
HttpResponse resp = http.del("https://api.example.com/users/1");
```

### PATCH

```cpp
HttpResponse resp = http.patch(
    "https://api.example.com/users/1",
    R"({"status": "active"})"
);
```

### HEAD

```cpp
// Obter apenas headers (sem body)
HttpResponse resp = http.head("https://api.example.com/file.zip");

// Verificar tamanho do arquivo
auto it = resp.headers.find("Content-Length");
if (it != resp.headers.end()) {
    ESP_LOGI("HTTP", "Tamanho: %s bytes", it->second.c_str());
}
```

### Request Genérico

```cpp
HttpResponse resp = http.request(
    HttpMethod::POST,
    "https://api.example.com/data",
    R"({"key": "value"})"
);
```

---

## Autenticação

### Basic Auth

```cpp
http.setBasicAuth("username", "password");

HttpResponse resp = http.get("https://api.example.com/protected");
```

### Bearer Token

```cpp
http.setBearerAuth("eyJhbGciOiJIUzI1NiIs...");

HttpResponse resp = http.get("https://api.example.com/protected");
```

### Header Manual

```cpp
http.setHeader("Authorization", "ApiKey abc123");
```

---

## TLS/HTTPS

### Certificado Embutido

```cpp
// No CMakeLists.txt:
// target_add_binary_data(${PROJECT_NAME} "certs/api.pem" TEXT)

extern const uint8_t cert_start[] asm("_binary_api_pem_start");
extern const uint8_t cert_end[] asm("_binary_api_pem_end");

http.setCertPEM(cert_start, cert_end);
```

### Certificado como String

```cpp
const char* cert = "-----BEGIN CERTIFICATE-----\n"
                   "MIIC+jCCAeKgAwIBAgIJ...\n"
                   "-----END CERTIFICATE-----\n";

http.setCertPEM(cert);
```

### Desabilitar Verificação (NÃO RECOMENDADO)

```cpp
HttpConfig config;
config.disableSslVerify = true;  // Inseguro!
HttpClient http(config);
```

---

## HttpResponse

Estrutura de resposta:

```cpp
struct HttpResponse {
    int statusCode;                              // Código HTTP (200, 404, etc)
    std::string body;                            // Corpo da resposta
    std::map<std::string, std::string> headers;  // Headers da resposta
    std::string errorMessage;                    // Mensagem de erro (se houver)
    
    bool isOk() const;          // 2xx
    bool isRedirect() const;    // 3xx
    bool isClientError() const; // 4xx
    bool isServerError() const; // 5xx
    bool isError() const;       // 4xx ou 5xx ou erro de conexão
    bool isNotFound() const;    // 404
    bool isUnauthorized() const;// 401
    bool isForbidden() const;   // 403
};
```

### Verificando Resposta

```cpp
HttpResponse resp = http.get(url);

if (resp.isOk()) {
    // Sucesso (2xx)
    ESP_LOGI("HTTP", "Body: %s", resp.body.c_str());
} else if (resp.isUnauthorized()) {
    // 401 - Não autorizado
    ESP_LOGW("HTTP", "Token inválido");
} else if (resp.isNotFound()) {
    // 404 - Não encontrado
    ESP_LOGW("HTTP", "Recurso não existe");
} else if (resp.isError()) {
    // Outro erro
    ESP_LOGE("HTTP", "Erro %d: %s", resp.statusCode, resp.errorMessage.c_str());
}
```

---

## Eventos

### onComplete

```cpp
http.onComplete.addHandler([](const HttpResponse& resp) {
    ESP_LOGI("HTTP", "Requisição completa: %d", resp.statusCode);
});
```

### onError

```cpp
http.onError.addHandler([](const std::string& url, const std::string& error) {
    ESP_LOGE("HTTP", "Erro em %s: %s", url.c_str(), error.c_str());
});
```

### Progress Callback

```cpp
http.setProgressCallback([](int64_t bytesTransferred, int64_t totalBytes) {
    if (totalBytes > 0) {
        int percent = (bytesTransferred * 100) / totalBytes;
        ESP_LOGI("HTTP", "Download: %d%%", percent);
    }
});
```

---

## Exemplos

### API REST Completa

```cpp
class ApiClient {
public:
    ApiClient(const std::string& baseUrl, const std::string& apiKey)
        : _baseUrl(baseUrl) {
        _http.setHeader("X-API-Key", apiKey);
        _http.setHeader("Content-Type", "application/json");
    }
    
    bool createUser(const std::string& name, const std::string& email) {
        char json[256];
        snprintf(json, sizeof(json),
            R"({"name": "%s", "email": "%s"})", name.c_str(), email.c_str());
        
        auto resp = _http.postJson(_baseUrl + "/users", json);
        return resp.isOk();
    }
    
    std::string getUser(int id) {
        char url[128];
        snprintf(url, sizeof(url), "%s/users/%d", _baseUrl.c_str(), id);
        
        auto resp = _http.get(url);
        return resp.isOk() ? resp.body : "";
    }
    
private:
    std::string _baseUrl;
    HttpClient _http;
};
```

### Download com Progresso

```cpp
void downloadFile(const std::string& url) {
    HttpClient http;
    
    http.setProgressCallback([](int64_t transferred, int64_t total) {
        if (total > 0) {
            ESP_LOGI("DL", "Progresso: %lld / %lld bytes", transferred, total);
        }
    });
    
    auto resp = http.get(url);
    
    if (resp.isOk()) {
        // Salvar resp.body em arquivo
        ESP_LOGI("DL", "Download completo: %d bytes", resp.body.size());
    }
}
```

### Webhook com Retry

```cpp
bool sendWebhook(const std::string& payload, int maxRetries = 3) {
    HttpClient http;
    http.setTimeout(5000);
    
    for (int i = 0; i < maxRetries; i++) {
        auto resp = http.postJson("https://webhook.example.com/notify", payload);
        
        if (resp.isOk()) {
            return true;
        }
        
        ESP_LOGW("Webhook", "Tentativa %d falhou: %d", i + 1, resp.statusCode);
        vTaskDelay(pdMS_TO_TICKS(1000 * (i + 1)));  // Backoff exponencial
    }
    
    return false;
}
```

---

## Referências

- [ESP-IDF HTTP Client](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_client.html)
