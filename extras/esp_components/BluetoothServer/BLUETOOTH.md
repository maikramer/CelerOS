# BluetoothServer Component

Componente para gerenciamento de servidor Bluetooth Low Energy (BLE) usando NimBLE no ESP32.

## Índice

- [Visão Geral](#visão-geral)
- [BluetoothServer](#bluetoothserver)
- [BluetoothManager](#bluetoothmanager)
- [BluetoothConnection](#bluetoothconnection)
- [BluetoothUtility](#bluetoothutility)
- [Exemplos](#exemplos)

---

## Visão Geral

O componente BluetoothServer fornece uma abstração completa para criar e gerenciar um servidor BLE no ESP32. Principais características:

- **Arquitetura Singleton**: Classes principais como singleton para acesso global
- **Event-driven**: Eventos para conexão/desconexão e recebimento de dados
- **Serviços públicos e privados**: Separação entre dados públicos e privados
- **Gerenciamento de conexões**: Múltiplas conexões simultâneas
- **Integração com Commander**: Sistema de comandos para comunicação estruturada

### Arquitetura

```
┌─────────────────────────────────────────────────────────┐
│                    BluetoothManager                      │
│  (Singleton - Orquestra todo o subsistema BLE)          │
├─────────────────────────────────────────────────────────┤
│                    BluetoothServer                       │
│  (Singleton - Gerencia servidor NimBLE)                 │
├─────────────────────────────────────────────────────────┤
│              BluetoothConnection (N)                     │
│  (Representa cada conexão individual)                    │
├─────────────────────────────────────────────────────────┤
│              NimBLE (ESP-IDF)                            │
└─────────────────────────────────────────────────────────┘
```

---

## BluetoothServer

Singleton que gerencia o servidor NimBLE, criando serviços e características.

### API

```cpp
class BluetoothServer : public Singleton<BluetoothServer> {
public:
    NimBLEServer* BleServer;  // Acesso direto ao servidor NimBLE
    
    // Setup
    ErrorCode setup(const std::string& deviceName);
    
    // Criar características
    NimBLECharacteristic* createPrivateWriteCharacteristic();
    NimBLECharacteristic* createPrivateNotifyCharacteristic();
    NimBLECharacteristic* createWriteCharacteristic(NimBLEService* service = nullptr);
    NimBLECharacteristic* createNotifyCharacteristic(NimBLEService* service = nullptr);
    
    // Criar serviços
    NimBLEService* createPrivateService();
    
    // Getters
    std::string getPrivateServiceUUID() const;
};
```

### Exemplo de Uso

```cpp
#include "BluetoothServer.h"

void setupBluetooth() {
    auto& server = BluetoothServer::instance();
    
    // Inicializar servidor com nome do dispositivo
    ErrorCode result = server.setup("ESP32_Device");
    if (result != CommonErrorCodes::None) {
        ESP_LOGE("BLE", "Falha ao inicializar BLE: %s", result.description().c_str());
        return;
    }
    
    // Criar características no serviço privado
    auto* writeChar = server.createPrivateWriteCharacteristic();
    auto* notifyChar = server.createPrivateNotifyCharacteristic();
    
    ESP_LOGI("BLE", "Servidor BLE iniciado!");
}
```

---

## BluetoothManager

Singleton de alto nível que orquestra todo o subsistema BLE, gerenciando inicialização e conexões.

### API

```cpp
class BluetoothManager : public Singleton<BluetoothManager> {
public:
    // Inicialização
    ErrorCode initialize();
    
    // Gerenciamento de conexões
    BluetoothConnection* createConnection();
    
    // Getters
    std::string getPrivateServiceUUID() const;
    
    // Eventos
    static Event<BluetoothManager*, BluetoothConnection*> onConnection;
};
```

### Eventos

| Evento | Parâmetros | Descrição |
|--------|------------|-----------|
| `onConnection` | `BluetoothManager*`, `BluetoothConnection*` | Nova conexão BLE estabelecida |

### Exemplo de Uso

```cpp
#include "BluetoothManager.h"

void initBluetooth() {
    auto& manager = BluetoothManager::instance();
    
    // Registrar handler de conexão
    manager.onConnection.addHandler([](BluetoothManager* mgr, BluetoothConnection* conn) {
        ESP_LOGI("BLE", "Nova conexão BLE! ID: %d", conn->getId());
        
        // Configurar eventos da conexão
        conn->onDataReceived.addHandler([](const std::vector<uint8_t>& data) {
            ESP_LOGI("BLE", "Dados recebidos: %d bytes", data.size());
        });
    });
    
    // Inicializar
    ErrorCode result = manager.initialize();
    if (result != CommonErrorCodes::None) {
        ESP_LOGE("BLE", "Erro: %s", result.description().c_str());
    }
}
```

---

## BluetoothConnection

Representa uma conexão BLE individual. Gerencia transmissão de dados, notificações e (opcionalmente) usuário associado.

### API

```cpp
class BluetoothConnection : public BaseConnection, public NimBLECharacteristicCallbacks {
public:
    BluetoothConnection();
    ~BluetoothConnection();
    
    // Ciclo de vida
    ErrorCode initialize();
    void connect(uint16_t connId);
    void disconnect() override;
    
    // Estado
    bool isFree() const;
    uint16_t getId() const;
    
    // Transmissão de dados
    ErrorCode sendData(const std::vector<uint8_t>& data, bool isNotification);
    
    // UUIDs das características
    std::string getWriteUUID() const;
    std::string getNotifyUUID() const;
    std::string getConnectionInfoJson() const;
    
    // Callbacks NimBLE
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
    void onStatus(NimBLECharacteristic* pCharacteristic, int code) override;
    
#ifdef USER_MANAGEMENT_ENABLED
    void setUser(ConnectedUser* user);
    ConnectedUser* getUser(bool canBeNull = true, bool canBeEmpty = true);
    void logoff();
#endif
};
```

### Exemplo de Uso

```cpp
#include "BluetoothConnection.h"

void handleConnection(BluetoothConnection* conn) {
    // Obter informações da conexão
    ESP_LOGI("BLE", "Connection ID: %d", conn->getId());
    ESP_LOGI("BLE", "Write UUID: %s", conn->getWriteUUID().c_str());
    ESP_LOGI("BLE", "Notify UUID: %s", conn->getNotifyUUID().c_str());
    
    // Enviar dados
    std::vector<uint8_t> response = {0x01, 0x02, 0x03};
    ErrorCode result = conn->sendData(response, true);  // true = notification
    
    if (result != CommonErrorCodes::None) {
        ESP_LOGE("BLE", "Erro ao enviar: %s", result.description().c_str());
    }
}

// Enviar JSON
void sendJsonResponse(BluetoothConnection* conn, const std::string& json) {
    std::vector<uint8_t> data(json.begin(), json.end());
    conn->sendData(data, true);
}
```

---

## BluetoothUtility

Classe utilitária para operações BLE comuns.

### API

```cpp
class BluetoothUtility {
public:
    /**
     * @brief Gera UUID único para serviço ou característica
     * @param isCharacteristic true para característica, false para serviço
     * @return UUID único como string
     */
    static std::string generateUniqueId(bool isCharacteristic);
};
```

### Exemplo

```cpp
// Gerar UUID para novo serviço
std::string serviceUUID = BluetoothUtility::generateUniqueId(false);

// Gerar UUID para nova característica
std::string charUUID = BluetoothUtility::generateUniqueId(true);
```

---

## Exemplos

### Setup Completo

```cpp
#include "BluetoothManager.h"
#include "Commander.h"

class MyBluetoothApp {
public:
    void init() {
        auto& manager = BluetoothManager::instance();
        
        // Evento de conexão
        manager.onConnection.addHandler([this](BluetoothManager* mgr, BluetoothConnection* conn) {
            handleNewConnection(conn);
        });
        
        // Inicializar
        ErrorCode result = manager.initialize();
        if (result != CommonErrorCodes::None) {
            ESP_LOGE("APP", "Erro BLE: %s", result.description().c_str());
        }
    }
    
private:
    void handleNewConnection(BluetoothConnection* conn) {
        ESP_LOGI("APP", "Cliente conectado: %d", conn->getId());
        
        // Configurar commander para esta conexão
        auto& commander = Commander::instance();
        commander.setConnection(conn);
        
        // Enviar informações de conexão
        std::string info = conn->getConnectionInfoJson();
        std::vector<uint8_t> data(info.begin(), info.end());
        conn->sendData(data, true);
    }
};
```

### Comunicação Bidirecional

```cpp
class DataHandler {
public:
    void setup(BluetoothConnection* conn) {
        _connection = conn;
        
        // Handler para dados recebidos (via Commander)
        Commander::instance().registerCommand("getData", [this](const nlohmann::json& params) {
            return handleGetData(params);
        });
        
        Commander::instance().registerCommand("setConfig", [this](const nlohmann::json& params) {
            return handleSetConfig(params);
        });
    }
    
private:
    nlohmann::json handleGetData(const nlohmann::json& params) {
        nlohmann::json response;
        response["temperature"] = 25.5;
        response["humidity"] = 60;
        return response;
    }
    
    nlohmann::json handleSetConfig(const nlohmann::json& params) {
        // Processar configuração
        if (params.contains("interval")) {
            _updateInterval = params["interval"];
        }
        return {{"success", true}};
    }
    
    BluetoothConnection* _connection;
    int _updateInterval = 1000;
};
```

### Com Gerenciamento de Usuário

```cpp
#ifdef USER_MANAGEMENT_ENABLED

void handleLogin(BluetoothConnection* conn, const std::string& username, const std::string& password) {
    // Autenticar usuário
    auto* user = UserManager::authenticate(username, password);
    
    if (user) {
        conn->setUser(user);
        ESP_LOGI("APP", "Usuário %s logado na conexão %d", username.c_str(), conn->getId());
    } else {
        ESP_LOGW("APP", "Falha no login para %s", username.c_str());
    }
}

void handleLogout(BluetoothConnection* conn) {
    conn->logoff();
    ESP_LOGI("APP", "Usuário deslogado da conexão %d", conn->getId());
}

#endif
```

---

## Serviços e Características

### Estrutura de Serviços

| Serviço | Tipo | Descrição |
|---------|------|-----------|
| Public Service | Público | Informações gerais do dispositivo |
| Private Service | Privado | Dados e comandos específicos da aplicação |

### Tipos de Características

| Tipo | Permissões | Uso |
|------|------------|-----|
| Write | Write, Write without response | Receber comandos do cliente |
| Notify | Read, Notify | Enviar dados para o cliente |

### UUIDs

Os UUIDs são gerados automaticamente usando `BluetoothUtility::generateUniqueId()` para garantir unicidade.

---

## Dependências

```cmake
idf_component_register(
    SRCS
        "BluetoothServer.cpp"
        "BluetoothManager.cpp"
        "BluetoothConnection.cpp"
        "Commander.cpp"
        "ConnectionManager.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nimble
        Utility
        ErrorCodes
        Connection
        JsonModels
)
```

## Configuração do Projeto

Adicionar ao `sdkconfig.defaults`:

```ini
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
```

## Compatibilidade

- ESP-IDF >= 5.0.0
- NimBLE stack
- Plataformas: ESP32, ESP32-S3, ESP32-C3 (com BLE)
