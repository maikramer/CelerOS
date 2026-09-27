# ErrorCodes Component

Sistema padronizado de códigos de erro para ESP32/ESP-IDF. Fornece uma estrutura organizada para definir, gerenciar e reportar erros em toda a aplicação.

## Índice

- [Visão Geral](#visão-geral)
- [ErrorCode Class](#errorcode-class)
- [Categorias de Erros](#categorias-de-erros)
- [Uso Básico](#uso-básico)
- [Definindo Novos Erros](#definindo-novos-erros)
- [Referência de Erros](#referência-de-erros)

---

## Visão Geral

O sistema de ErrorCodes oferece:

- **Tipagem forte**: Erros são objetos, não apenas números
- **Categorização**: Erros agrupados por tipo (Network, WiFi, Storage, etc.)
- **Descrições**: Cada erro tem nome e descrição legível
- **Logging integrado**: Método `log()` para output consistente
- **Base de dados global**: Erros registrados centralmente

### Arquitetura

```
CommonErrorCodes.h (inclui todos)
    │
    ├── GeneralErrorCodes.h    (erros gerais)
    ├── NetworkErrorCodes.h    (rede)
    ├── WifiErrorCodes.h       (WiFi)
    ├── BluetoothErrorCodes.h  (Bluetooth)
    ├── FileErrorCodes.h       (arquivos)
    ├── StorageErrorCodes.h    (armazenamento)
    ├── UserErrorCodes.h       (autenticação)
    ├── HardwareErrorCodes.h   (hardware)
    └── CommunicationErrorCodes.h (comunicação)
```

---

## ErrorCode Class

### Tipos de Erro

```cpp
enum class ErrorCodeType {
    General,       // Erros gerais
    Network,       // Erros de rede
    WiFi,          // Erros específicos de WiFi
    Socket,        // Erros de socket
    File,          // Erros de sistema de arquivos
    Memory,        // Erros de alocação de memória
    Hardware,      // Erros de hardware
    User,          // Erros de autenticação/usuário
    Storage,       // Erros de armazenamento
    Project,       // Erros específicos do projeto
    Communication  // Erros de comunicação
};
```

### API

```cpp
class ErrorCode {
public:
    // Construtores
    ErrorCode();  // Cria erro inválido
    ErrorCode(std::string name, std::string description, ErrorCodeType type);
    
    // Getters
    const std::string& name() const;
    const std::string& description() const;
    ErrorCodeType type() const;
    bool isValid() const;
    
    // Operadores
    bool operator==(const ErrorCode& other) const;
    bool operator!=(const ErrorCode& other) const;
    friend std::ostream& operator<<(std::ostream& os, const ErrorCode& errorCode);
    
    // Logging
    void log(const char* tag, 
             esp_log_level_t level = ESP_LOG_ERROR,
             const std::string& additionalMessage = "") const;
    
    // Métodos estáticos
    static void initialize();  // Inicializa base de dados
    static ErrorCode define(const std::string& name, 
                           const std::string& description, 
                           ErrorCodeType type);
    static ErrorCode get(const std::string& name);
};
```

---

## Categorias de Erros

### GeneralErrorCodes

Erros comuns de propósito geral.

```cpp
namespace CommonErrorCodes {
    extern const ErrorCode Invalid;           // Código de erro inválido
    extern const ErrorCode None;              // Sem erro (sucesso)
    extern const ErrorCode UnknownError;      // Erro desconhecido
    extern const ErrorCode OperationFailed;   // Operação falhou
    extern const ErrorCode NotImplemented;    // Não implementado
    extern const ErrorCode NotInitialized;    // Componente não inicializado
    extern const ErrorCode ArgumentError;     // Argumento inválido
    extern const ErrorCode Timeout;           // Timeout
    extern const ErrorCode ConnectionClosed;  // Conexão fechada
    extern const ErrorCode ListIsEmpty;       // Lista vazia
}
```

### NetworkErrorCodes

Erros relacionados a operações de rede.

```cpp
namespace CommonErrorCodes {
    extern const ErrorCode NetworkError;
    extern const ErrorCode ConnectionFailed;
    extern const ErrorCode ConnectionTimeout;
    extern const ErrorCode HostNotFound;
    extern const ErrorCode NetworkUnavailable;
}
```

### WifiErrorCodes

Erros específicos de WiFi.

```cpp
namespace CommonErrorCodes {
    extern const ErrorCode WifiNotConnected;
    extern const ErrorCode WifiConnectionFailed;
    extern const ErrorCode WifiAuthenticationFailed;
    extern const ErrorCode WifiSSIDNotFound;
    extern const ErrorCode WifiPasswordIncorrect;
}
```

### StorageErrorCodes

Erros de armazenamento e NVS.

```cpp
namespace CommonErrorCodes {
    extern const ErrorCode StorageError;
    extern const ErrorCode StorageFull;
    extern const ErrorCode KeyNotFound;
    extern const ErrorCode StorageCorrupted;
}
```

### UserErrorCodes

Erros de autenticação e gerenciamento de usuários.

```cpp
namespace CommonErrorCodes {
    extern const ErrorCode UserNotFound;
    extern const ErrorCode InvalidCredentials;
    extern const ErrorCode UserAlreadyExists;
    extern const ErrorCode SessionExpired;
    extern const ErrorCode UnauthorizedAccess;
    extern const ErrorCode PasswordTooWeak;
}
```

### HardwareErrorCodes

Erros de hardware.

```cpp
namespace CommonErrorCodes {
    extern const ErrorCode HardwareError;
    extern const ErrorCode SensorError;
    extern const ErrorCode PeripheralNotFound;
    extern const ErrorCode CalibrationFailed;
}
```

---

## Uso Básico

### Incluir Headers

```cpp
// Incluir todos os códigos de erro
#include "CommonErrorCodes.h"

// Ou incluir apenas categorias específicas
#include "ErrorCode.h"
#include "NetworkErrorCodes.h"
#include "UserErrorCodes.h"
```

### Inicialização

```cpp
void app_main() {
    // Inicializar base de dados de erros (chamar uma vez)
    ErrorCode::initialize();
    
    // ... resto da aplicação
}
```

### Retornando Erros

```cpp
#include "CommonErrorCodes.h"

ErrorCode connectToServer(const std::string& host) {
    if (host.empty()) {
        return CommonErrorCodes::ArgumentError;
    }
    
    if (!wifi.isConnected()) {
        return CommonErrorCodes::WifiNotConnected;
    }
    
    if (!socket.connect(host)) {
        return CommonErrorCodes::ConnectionFailed;
    }
    
    return CommonErrorCodes::None;  // Sucesso
}
```

### Verificando Erros

```cpp
ErrorCode result = connectToServer("api.example.com");

if (result != CommonErrorCodes::None) {
    // Logar erro
    result.log("MAIN", ESP_LOG_ERROR, "Falha na conexão");
    
    // Tratar erro específico
    if (result == CommonErrorCodes::WifiNotConnected) {
        reconnectWifi();
    } else if (result == CommonErrorCodes::Timeout) {
        retry();
    }
}
```

### Logging de Erros

```cpp
ErrorCode error = CommonErrorCodes::NetworkError;

// Log simples
error.log("NETWORK");
// Output: E NETWORK: NetworkError - Erro de rede

// Log com mensagem adicional
error.log("NETWORK", ESP_LOG_ERROR, "Host: api.example.com");
// Output: E NETWORK: NetworkError - Erro de rede (Host: api.example.com)

// Log como warning
error.log("NETWORK", ESP_LOG_WARN);
// Output: W NETWORK: NetworkError - Erro de rede
```

---

## Definindo Novos Erros

### Criar Arquivo de Header

```cpp
// ProjectErrorCodes.h
#ifndef PROJECT_ERROR_CODES_H
#define PROJECT_ERROR_CODES_H

#include "ErrorCode.h"

namespace ProjectErrors {
    extern const ErrorCode SensorCalibrationFailed;
    extern const ErrorCode MotorOverheat;
    extern const ErrorCode BatteryLow;
    extern const ErrorCode ConfigurationInvalid;
}

#endif
```

### Criar Arquivo de Implementação

```cpp
// ProjectErrorCodes.cpp
#include "ProjectErrorCodes.h"

namespace ProjectErrors {
    const ErrorCode SensorCalibrationFailed = ErrorCode::define(
        "SensorCalibrationFailed",
        "Falha na calibração do sensor",
        ErrorCodeType::Hardware
    );
    
    const ErrorCode MotorOverheat = ErrorCode::define(
        "MotorOverheat",
        "Motor superaquecido - resfriamento necessário",
        ErrorCodeType::Hardware
    );
    
    const ErrorCode BatteryLow = ErrorCode::define(
        "BatteryLow",
        "Nível de bateria crítico",
        ErrorCodeType::Hardware
    );
    
    const ErrorCode ConfigurationInvalid = ErrorCode::define(
        "ConfigurationInvalid",
        "Configuração do sistema inválida",
        ErrorCodeType::Project
    );
}
```

### Usar Novos Erros

```cpp
#include "ProjectErrorCodes.h"

ErrorCode checkBattery() {
    uint32_t voltage = battery.GetVoltage();
    
    if (voltage < 3300) {
        return ProjectErrors::BatteryLow;
    }
    
    return CommonErrorCodes::None;
}
```

---

## Padrões de Uso

### Funções com Retorno de Erro

```cpp
struct Result {
    ErrorCode error;
    std::optional<std::string> data;
};

Result fetchData(const std::string& endpoint) {
    Result result;
    
    HttpResponse response = http.get(endpoint);
    
    if (!response.success) {
        result.error = CommonErrorCodes::NetworkError;
        return result;
    }
    
    if (response.statusCode == 404) {
        result.error = CommonErrorCodes::NotFound;
        return result;
    }
    
    result.error = CommonErrorCodes::None;
    result.data = response.body;
    return result;
}
```

### Classe com Estado de Erro

```cpp
class SensorManager {
public:
    ErrorCode getLastError() const { return _lastError; }
    
    bool readTemperature(float& temperature) {
        if (!_initialized) {
            _lastError = CommonErrorCodes::NotInitialized;
            return false;
        }
        
        if (!sensor.read(&temperature)) {
            _lastError = CommonErrorCodes::SensorError;
            return false;
        }
        
        _lastError = CommonErrorCodes::None;
        return true;
    }
    
private:
    ErrorCode _lastError = CommonErrorCodes::None;
    bool _initialized = false;
};
```

### Propagação de Erros

```cpp
ErrorCode processUserRequest(const std::string& userId, const std::string& action) {
    // Verificar autenticação
    ErrorCode authResult = authenticateUser(userId);
    if (authResult != CommonErrorCodes::None) {
        return authResult;  // Propagar erro
    }
    
    // Verificar permissões
    ErrorCode permResult = checkPermissions(userId, action);
    if (permResult != CommonErrorCodes::None) {
        return permResult;  // Propagar erro
    }
    
    // Executar ação
    return executeAction(action);
}
```

---

## Referência Rápida

### Verificações Comuns

```cpp
// Sucesso
if (error == CommonErrorCodes::None) { /* OK */ }

// Qualquer erro
if (error != CommonErrorCodes::None) { /* Erro */ }

// Erro válido (não é Invalid)
if (error.isValid()) { /* Erro definido */ }

// Erro por tipo
if (error.type() == ErrorCodeType::Network) { /* Erro de rede */ }
```

### Obter Erro por Nome

```cpp
// Útil para deserialização
ErrorCode error = ErrorCode::get("NetworkError");
if (error.isValid()) {
    // Erro encontrado
}
```

---

## Dependências

```cmake
idf_component_register(
    SRCS
        "ErrorCode.cpp"
        "GeneralErrorCodes.cpp"
        "NetworkErrorCodes.cpp"
        "WifiErrorCodes.cpp"
        "BluetoothErrorCodes.cpp"
        "FileErrorCodes.cpp"
        "StorageErrorCodes.cpp"
        "UserErrorCodes.cpp"
        "HardwareErrorCodes.cpp"
        "CommunicationErrorCodes.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_log
)
```

## Compatibilidade

- ESP-IDF >= 5.0.0
- C++17 ou superior
- Plataformas: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
