# SystemInfo - Informações do Sistema

Classe singleton para obter informações de hardware e runtime do ESP32.

## Índice

- [Características](#características)
- [Uso Básico](#uso-básico)
- [Informações do Chip](#informações-do-chip)
- [Memória](#memória)
- [Identificação](#identificação)
- [Runtime](#runtime)
- [Flash](#flash)
- [Exemplos](#exemplos)

---

## Características

- **Singleton**: Acesso global simplificado
- **Hardware**: Modelo do chip, revisão, cores, recursos
- **Memória**: Heap, PSRAM, fragmentação
- **Identificação**: MAC address, device ID único
- **Runtime**: Uptime, razão de reset
- **Flash**: Tamanho, velocidade, modo

---

## Uso Básico

```cpp
#include "SystemInfo.h"

void printSystemInfo() {
    auto& sys = SystemInfo::instance();
    
    ESP_LOGI("System", "Chip: %s", sys.getChipModel().c_str());
    ESP_LOGI("System", "MAC: %s", sys.getMacAddress().c_str());
    ESP_LOGI("System", "Free Heap: %lu bytes", sys.getFreeHeap());
    ESP_LOGI("System", "Uptime: %s", sys.getFormattedUptime().c_str());
}
```

---

## Informações do Chip

### ChipInfo Completo

```cpp
ChipInfo info = sys.getChipInfo();

ESP_LOGI("Chip", "Modelo: %s", info.model.c_str());
ESP_LOGI("Chip", "Cores: %d", info.cores);
ESP_LOGI("Chip", "Revisão: %d", info.revision);
ESP_LOGI("Chip", "WiFi: %s", info.hasWifi ? "Sim" : "Não");
ESP_LOGI("Chip", "Bluetooth: %s", info.hasBluetooth ? "Sim" : "Não");
ESP_LOGI("Chip", "BLE: %s", info.hasBLE ? "Sim" : "Não");
ESP_LOGI("Chip", "Flash embutida: %s", info.hasEmbeddedFlash ? "Sim" : "Não");
ESP_LOGI("Chip", "PSRAM embutida: %s", info.hasEmbeddedPsram ? "Sim" : "Não");
```

### Métodos Individuais

```cpp
std::string model = sys.getChipModel();     // "ESP32", "ESP32-S3", etc
uint8_t cores = sys.getCoreCount();          // 1 ou 2
uint8_t rev = sys.getChipRevision();         // Número da revisão
```

---

## Memória

### MemoryInfo Completo

```cpp
MemoryInfo mem = sys.getMemoryInfo();

ESP_LOGI("Mem", "Heap livre: %lu bytes", mem.freeHeap);
ESP_LOGI("Mem", "Heap mínimo: %lu bytes", mem.minFreeHeap);
ESP_LOGI("Mem", "Maior bloco: %lu bytes", mem.largestFreeBlock);
ESP_LOGI("Mem", "Heap total: %lu bytes", mem.totalHeap);
ESP_LOGI("Mem", "PSRAM livre: %lu bytes", mem.freePsram);
ESP_LOGI("Mem", "PSRAM total: %lu bytes", mem.totalPsram);
```

### Métodos Individuais

```cpp
uint32_t freeHeap = sys.getFreeHeap();
uint32_t minFree = sys.getMinFreeHeap();        // Menor valor já registrado
uint32_t largestBlock = sys.getLargestFreeBlock(); // Fragmentação

// PSRAM
if (sys.hasPsram()) {
    uint32_t freePsram = sys.getFreePsram();
}
```

### Monitoramento de Memória

```cpp
void checkMemory() {
    auto& sys = SystemInfo::instance();
    
    uint32_t free = sys.getFreeHeap();
    uint32_t minFree = sys.getMinFreeHeap();
    
    // Alerta se memória baixa
    if (free < 20000) {
        ESP_LOGW("Mem", "Memória baixa: %lu bytes", free);
    }
    
    // Verificar fragmentação
    uint32_t largest = sys.getLargestFreeBlock();
    if (largest < free / 2) {
        ESP_LOGW("Mem", "Heap fragmentado! Livre: %lu, Maior bloco: %lu", 
                 free, largest);
    }
}
```

---

## Identificação

### MAC Address

```cpp
// Como string formatada
std::string mac = sys.getMacAddress();
// "AA:BB:CC:DD:EE:FF"

// Como bytes
uint8_t macBytes[6];
sys.getMacAddressBytes(macBytes);
```

### Device ID

```cpp
// ID curto (últimos 6 caracteres do MAC)
std::string shortId = sys.getDeviceId();
// "DDEEFF"

// ID completo (MAC sem separadores)
std::string fullId = sys.getFullDeviceId();
// "AABBCCDDEEFF"
```

### Uso em Identificação

```cpp
// Nome único para dispositivo
std::string deviceName = "ESP32-" + sys.getDeviceId();
// "ESP32-DDEEFF"

// Topic MQTT único
std::string topic = "devices/" + sys.getFullDeviceId() + "/data";
// "devices/AABBCCDDEEFF/data"
```

---

## Runtime

### Uptime

```cpp
uint32_t upSec = sys.getUptimeSeconds();
uint64_t upMs = sys.getUptimeMillis();

// Formatado
std::string uptime = sys.getFormattedUptime();
// "01:23:45" ou "2d 01:23:45" se > 24h
```

### Razão de Reset

```cpp
ResetReason reason = sys.getResetReason();

switch (reason) {
    case ResetReason::PowerOn:
        ESP_LOGI("Boot", "Power-on reset");
        break;
    case ResetReason::Software:
        ESP_LOGI("Boot", "Software reset");
        break;
    case ResetReason::Panic:
        ESP_LOGW("Boot", "Reset por panic/exception!");
        break;
    case ResetReason::TaskWatchdog:
        ESP_LOGW("Boot", "Reset por watchdog!");
        break;
    case ResetReason::Brownout:
        ESP_LOGW("Boot", "Reset por queda de tensão!");
        break;
    case ResetReason::DeepSleep:
        ESP_LOGI("Boot", "Acordou do deep sleep");
        break;
    default:
        ESP_LOGI("Boot", "Razão: %s", sys.getResetReasonString().c_str());
}
```

### Todas as Razões de Reset

| ResetReason | Descrição |
|-------------|-----------|
| `Unknown` | Desconhecido |
| `PowerOn` | Ligado normalmente |
| `ExternalReset` | Botão reset pressionado |
| `Software` | `esp_restart()` chamado |
| `Panic` | Exception/panic no código |
| `IntWatchdog` | Watchdog de interrupção |
| `TaskWatchdog` | Watchdog de task |
| `Watchdog` | Outro watchdog |
| `DeepSleep` | Acordou do deep sleep |
| `Brownout` | Queda de tensão |
| `Sdio` | Reset via SDIO |

---

## Flash

### FlashInfo

```cpp
FlashInfo flash = sys.getFlashInfo();

ESP_LOGI("Flash", "Tamanho: %lu bytes (%lu MB)", 
         flash.size, flash.size / (1024 * 1024));
ESP_LOGI("Flash", "Velocidade: %lu MHz", flash.speed / 1000000);
ESP_LOGI("Flash", "Modo: %s", flash.mode.c_str());
```

### Método Direto

```cpp
uint32_t flashSize = sys.getFlashSize();
// Tamanho em bytes
```

---

## ESP-IDF

```cpp
std::string idfVersion = sys.getIdfVersion();
// "v5.1.2" ou similar
```

---

## Sumário Completo

```cpp
// Obter resumo de todas as informações
std::string summary = sys.getSummary();
ESP_LOGI("System", "\n%s", summary.c_str());
```

Exemplo de saída:

```
=== System Information ===
Chip: ESP32 rev.3 (2 cores)
WiFi: Yes, BT: Yes, BLE: Yes

Memory:
  Heap: 234567 / 327680 bytes (71%)
  Min Free: 198765 bytes
  PSRAM: 4194304 bytes

Flash: 4MB @ 80MHz (QIO)

MAC: AA:BB:CC:DD:EE:FF
Device ID: AABBCCDDEEFF

Uptime: 1d 02:34:56
Reset: Power-on

ESP-IDF: v5.1.2
```

---

## Exemplos

### Log de Boot

```cpp
void logBootInfo() {
    auto& sys = SystemInfo::instance();
    
    ESP_LOGI("Boot", "===============================");
    ESP_LOGI("Boot", "Device: %s", sys.getChipModel().c_str());
    ESP_LOGI("Boot", "ID: %s", sys.getDeviceId().c_str());
    ESP_LOGI("Boot", "Reason: %s", sys.getResetReasonString().c_str());
    ESP_LOGI("Boot", "Free Heap: %lu bytes", sys.getFreeHeap());
    ESP_LOGI("Boot", "IDF: %s", sys.getIdfVersion().c_str());
    ESP_LOGI("Boot", "===============================");
}
```

### Diagnóstico Periódico

```cpp
void diagnosticTask(void* param) {
    auto& sys = SystemInfo::instance();
    
    while (true) {
        ESP_LOGI("Diag", "Uptime: %s | Heap: %lu/%lu | Min: %lu",
                 sys.getFormattedUptime().c_str(),
                 sys.getFreeHeap(),
                 sys.getMemoryInfo().totalHeap,
                 sys.getMinFreeHeap());
        
        vTaskDelay(pdMS_TO_TICKS(60000));  // A cada minuto
    }
}
```

### Telemetria

```cpp
std::string buildTelemetryJson() {
    auto& sys = SystemInfo::instance();
    auto& time = TimeManager::instance();
    
    char json[512];
    snprintf(json, sizeof(json),
        R"({
            "device_id": "%s",
            "timestamp": "%s",
            "uptime_sec": %lu,
            "free_heap": %lu,
            "min_heap": %lu,
            "reset_reason": "%s"
        })",
        sys.getFullDeviceId().c_str(),
        time.getIsoTimestamp().c_str(),
        sys.getUptimeSeconds(),
        sys.getFreeHeap(),
        sys.getMinFreeHeap(),
        sys.getResetReasonString().c_str()
    );
    
    return std::string(json);
}
```

### Verificação de Saúde

```cpp
struct HealthCheck {
    bool memoryOk;
    bool uptimeOk;
    bool resetOk;
    
    std::string toJson() const {
        char json[128];
        snprintf(json, sizeof(json),
            R"({"memory_ok": %s, "uptime_ok": %s, "reset_ok": %s})",
            memoryOk ? "true" : "false",
            uptimeOk ? "true" : "false",
            resetOk ? "true" : "false");
        return json;
    }
};

HealthCheck checkHealth() {
    auto& sys = SystemInfo::instance();
    HealthCheck health;
    
    // Memória > 10KB livre
    health.memoryOk = sys.getFreeHeap() > 10000;
    
    // Uptime > 60 segundos (boot completo)
    health.uptimeOk = sys.getUptimeSeconds() > 60;
    
    // Não foi watchdog/panic
    auto reason = sys.getResetReason();
    health.resetOk = (reason != ResetReason::Panic &&
                      reason != ResetReason::TaskWatchdog &&
                      reason != ResetReason::IntWatchdog);
    
    return health;
}
```

---

## Referências

- [ESP-IDF System API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system.html)
- [ESP-IDF Heap Memory](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html)
