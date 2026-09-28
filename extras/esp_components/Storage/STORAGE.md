# Storage - Armazenamento Persistente

Sistema de armazenamento híbrido para ESP32 com suporte a sistema de arquivos (SD Card) e NVS (Non-Volatile Storage).

## Índice

- [Características](#características)
- [Arquitetura](#arquitetura)
- [Inicialização](#inicialização)
- [Operações de Key-Value](#operações-de-key-value)
- [NVS Direct](#nvs-direct)
- [Arquivos](#arquivos)
- [Configuração](#configuração)
- [Exemplos](#exemplos)

---

## Características

- **Híbrido**: File System + NVS fallback
- **Key-Value**: Armazenamento simples de pares chave-valor
- **Templates**: Suporte a tipos genéricos
- **Configuração**: Namespace dedicado para configs
- **Thread-Safe**: Operações seguras para FreeRTOS

---

## Arquitetura

```
┌─────────────────────────────────────────┐
│              Storage API                │
├─────────────────────────────────────────┤
│  storeKeyValue() | readKeyValue()       │
│  storeConfig()   | loadConfig()         │
└──────────────────┬──────────────────────┘
                   │
       ┌───────────┴───────────┐
       │                       │
       ▼                       ▼
┌─────────────┐         ┌─────────────┐
│ File System │         │     NVS     │
│  (SD Card)  │         │  (Flash)    │
├─────────────┤         ├─────────────┤
│ - Arquivos  │         │ - Key-Value │
│ - Ilimitado │         │ - Limitado  │
│ - Lento     │         │ - Rápido    │
└─────────────┘         └─────────────┘
```

---

## Inicialização

```cpp
#include "Storage.h"

void setup() {
    // Inicializar Storage (tenta File System, fallback para NVS)
    ErrorCode err = Storage::initialize();
    
    if (err == CommonErrorCodes::None) {
        if (Storage::isFileSystemAvailable()) {
            ESP_LOGI("Storage", "Usando File System (SD Card)");
        } else {
            ESP_LOGI("Storage", "Usando NVS (fallback)");
        }
    } else {
        ESP_LOGE("Storage", "Falha na inicialização: %s", err.description().c_str());
    }
}
```

---

## Operações de Key-Value

### Armazenar Valor

```cpp
// String
Storage::storeKeyValue("wifi_ssid", "MinhaRede", "config");

// Números
Storage::storeKeyValue("volume", 75, "settings");
Storage::storeKeyValue("threshold", 3.14f, "calibration");

// Com controle de sobrescrita
Storage::storeKeyValue("device_id", "ABC123", "system", false);  // Não sobrescreve se existir
```

### Ler Valor

```cpp
// String
std::string ssid;
ErrorCode err = Storage::readKeyValue("wifi_ssid", ssid, "config");

if (err == CommonErrorCodes::None) {
    ESP_LOGI("Storage", "SSID: %s", ssid.c_str());
} else if (err == CommonErrorCodes::FileNotFound) {
    ESP_LOGW("Storage", "Chave não encontrada");
}

// Números
int volume;
Storage::readKeyValue("volume", volume, "settings");

float threshold;
Storage::readKeyValue("threshold", threshold, "calibration");
```

### Ler ou Criar

```cpp
// Se não existir, cria com valor padrão
int volume = 50;  // Valor padrão
Storage::readOrCreateKeyValue("volume", volume, "settings", "volume");

// volume agora tem o valor lido ou 50 se não existia
```

### Obter Todas as Entradas

```cpp
std::map<std::string, std::string> configs;
ErrorCode err = Storage::getEntriesFromFile("config", configs);

if (err == CommonErrorCodes::None) {
    for (const auto& pair : configs) {
        ESP_LOGI("Storage", "%s = %s", pair.first.c_str(), pair.second.c_str());
    }
}
```

---

## NVS Direct

Para acesso direto ao NVS (mais rápido, mas com limitações):

```cpp
#include "NVS.h"

// Strings
NVS::storeValue("config", "wifi_ssid", "MinhaRede", true);

std::string ssid;
NVS::readValue("config", "wifi_ssid", ssid);

// Inteiros
NVS::storeValue("config", "boot_count", 42);

int32_t count;
NVS::readValue("config", "boot_count", count);

// Blob (dados binários)
uint8_t data[32] = {...};
NVS::storeBlob("config", "calibration", data, sizeof(data));

uint8_t readData[32];
size_t len = sizeof(readData);
NVS::readBlob("config", "calibration", readData, len);

// Apagar
NVS::eraseData("config", "wifi_ssid");
NVS::eraseNamespace("config");  // Apaga tudo do namespace
```

### Namespaces NVS

```cpp
// Listar chaves em um namespace
std::vector<std::string> keys;
NVS::getKeys("config", keys);

for (const auto& key : keys) {
    ESP_LOGI("NVS", "Key: %s", key.c_str());
}
```

---

## Arquivos

Operações de arquivo quando File System está disponível:

### Deletar Arquivo

```cpp
ErrorCode err = Storage::deleteFile("old_logs");

if (err == CommonErrorCodes::None) {
    ESP_LOGI("Storage", "Arquivo deletado");
}
```

### Copiar Arquivo

```cpp
ErrorCode err = Storage::copyFile("config", "config_backup");
```

### Status do Armazenamento

```cpp
StorageStatus status;
ErrorCode err = Storage::getStatus(status);

if (err == CommonErrorCodes::None) {
    ESP_LOGI("Storage", "Espaço livre: %llu bytes", status.freeSpace);
    ESP_LOGI("Storage", "Espaço total: %llu bytes", status.totalSpace);
}
```

### Apagar Tudo

```cpp
// CUIDADO: Apaga todos os dados!
ErrorCode err = Storage::eraseData();
```

---

## Configuração

API simplificada para configurações de aplicação:

```cpp
// Salvar configuração
Storage::storeConfig("api_url", "https://api.example.com");
Storage::storeConfig("api_key", "secret123");
Storage::storeConfig("interval", "60");

// Carregar configuração
std::string apiUrl;
Storage::loadConfig("api_url", apiUrl);

std::string interval;
if (Storage::loadConfig("interval", interval) == CommonErrorCodes::None) {
    int intervalSec = std::stoi(interval);
}
```

---

## Constantes

```cpp
namespace StorageConstants {
    constexpr const char* BasePath = "/storage";     // Caminho base no FS
    constexpr const char* UsersFilename = "users";   // Arquivo de usuários
    constexpr const char* ConfigFilename = "config"; // Arquivo de config
    constexpr const char* InfoFilename = "info";     // Arquivo de info
}
```

---

## Códigos de Erro Comuns

| Código | Descrição |
|--------|-----------|
| `None` | Sucesso |
| `FileNotFound` | Arquivo/chave não encontrado |
| `FileOpenError` | Erro ao abrir arquivo |
| `FileIsEmpty` | Arquivo vazio |
| `FileExists` | Arquivo já existe (quando overwrite=false) |
| `StorageReadError` | Erro de leitura |
| `StorageWriteError` | Erro de escrita |
| `ArgumentError` | Argumento inválido |

---

## Exemplos

### Sistema de Configuração

```cpp
class ConfigManager {
public:
    static void loadDefaults() {
        // Carregar ou criar configurações padrão
        std::string val;
        
        if (Storage::loadConfig("initialized", val) != CommonErrorCodes::None) {
            // Primeira execução - criar configurações padrão
            Storage::storeConfig("wifi_ssid", "");
            Storage::storeConfig("wifi_pass", "");
            Storage::storeConfig("api_url", "https://api.example.com");
            Storage::storeConfig("interval", "60");
            Storage::storeConfig("initialized", "1");
        }
    }
    
    static std::string get(const std::string& key) {
        std::string value;
        Storage::loadConfig(key, value);
        return value;
    }
    
    static void set(const std::string& key, const std::string& value) {
        Storage::storeConfig(key, value, true);
    }
};
```

### Contador de Boot

```cpp
void incrementBootCount() {
    int32_t count = 0;
    
    // Ler contador atual
    if (NVS::readValue("system", "boot_count", count) != CommonErrorCodes::None) {
        count = 0;
    }
    
    // Incrementar e salvar
    count++;
    NVS::storeValue("system", "boot_count", count);
    
    ESP_LOGI("Boot", "Boot #%d", count);
}
```

### Cache de Dados

```cpp
class DataCache {
public:
    static bool save(const std::string& key, const std::string& json) {
        return Storage::storeKeyValue(key, json, "cache") == CommonErrorCodes::None;
    }
    
    static std::string load(const std::string& key) {
        std::string json;
        if (Storage::readKeyValue(key, json, "cache") == CommonErrorCodes::None) {
            return json;
        }
        return "";
    }
    
    static void clear() {
        Storage::deleteFile("cache");
    }
};
```

### Calibração de Sensor

```cpp
struct CalibrationData {
    float offset;
    float scale;
    uint32_t timestamp;
};

void saveCalibration(const CalibrationData& cal) {
    NVS::storeBlob("sensor", "calibration", &cal, sizeof(cal));
}

bool loadCalibration(CalibrationData& cal) {
    size_t len = sizeof(cal);
    return NVS::readBlob("sensor", "calibration", &cal, len) == CommonErrorCodes::None;
}
```

---

## Referências

- [ESP-IDF NVS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
- [ESP-IDF FAT Filesystem](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/fatfs.html)
