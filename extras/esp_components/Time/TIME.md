# TimeManager - Sincronização de Tempo

Gerenciador de tempo para ESP32 com sincronização NTP e suporte a timezones.

## Índice

- [Características](#características)
- [Inicialização](#inicialização)
- [Sincronização](#sincronização)
- [Timezones](#timezones)
- [Obter Tempo](#obter-tempo)
- [Eventos](#eventos)
- [Exemplos](#exemplos)

---

## Características

- **SNTP**: Sincronização automática via NTP
- **Multi-servidor**: Fallback entre servidores NTP
- **Timezones**: Suporte a fusos horários POSIX
- **Auto-resync**: Ressincronização periódica
- **Eventos**: Callbacks para sync completo/falha
- **Singleton**: Acesso global simplificado

---

## Inicialização

### Básica

```cpp
#include "TimeManager.h"

void setup() {
    auto& time = TimeManager::instance();
    
    // Inicializar com servidor padrão (pool.ntp.org)
    if (time.init()) {
        ESP_LOGI("Time", "TimeManager inicializado");
    }
    
    // Sincronizar (bloqueante)
    if (time.sync(true)) {
        ESP_LOGI("Time", "Tempo sincronizado: %s", time.getFormattedTime().c_str());
    }
}
```

### Com Configuração Completa

```cpp
TimeConfig config;
config.primaryServer = "pool.ntp.org";
config.secondaryServer = "time.google.com";
config.tertiaryServer = "time.cloudflare.com";
config.timezone = "BRT3";           // Brasil
config.syncIntervalSeconds = 3600;  // Resync a cada hora
config.syncTimeoutMs = 15000;       // Timeout de 15s
config.maxRetries = 3;              // 3 tentativas

auto& time = TimeManager::instance();
time.init(config);
```

---

## Sincronização

### Sync Bloqueante

```cpp
// Aguarda até sincronizar ou timeout
bool success = time.sync(true);

if (success) {
    ESP_LOGI("Time", "Sincronizado!");
} else {
    ESP_LOGE("Time", "Falha na sincronização");
}
```

### Sync Não-Bloqueante

```cpp
// Inicia sync em background
time.sync(false);

// Verificar depois
if (time.isSynced()) {
    ESP_LOGI("Time", "Tempo válido");
}
```

### Status de Sincronização

```cpp
TimeSyncStatus status = time.getSyncStatus();

switch (status) {
    case TimeSyncStatus::NotStarted:
        ESP_LOGI("Time", "Sync não iniciada");
        break;
    case TimeSyncStatus::InProgress:
        ESP_LOGI("Time", "Sincronizando...");
        break;
    case TimeSyncStatus::Completed:
        ESP_LOGI("Time", "Sincronizado");
        break;
    case TimeSyncStatus::Failed:
        ESP_LOGE("Time", "Sync falhou");
        break;
}

// Última sincronização
time_t lastSync = time.getLastSyncTime();
if (lastSync > 0) {
    ESP_LOGI("Time", "Última sync há %d segundos", time(nullptr) - lastSync);
}
```

---

## Timezones

### Definir Timezone

```cpp
// Brasil (sem horário de verão)
time.setTimezone("BRT3");

// Brasil (com horário de verão - quando aplicável)
time.setTimezone("BRT3BRST,M10.3.0/0,M2.3.0/0");

// EUA Eastern
time.setTimezone("EST5EDT,M3.2.0,M11.1.0");

// EUA Pacific
time.setTimezone("PST8PDT,M3.2.0,M11.1.0");

// Europa Central
time.setTimezone("CET-1CEST,M3.5.0,M10.5.0/3");

// UTC
time.setTimezone("UTC0");
```

### Formato POSIX

O formato é: `STD<offset>DST,<start>,<end>`

- `STD`: Nome do timezone padrão (ex: BRT, EST)
- `offset`: Horas de diferença para UTC (positivo = oeste)
- `DST`: Nome do horário de verão (opcional)
- `start/end`: Regras de início/fim do DST

### Timezones Comuns no Brasil

| Região | Timezone |
|--------|----------|
| Brasília (BRT) | `BRT3` |
| Manaus (AMT) | `AMT4` |
| Fernando de Noronha | `FNT2` |
| Acre (ACT) | `ACT5` |

---

## Obter Tempo

### time_t (Unix Timestamp)

```cpp
time_t now = time.getTime();
uint64_t unixTs = time.getUnixTimestamp();
uint64_t unixMs = time.getUnixTimestampMs();
```

### struct tm

```cpp
// Hora local (com timezone)
struct tm local = time.getLocalTime();
ESP_LOGI("Time", "Hora local: %02d:%02d:%02d", 
         local.tm_hour, local.tm_min, local.tm_sec);

// Hora UTC
struct tm utc = time.getUtcTime();
ESP_LOGI("Time", "UTC: %02d:%02d:%02d", 
         utc.tm_hour, utc.tm_min, utc.tm_sec);
```

### Formatado

```cpp
// Data e hora (padrão: YYYY-MM-DD HH:MM:SS)
std::string datetime = time.getFormattedTime();
// "2025-01-15 14:30:45"

// Apenas data
std::string date = time.getFormattedDate();
// "2025-01-15"

// Formato customizado
std::string custom = time.getFormattedTime("%d/%m/%Y %H:%M");
// "15/01/2025 14:30"

// ISO 8601
std::string iso = time.getIsoTimestamp();
// "2025-01-15T14:30:45-03:00"
```

### Formatos strftime

| Código | Descrição | Exemplo |
|--------|-----------|---------|
| `%Y` | Ano (4 dígitos) | 2025 |
| `%m` | Mês (01-12) | 01 |
| `%d` | Dia (01-31) | 15 |
| `%H` | Hora (00-23) | 14 |
| `%M` | Minuto (00-59) | 30 |
| `%S` | Segundo (00-59) | 45 |
| `%j` | Dia do ano (001-366) | 015 |
| `%w` | Dia da semana (0-6, dom=0) | 3 |
| `%a` | Dia abreviado | Wed |
| `%b` | Mês abreviado | Jan |

---

## Eventos

### Sync Completo

```cpp
time.onSynced.addHandler([](time_t syncTime) {
    ESP_LOGI("Time", "Sincronizado em: %s", ctime(&syncTime));
});
```

### Sync Falhou

```cpp
time.onSyncFailed.addHandler([](const std::string& error) {
    ESP_LOGE("Time", "Sync falhou: %s", error.c_str());
});
```

---

## Validação

```cpp
// Verificar se o tempo parece válido (ano > 2020)
if (time.isTimeValid()) {
    // Tempo confiável
}

// Verificar se sincronizou ao menos uma vez
if (time.isSynced()) {
    // Sincronizado
}
```

---

## Exemplos

### Timestamp para Logs

```cpp
void logWithTimestamp(const char* message) {
    auto& time = TimeManager::instance();
    
    if (time.isSynced()) {
        ESP_LOGI("App", "[%s] %s", 
                 time.getFormattedTime().c_str(), message);
    } else {
        ESP_LOGI("App", "[NO_TIME] %s", message);
    }
}
```

### Agendamento Simples

```cpp
bool isBusinessHours() {
    auto& time = TimeManager::instance();
    
    if (!time.isSynced()) return true;  // Assume sim se não sincronizado
    
    struct tm local = time.getLocalTime();
    
    // Segunda a sexta, 8h às 18h
    bool isWeekday = (local.tm_wday >= 1 && local.tm_wday <= 5);
    bool isWorkHours = (local.tm_hour >= 8 && local.tm_hour < 18);
    
    return isWeekday && isWorkHours;
}
```

### Sincronização após WiFi

```cpp
// No handler de conexão WiFi
wifi.onConnected.addHandler([]() {
    auto& time = TimeManager::instance();
    
    if (!time.isInitialized()) {
        time.init();
    }
    
    // Sync não-bloqueante
    time.sync(false);
});

time.onSynced.addHandler([](time_t t) {
    ESP_LOGI("App", "Tempo sincronizado após conexão WiFi");
});
```

### Cálculo de Duração

```cpp
class Timer {
public:
    void start() {
        _startMs = TimeManager::instance().getUnixTimestampMs();
    }
    
    uint32_t elapsedMs() const {
        return TimeManager::instance().getUnixTimestampMs() - _startMs;
    }
    
    float elapsedSeconds() const {
        return elapsedMs() / 1000.0f;
    }
    
private:
    uint64_t _startMs = 0;
};
```

### Formatação para API

```cpp
std::string getApiTimestamp() {
    auto& time = TimeManager::instance();
    
    if (!time.isSynced()) {
        return "";
    }
    
    // ISO 8601 para APIs REST
    return time.getIsoTimestamp();
}

// Uso em JSON
void sendSensorData(float value) {
    char json[128];
    snprintf(json, sizeof(json),
        R"({"value": %.2f, "timestamp": "%s"})",
        value, getApiTimestamp().c_str());
    
    // Enviar para API...
}
```

---

## Configuração Padrão

```cpp
TimeConfig defaultConfig;
// primaryServer = "pool.ntp.org"
// secondaryServer = "time.google.com"
// tertiaryServer = "time.cloudflare.com"
// timezone = "UTC0"
// syncIntervalSeconds = 3600 (1 hora)
// syncTimeoutMs = 15000 (15 segundos)
// maxRetries = 3
```

---

## Referências

- [ESP-IDF SNTP](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html)
- [POSIX Timezone Format](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html)
- [NTP Pool Project](https://www.ntppool.org/)
