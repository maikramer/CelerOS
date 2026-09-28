# JsonModels Component

Componente com modelos de dados JSON para serialização e deserialização usando nlohmann/json.

## Índice

- [Visão Geral](#visão-geral)
- [BaseJsonData](#basejsondata)
- [Modelos de Erro](#modelos-de-erro)
- [Modelos de Lista](#modelos-de-lista)
- [Modelos de Usuário](#modelos-de-usuário)
- [Exemplos](#exemplos)

---

## Visão Geral

O componente JsonModels fornece classes base e modelos específicos para trabalhar com JSON de forma estruturada:

- **Classes base abstratas**: Para criar novos modelos facilmente
- **Tratamento de erros**: Modelos que incluem códigos de erro
- **Suporte a listas**: Paginação e iteração de dados
- **Modelos de usuário**: Autenticação e gerenciamento (quando habilitado)

### Dependência

Usa a biblioteca [nlohmann/json](https://github.com/nlohmann/json) para parsing e serialização.

---

## BaseJsonData

Classe base abstrata para todos os modelos JSON.

### API

```cpp
namespace JsonModels {

class BaseJsonData {
public:
    /**
     * @brief Converte objeto para string JSON
     */
    virtual std::string toJson() const = 0;
    
    /**
     * @brief Popula objeto a partir de string JSON
     * @return true se parsing foi bem sucedido
     */
    virtual bool fromString(const std::string& jsonStr);
    
    /**
     * @brief Popula objeto a partir de objeto JSON
     * @return true se população foi bem sucedida
     */
    virtual bool fromJson(const nlohmann::json& j) = 0;
};

// Operadores de stream
std::ostream& operator<<(std::ostream& Str, const BaseJsonData& v);
std::istream& operator>>(std::istream& Str, BaseJsonData& v);

}
```

### Criando Novos Modelos

```cpp
#include "JsonModels.h"

class SensorData : public JsonModels::BaseJsonData {
public:
    float temperature = 0.0f;
    float humidity = 0.0f;
    int64_t timestamp = 0;
    
    std::string toJson() const override {
        nlohmann::json j;
        j["temperature"] = temperature;
        j["humidity"] = humidity;
        j["timestamp"] = timestamp;
        return j.dump();
    }
    
    bool fromJson(const nlohmann::json& j) override {
        try {
            if (j.contains("temperature")) temperature = j["temperature"];
            if (j.contains("humidity")) humidity = j["humidity"];
            if (j.contains("timestamp")) timestamp = j["timestamp"];
            return true;
        } catch (...) {
            return false;
        }
    }
};
```

---

## Modelos de Erro

### BaseJsonDataError

Modelo base que inclui código de erro na serialização.

```cpp
class BaseJsonDataError : public BaseJsonData {
public:
    ErrorCode ErrorMessage = CommonErrorCodes::None;
    
    std::string toJson() const override;
    
protected:
    nlohmann::json getPartialJson(bool force) const;
};
```

### Exemplo de Uso

```cpp
class ApiResponse : public JsonModels::BaseJsonDataError {
public:
    std::string data;
    
    std::string toJson() const override {
        nlohmann::json j = getPartialJson(false);  // Inclui erro se houver
        j["data"] = data;
        return j.dump();
    }
    
    bool fromJson(const nlohmann::json& j) override {
        if (j.contains("data")) data = j["data"];
        return true;
    }
};

// Uso
ApiResponse response;
response.data = "Hello";
response.ErrorMessage = CommonErrorCodes::None;

std::string json = response.toJson();
// {"data": "Hello"}

response.ErrorMessage = CommonErrorCodes::NetworkError;
json = response.toJson();
// {"data": "Hello", "error": {"name": "NetworkError", "description": "..."}}
```

### UpdateDataJson

Modelo para operações de atualização com flag de update.

```cpp
class UpdateDataJson : public BaseJsonDataError {
protected:
    bool IsUpdate = true;
    nlohmann::json getPartialUpdateJson(bool force) const;
};
```

---

## Modelos de Lista

### BaseListJsonDataBasic

Base para modelos de lista com paginação.

```cpp
class BaseListJsonDataBasic : public BaseJsonDataError {
public:
    bool End = false;    // Fim da lista
    bool Begin = false;  // Início da lista
    int Index = 0;       // Índice atual
    
protected:
    nlohmann::json getPartialListJson() const;
};
```

### BaseListJsonData

Template para listas tipadas com chave/valor.

```cpp
template<typename TKey, typename TValue>
class BaseListJsonData : public BaseListJsonDataBasic {
public:
    virtual void fromPair(TKey first, TValue second) = 0;
};
```

### Exemplo de Lista

```cpp
class DeviceListItem : public JsonModels::BaseListJsonData<std::string, DeviceInfo> {
public:
    std::string deviceId;
    DeviceInfo deviceInfo;
    
    std::string toJson() const override {
        nlohmann::json j = getPartialListJson();
        j["deviceId"] = deviceId;
        j["device"] = {
            {"name", deviceInfo.name},
            {"type", deviceInfo.type}
        };
        return j.dump();
    }
    
    void fromPair(std::string id, DeviceInfo info) override {
        deviceId = id;
        deviceInfo = info;
    }
    
    bool fromJson(const nlohmann::json& j) override {
        // Implementar se necessário
        return true;
    }
};

// Uso para enviar lista paginada
void sendDeviceList(BluetoothConnection* conn, const std::map<std::string, DeviceInfo>& devices) {
    int index = 0;
    int total = devices.size();
    
    for (const auto& [id, info] : devices) {
        DeviceListItem item;
        item.fromPair(id, info);
        item.Index = index;
        item.Begin = (index == 0);
        item.End = (index == total - 1);
        
        std::string json = item.toJson();
        conn->sendData(std::vector<uint8_t>(json.begin(), json.end()), true);
        
        index++;
    }
}
```

---

## Modelos de Usuário

> **Nota**: Disponíveis apenas quando `USER_MANAGEMENT_ENABLED` está definido.

### User

Modelo para dados de usuário.

```cpp
#ifdef USER_MANAGEMENT_ENABLED

class User : public BaseJsonData {
public:
    std::string Name = "";
    std::string Password = "";
    std::string Email = "";
    bool IsConfirmed = false;
    bool IsAdmin = false;
    
    std::string toJson() const override;
    bool fromString(const std::string& jsonStr) override;
    bool fromJson(const nlohmann::json& j) override;
    
    bool isValid() const;
    std::string toString() const;
    nlohmann::json toPureJson() const;  // Sem info de erro
};

#endif
```

### Exemplo User

```cpp
#ifdef USER_MANAGEMENT_ENABLED

JsonModels::User user;
user.Name = "john_doe";
user.Email = "john@example.com";
user.IsAdmin = false;

// Serializar
std::string json = user.toJson();
// {"name": "john_doe", "email": "john@example.com", "isAdmin": false, ...}

// Deserializar
JsonModels::User parsed;
if (parsed.fromString(json)) {
    ESP_LOGI("APP", "Usuário: %s", parsed.Name.c_str());
}

#endif
```

### UserListJsonData

Modelo para lista de usuários.

```cpp
#ifdef USER_MANAGEMENT_ENABLED

class UserListJsonData : public BaseListJsonData<std::string, User> {
public:
    std::string UserName;
    User UserJson;
    
    std::string toJson() const override;
    void fromPair(std::string userName, User userJson) override;
};

#endif
```

### LoginTryResultJson

Resultado de tentativa de login.

```cpp
#ifdef USER_MANAGEMENT_ENABLED

class LoginTryResultJson : public BaseJsonDataError {
public:
    bool IsAdmin = false;
    
    std::string toJson() const override;
    bool fromJson(const nlohmann::json& j) override;
};

class SignUpResultJson : public LoginTryResultJson {};

#endif
```

### Exemplo Login/Signup

```cpp
#ifdef USER_MANAGEMENT_ENABLED

void handleLoginResult(bool success, bool isAdmin) {
    JsonModels::LoginTryResultJson result;
    
    if (success) {
        result.IsAdmin = isAdmin;
        result.ErrorMessage = CommonErrorCodes::None;
    } else {
        result.ErrorMessage = CommonErrorCodes::InvalidCredentials;
    }
    
    std::string response = result.toJson();
    // Enviar resposta...
}

#endif
```

---

## UuidInfoJsonData

Modelo para informações de UUID BLE.

```cpp
class UuidInfoJsonData : public BaseJsonData {
public:
    std::string NotifyUUID = "";
    std::string ServiceUUID = "";
    std::string WriteUUID = "";
    
    std::string toJson() const override;
    bool fromJson(const nlohmann::json& j) override;
};
```

### Exemplo

```cpp
JsonModels::UuidInfoJsonData uuidInfo;
uuidInfo.ServiceUUID = "12345678-1234-1234-1234-123456789abc";
uuidInfo.WriteUUID = "12345678-1234-1234-1234-123456789abd";
uuidInfo.NotifyUUID = "12345678-1234-1234-1234-123456789abe";

std::string json = uuidInfo.toJson();
// {"serviceUUID": "...", "writeUUID": "...", "notifyUUID": "..."}
```

---

## Exemplos

### Modelo Completo Customizado

```cpp
#include "JsonModels.h"

namespace MyApp {

class ConfigData : public JsonModels::BaseJsonDataError {
public:
    std::string deviceName;
    int updateInterval = 1000;
    bool autoConnect = true;
    std::vector<std::string> endpoints;
    
    std::string toJson() const override {
        nlohmann::json j = getPartialJson(false);
        j["deviceName"] = deviceName;
        j["updateInterval"] = updateInterval;
        j["autoConnect"] = autoConnect;
        j["endpoints"] = endpoints;
        return j.dump();
    }
    
    bool fromJson(const nlohmann::json& j) override {
        try {
            if (j.contains("deviceName")) deviceName = j["deviceName"];
            if (j.contains("updateInterval")) updateInterval = j["updateInterval"];
            if (j.contains("autoConnect")) autoConnect = j["autoConnect"];
            if (j.contains("endpoints")) {
                endpoints.clear();
                for (const auto& ep : j["endpoints"]) {
                    endpoints.push_back(ep);
                }
            }
            return true;
        } catch (const std::exception& e) {
            ESP_LOGE("CONFIG", "Parse error: %s", e.what());
            return false;
        }
    }
    
    bool validate() const {
        if (deviceName.empty()) return false;
        if (updateInterval < 100) return false;
        return true;
    }
};

}

// Uso
void processConfig(const std::string& jsonStr) {
    MyApp::ConfigData config;
    
    if (!config.fromString(jsonStr)) {
        ESP_LOGE("APP", "Erro ao parsear configuração");
        return;
    }
    
    if (!config.validate()) {
        ESP_LOGE("APP", "Configuração inválida");
        return;
    }
    
    ESP_LOGI("APP", "Device: %s, Interval: %d", 
             config.deviceName.c_str(), config.updateInterval);
}
```

### Resposta de API com Erro

```cpp
class ApiResult : public JsonModels::BaseJsonDataError {
public:
    bool success = false;
    std::string message;
    nlohmann::json data;
    
    std::string toJson() const override {
        nlohmann::json j = getPartialJson(true);  // Sempre incluir erro
        j["success"] = success;
        j["message"] = message;
        if (!data.is_null()) {
            j["data"] = data;
        }
        return j.dump();
    }
    
    bool fromJson(const nlohmann::json& j) override {
        if (j.contains("success")) success = j["success"];
        if (j.contains("message")) message = j["message"];
        if (j.contains("data")) data = j["data"];
        return true;
    }
    
    static ApiResult Success(const std::string& msg, const nlohmann::json& responseData = nullptr) {
        ApiResult result;
        result.success = true;
        result.message = msg;
        result.data = responseData;
        result.ErrorMessage = CommonErrorCodes::None;
        return result;
    }
    
    static ApiResult Error(ErrorCode error, const std::string& msg = "") {
        ApiResult result;
        result.success = false;
        result.message = msg.empty() ? error.description() : msg;
        result.ErrorMessage = error;
        return result;
    }
};

// Uso
std::string handleRequest(const std::string& action) {
    if (action == "getData") {
        nlohmann::json data = {{"value", 42}};
        return ApiResult::Success("Dados obtidos", data).toJson();
    }
    
    return ApiResult::Error(CommonErrorCodes::InvalidCommand, "Ação desconhecida").toJson();
}
```

---

## Dependências

```cmake
idf_component_register(
    SRCS
        "JsonModels.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        json
        ErrorCodes
)
```

## Compatibilidade

- ESP-IDF >= 5.0.0
- nlohmann/json >= 3.0
- C++17 ou superior
