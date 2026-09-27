# Wifi Component

Componente para gerenciamento de conexões WiFi no ESP32, com arquitetura event-driven.

## Índice

- [Visão Geral](#visão-geral)
- [WifiConnection](#wificonnection)
- [Estados e Eventos](#estados-e-eventos)
- [WifiClient](#wificlient)
- [IPAddress](#ipaddress)
- [Exemplos](#exemplos)
- [OTA Updates](#ota-updates)

---

## Visão Geral

O componente Wifi fornece uma interface completa e event-driven para gerenciamento de conexões WiFi:

- **Event-driven**: Todos os eventos de WiFi são notificados através de callbacks
- **Scan assíncrono**: Busca de redes sem bloquear a aplicação
- **Retry automático**: Reconexão automática configurável
- **Monitoramento de sinal**: Eventos de mudança de RSSI
- **Integração com NetworkManager**: Para gerenciamento avançado de credenciais

### Arquitetura

```
┌─────────────────────────────────────────────────────────┐
│                    WifiConnection                        │
│  (Gerencia conexão WiFi STA)                            │
├─────────────────────────────────────────────────────────┤
│                      WifiClient                          │
│  (Transmissão de dados via socket)                      │
├─────────────────────────────────────────────────────────┤
│                   ESP-IDF WiFi Driver                    │
└─────────────────────────────────────────────────────────┘
```

---

## WifiConnection

Classe principal para gerenciamento de conexão WiFi em modo Station (STA).

### Estados

```cpp
enum class WiFiConnectionState {
    Idle,           // Não inicializado ou desconectado
    Initializing,   // Inicializando subsistema WiFi
    Scanning,       // Escaneando redes
    Connecting,     // Tentando conectar
    Connected,      // Conectado com sucesso
    Disconnecting,  // Desconectando
    Error           // Estado de erro
};
```

### API Principal

```cpp
class WifiConnection : public BaseConnection {
public:
    WifiConnection();
    ~WifiConnection();
    
    // Inicialização
    ErrorCode init();
    
    // Conexão
    ErrorCode connect(const std::string& ssid, const std::string& password, 
                      bool asyncConnect = false);
    void disconnect() override;
    
    // Estado
    bool isConnected() const override;
    WiFiConnectionState getState() const;
    
    // Informações de rede
    std::string getSSID() const;
    IPAddress getIPAddress() const;
    int8_t getRSSI() const;
    uint8_t getChannel() const;
    NetworkInfo getNetworkInfo() const;
    
    // Scan
    int scan(wifi_ap_record_t* ap_list, uint16_t max_aps);
    ErrorCode startScanAsync();
    WiFiScanResult getLastScanResults() const;
    
    // Configuração
    void setMaxRetries(uint8_t maxRetries);
    void setConnectionTimeout(uint32_t timeoutMs);
    
    // Transmissão
    ErrorCode sendRawData(const uint8_t* data, size_t length) const override;
    void setWifiClient(WifiClient* wifiClient);
};
```

### Eventos

| Evento | Parâmetros | Descrição |
|--------|------------|-----------|
| `onStateChanged` | `WifiConnection*`, `old_state`, `new_state` | Mudança de estado |
| `onScanStarted` | `WifiConnection*` | Scan iniciado |
| `onScanCompleted` | `WifiConnection*`, `WiFiScanResult&` | Scan concluído |
| `onConnecting` | `WifiConnection*`, `ssid` | Iniciando conexão |
| `onConnected` | `WifiConnection*`, `WiFiConnectionEvent&` | Conectado |
| `onAuthFailed` | `WifiConnection*`, `ssid`, `ErrorCode` | Falha de autenticação |
| `onDisconnected` | `WifiConnection*`, `WiFiConnectionEvent&` | Desconectado |
| `onSignalChanged` | `WifiConnection*`, `old_rssi`, `new_rssi` | Mudança de sinal |
| `onRetrying` | `WifiConnection*`, `retry_count`, `max_retries` | Tentando reconectar |

### Estruturas

```cpp
struct WiFiScanResult {
    std::vector<ScannedNetwork> networks;   // Redes encontradas
    int count;                               // Quantidade
    bool success;                            // Sucesso do scan
    ErrorCode error;                         // Erro (se houver)
};

struct WiFiConnectionEvent {
    std::string ssid;           // SSID da rede
    int8_t rssi;                // Força do sinal
    IPAddress ip;               // Endereço IP
    ErrorCode error;            // Código de erro
    uint8_t retryCount;         // Tentativas de reconexão
};
```

---

## Estados e Eventos

### Diagrama de Estados

```
                    ┌──────────────┐
                    │     Idle     │
                    └──────┬───────┘
                           │ init()
                    ┌──────▼───────┐
                    │ Initializing │
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
       scan() │            │ connect()  │
              │            │            │
       ┌──────▼────┐ ┌─────▼─────┐      │
       │  Scanning │ │Connecting │      │
       └──────┬────┘ └─────┬─────┘      │
              │            │            │
              │      ┌─────▼─────┐      │
              │      │ Connected │◄─────┘
              │      └─────┬─────┘
              │            │ disconnect()
              │      ┌─────▼───────┐
              │      │Disconnecting│
              │      └─────┬───────┘
              │            │
              └────────────▼────────────┐
                    ┌──────────┐        │
                    │   Idle   │        │
                    └──────────┘        │
                    ┌──────────┐        │
                    │  Error   │◄───────┘
                    └──────────┘
```

### Exemplo com Eventos

```cpp
#include "WifiConnection.h"

WifiConnection wifi;

void setupWifi() {
    // Registrar handlers de eventos
    wifi.onStateChanged.addHandler([](WifiConnection* conn, 
                                      WiFiConnectionState oldState, 
                                      WiFiConnectionState newState) {
        ESP_LOGI("WIFI", "Estado: %d -> %d", (int)oldState, (int)newState);
    });
    
    wifi.onConnected.addHandler([](WifiConnection* conn, 
                                   const WiFiConnectionEvent& event) {
        ESP_LOGI("WIFI", "Conectado a %s", event.ssid.c_str());
        ESP_LOGI("WIFI", "IP: %s", conn->getIPAddress().toString().c_str());
        ESP_LOGI("WIFI", "RSSI: %d dBm", event.rssi);
    });
    
    wifi.onDisconnected.addHandler([](WifiConnection* conn, 
                                      const WiFiConnectionEvent& event) {
        ESP_LOGW("WIFI", "Desconectado de %s", event.ssid.c_str());
    });
    
    wifi.onAuthFailed.addHandler([](WifiConnection* conn, 
                                    const std::string& ssid, 
                                    ErrorCode error) {
        ESP_LOGE("WIFI", "Falha de autenticação em %s: %s", 
                 ssid.c_str(), error.description().c_str());
    });
    
    wifi.onRetrying.addHandler([](WifiConnection* conn, 
                                  uint8_t retry, 
                                  uint8_t maxRetries) {
        ESP_LOGW("WIFI", "Reconectando... %d/%d", retry, maxRetries);
    });
    
    wifi.onSignalChanged.addHandler([](WifiConnection* conn, 
                                       int8_t oldRssi, 
                                       int8_t newRssi) {
        ESP_LOGI("WIFI", "Sinal: %d -> %d dBm", oldRssi, newRssi);
    });
    
    // Inicializar e conectar
    wifi.init();
    wifi.setMaxRetries(5);
    wifi.setConnectionTimeout(15000);
    
    wifi.connect("MinhaRede", "MinhaSenha", true);  // Async
}
```

---

## Scan de Redes

### Scan Síncrono (Blocking)

```cpp
wifi_ap_record_t ap_list[20];
int count = wifi.scan(ap_list, 20);

if (count > 0) {
    for (int i = 0; i < count; i++) {
        ESP_LOGI("SCAN", "SSID: %s, RSSI: %d, Canal: %d",
                 (char*)ap_list[i].ssid,
                 ap_list[i].rssi,
                 ap_list[i].primary);
    }
}
```

### Scan Assíncrono

```cpp
wifi.onScanStarted.addHandler([](WifiConnection* conn) {
    ESP_LOGI("SCAN", "Scan iniciado...");
});

wifi.onScanCompleted.addHandler([](WifiConnection* conn, 
                                   const WiFiScanResult& result) {
    if (result.success) {
        ESP_LOGI("SCAN", "Encontradas %d redes", result.count);
        for (const auto& network : result.networks) {
            ESP_LOGI("SCAN", "  %s (%d dBm)", 
                     network.ssid.c_str(), network.rssi);
        }
    } else {
        ESP_LOGE("SCAN", "Erro: %s", result.error.description().c_str());
    }
});

// Iniciar scan
ErrorCode err = wifi.startScanAsync();
```

---

## WifiClient

Classe para comunicação TCP/UDP sobre WiFi.

```cpp
class WifiClient {
public:
    ErrorCode connect(const std::string& host, uint16_t port);
    void disconnect();
    bool isConnected() const;
    
    ErrorCode send(const uint8_t* data, size_t length);
    int receive(uint8_t* buffer, size_t maxLength, uint32_t timeoutMs);
};
```

### Exemplo

```cpp
WifiClient client;

void sendData() {
    if (!wifi.isConnected()) {
        ESP_LOGE("APP", "WiFi não conectado");
        return;
    }
    
    ErrorCode err = client.connect("192.168.1.100", 8080);
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("APP", "Erro de conexão: %s", err.description().c_str());
        return;
    }
    
    const char* message = "Hello, Server!";
    client.send((uint8_t*)message, strlen(message));
    
    uint8_t buffer[256];
    int received = client.receive(buffer, sizeof(buffer), 5000);
    if (received > 0) {
        ESP_LOGI("APP", "Resposta: %.*s", received, buffer);
    }
    
    client.disconnect();
}
```

---

## IPAddress

Classe utilitária para manipulação de endereços IP.

```cpp
class IPAddress {
public:
    IPAddress();
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
    IPAddress(uint32_t addr);
    IPAddress(const std::string& str);
    
    bool isValid() const;
    std::string toString() const;
    uint32_t toUint32() const;
    
    uint8_t operator[](int index) const;
    bool operator==(const IPAddress& other) const;
    bool operator!=(const IPAddress& other) const;
};
```

### Exemplo

```cpp
IPAddress ip1(192, 168, 1, 100);
IPAddress ip2("192.168.1.100");
IPAddress ip3 = wifi.getIPAddress();

ESP_LOGI("IP", "Endereço: %s", ip3.toString().c_str());
ESP_LOGI("IP", "Octeto 0: %d", ip3[0]);  // 192

if (ip1 == ip2) {
    ESP_LOGI("IP", "IPs iguais");
}
```

---

## Exemplos

### Conexão Básica

```cpp
#include "WifiConnection.h"

WifiConnection wifi;

void connectToWifi() {
    wifi.init();
    
    // Conexão síncrona (blocking)
    ErrorCode err = wifi.connect("MinhaRede", "MinhaSenha", false);
    
    if (err == CommonErrorCodes::None) {
        ESP_LOGI("WIFI", "Conectado!");
        ESP_LOGI("WIFI", "IP: %s", wifi.getIPAddress().toString().c_str());
    } else {
        ESP_LOGE("WIFI", "Erro: %s", err.description().c_str());
    }
}
```

### Com Reconexão Automática

> **Nota**: Para reconexão automática, roaming e gerenciamento avançado, use `NetworkManager` do componente Connection.

```cpp
#include "NetworkManager.h"

void setupWifi() {
    auto& network = NetworkManager::instance();
    
    // Eventos
    network.onStateChanged.addHandler([](NetworkState oldState, NetworkState newState) {
        ESP_LOGI("WIFI", "Estado: %d -> %d", (int)oldState, (int)newState);
    });
    
    network.onNetworkAvailable.addHandler([](const NetworkInfo& info) {
        ESP_LOGI("WIFI", "Conectado a %s - IP: %s", 
                 info.ssid.c_str(), info.ipAddress.c_str());
    });
    
    // Inicializar (com task de background para reconexão automática)
    network.init(true);
    
    // Conectar (credenciais são salvas automaticamente)
    network.connect("MinhaRede", "MinhaSenha");
}
```

### Seleção de Melhor Rede

```cpp
void connectToBestNetwork(const std::vector<std::pair<std::string, std::string>>& credentials) {
    wifi.onScanCompleted.addHandler([&](WifiConnection* conn, const WiFiScanResult& result) {
        if (!result.success) return;
        
        // Encontrar melhor rede conhecida
        int bestRssi = -100;
        std::string bestSsid, bestPassword;
        
        for (const auto& network : result.networks) {
            for (const auto& [ssid, password] : credentials) {
                if (network.ssid == ssid && network.rssi > bestRssi) {
                    bestRssi = network.rssi;
                    bestSsid = ssid;
                    bestPassword = password;
                }
            }
        }
        
        if (!bestSsid.empty()) {
            ESP_LOGI("WIFI", "Conectando a %s (%d dBm)", bestSsid.c_str(), bestRssi);
            conn->connect(bestSsid, bestPassword, true);
        }
    });
    
    wifi.startScanAsync();
}
```

---

## OTA Updates

O componente inclui suporte a atualizações Over-The-Air. Veja [OTA_USAGE.md](./OTA_USAGE.md) para documentação detalhada.

### Uso Básico

```cpp
#include "OTAManager.h"

OTAManager& ota = OTAManager::instance();

// Verificar atualização
ota.checkForUpdate("https://server.com/firmware.bin");

// Eventos
ota.onUpdateAvailable.addHandler([](const OTAInfo& info) {
    ESP_LOGI("OTA", "Versão disponível: %s", info.version.c_str());
});

ota.onProgress.addHandler([](int progress) {
    ESP_LOGI("OTA", "Progresso: %d%%", progress);
});
```

---

## Dependências

```cmake
idf_component_register(
    SRCS
        "WifiConnection.cpp"
        "WifiClient.cpp"
        "IPAddress.cpp"
        "OTAManager.cpp"
        # ... outros arquivos
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_client
        esp_https_ota
        Utility
        ErrorCodes
        Connection
)
```

## Configuração

### sdkconfig.defaults

```ini
CONFIG_ESP_WIFI_SSID=""
CONFIG_ESP_WIFI_PASSWORD=""
CONFIG_ESP_MAXIMUM_RETRY=5
CONFIG_ESP_WIFI_AUTH_WPA2_PSK=y
CONFIG_ESP_WIFI_SCAN_RSSI_THRESHOLD=-127
```

## Compatibilidade

- ESP-IDF >= 5.0.0
- Plataformas: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
