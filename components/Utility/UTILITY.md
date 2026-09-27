# Utility Component

Componente utilitário com classes auxiliares e padrões de design para desenvolvimento em ESP32/ESP-IDF.

> **Nota**: Para containers thread-safe (SafeList, SafeMap, SafeQueue, SafeVector), veja o componente [SafeContainers](../SafeContainers/SAFE_CONTAINERS.md).

## Índice

- [Event](#event)
- [Timeout](#timeout)
- [Singleton](#singleton)
- [Utility](#utility-class)
- [CrossPlatformUtility](#crossplatformutility)

---

## Event

Sistema de eventos thread-safe para arquitetura event-driven. Permite registrar callbacks que são chamados quando o evento é disparado.

### Características

- **Thread-safe**: Usa mutex FreeRTOS para proteção
- **Variadic templates**: Suporta qualquer número e tipo de argumentos
- **Cross-platform**: Compatível com STM32 e ESP32

### API

```cpp
template <typename... Args>
class Event {
public:
    Event();
    ~Event();
    
    void addHandler(std::function<void(Args...)> handler);
    void removeHandler(std::function<void(Args...)> handler);
    void trigger(Args... args);
};
```

### Exemplo de Uso

```cpp
#include "Event.h"

// Evento sem argumentos
Event<> onButtonPressed;

// Evento com argumentos
Event<int, const std::string&> onDataReceived;

// Registrar handlers
onButtonPressed.addHandler([]() {
    ESP_LOGI("APP", "Botão pressionado!");
});

onDataReceived.addHandler([](int code, const std::string& message) {
    ESP_LOGI("APP", "Dados recebidos: %d - %s", code, message.c_str());
});

// Disparar eventos
onButtonPressed.trigger();
onDataReceived.trigger(200, "OK");
```

### Uso em Classes

```cpp
class SensorManager {
public:
    Event<float> onTemperatureChanged;
    Event<float, float> onHumidityAndTemp;
    Event<> onSensorError;
    
    void readSensor() {
        float temp = readTemperature();
        float humidity = readHumidity();
        
        if (temp != _lastTemp) {
            _lastTemp = temp;
            onTemperatureChanged.trigger(temp);
        }
        
        onHumidityAndTemp.trigger(humidity, temp);
    }
    
private:
    float _lastTemp = 0;
};

// Uso
SensorManager sensor;
sensor.onTemperatureChanged.addHandler([](float temp) {
    ESP_LOGI("APP", "Temperatura: %.2f°C", temp);
});
```

---

## Timeout

Gerenciador de timeout baseado em ticks do FreeRTOS.

### API

```cpp
class Timeout {
public:
    Timeout();
    Timeout(uint32_t timeout, bool start);
    
    void SetTimeout(uint32_t timeout);  // em milissegundos
    void Start();
    void Stop();
    
    bool TrueUntilTimeout();  // true enquanto não expirou
    bool TimeoutOccurred();   // true se expirou
};
```

### Exemplo de Uso

```cpp
#include "Timeout.h"

// Timeout de 5 segundos, iniciado automaticamente
Timeout connectionTimeout(5000, true);

while (TrueUntilTimeout()) {
    if (tryConnect()) {
        connectionTimeout.Stop();
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

if (connectionTimeout.TimeoutOccurred()) {
    ESP_LOGE("APP", "Timeout de conexão!");
}

// Reutilizar timeout
connectionTimeout.SetTimeout(10000);
connectionTimeout.Start();
```

### Uso em Loops de Retry

```cpp
Timeout retry(3000, true);
int attempts = 0;

while (!retry.TimeoutOccurred() && attempts < 5) {
    if (sendData()) {
        ESP_LOGI("APP", "Dados enviados!");
        break;
    }
    attempts++;
    vTaskDelay(pdMS_TO_TICKS(500));
}
```

---

## Singleton

Template para implementar o padrão Singleton de forma thread-safe.

### API

```cpp
template<typename T>
class Singleton {
public:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton) = delete;
    
    static T& instance();
    
protected:
    struct token {};
    Singleton() = default;
};
```

### Exemplo de Uso

```cpp
#include "Singleton.h"

class ConfigManager : public Singleton<ConfigManager> {
public:
    // Construtor deve usar token
    ConfigManager(token) {
        // Inicialização
    }
    
    std::string getWifiSSID() const { return _ssid; }
    void setWifiSSID(const std::string& ssid) { _ssid = ssid; }
    
private:
    std::string _ssid;
};

// Uso
ConfigManager::instance().setWifiSSID("MyNetwork");
std::string ssid = ConfigManager::instance().getWifiSSID();
```

---

## Utility Class

Funções utilitárias estáticas para operações comuns.

### API

```cpp
class Utility {
public:
    // Strings
    static std::vector<std::string> split(const std::string& source, char delimiter);
    static std::string trim(const std::string& str);
    static std::string CamelCaseToTitleCase(const std::string& toConvert);
    
    // Conversão
    template<typename T>
    static T GetConvertedFromString(const std::string& str);
    static uint32_t StringToByteArray(const std::string& input, uint8_t* output);
    static std::list<uint8_t> StringToByteList(const std::string& input);
    
    // GPIO
    static void SetInput(gpio_num_t gpioNum, gpio_pullup_t pullUp, 
                         gpio_int_type_t intType = GPIO_INTR_DISABLE);
    static void SetOutput(gpio_num_t gpioNum, bool openDrain, uint32_t initial_level = 0);
    static uint32_t ReadOutput(gpio_num_t gpio);
    
    // Tasks
    static TaskHandle_t CreateAndProfile(const char* taskName, TaskFunction_t function,
                                         uint32_t stack, UBaseType_t priority,
                                         int core, void* parameter);
    
    // Debug
    static void ListJsonKeys(const nlohmann::json& j);
};
```

### Exemplos

```cpp
#include "Utility.h"

// Split string
std::string data = "valor1,valor2,valor3";
auto parts = Utility::split(data, ',');
// parts = ["valor1", "valor2", "valor3"]

// Trim whitespace
std::string text = "  hello world  ";
auto trimmed = Utility::trim(text);
// trimmed = "hello world"

// Conversão de tipo
std::string numStr = "42";
int num = Utility::GetConvertedFromString<int>(numStr);

// Configurar GPIO como entrada com pull-up
Utility::SetInput(GPIO_NUM_4, GPIO_PULLUP_ENABLE);

// Configurar GPIO como saída
Utility::SetOutput(GPIO_NUM_5, false, 0);

// Criar task com profiling
Utility::CreateAndProfile("MyTask", myTaskFunction, 4096, 5, 0, nullptr);
```

---

## CrossPlatformUtility

Funções de logging cross-platform (ESP32 e STM32).

### API

```cpp
void log_device(bool isError, const char* origin, const char* format, ...);
```

### Exemplo

```cpp
#include "CrossPlatformUtility.h"

// Log de erro
log_device(true, "MyModule", "Erro ao conectar: %d", errorCode);

// Log informativo
log_device(false, "MyModule", "Conexão estabelecida");
```

---

## Dependências

```cmake
idf_component_register(
    SRCS
        "Utility.cpp"
        "CrossPlatformUtility.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        nlohmann-json
        esp_driver_gpio
)
```

## Compatibilidade

- ESP-IDF >= 5.0.0
- FreeRTOS
- C++17 ou superior
- Plataformas: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
