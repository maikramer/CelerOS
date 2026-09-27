# OTA Manager - Guia de Uso

## Visão Geral

O `OtaManager` é um componente que facilita atualizações OTA (Over-The-Air) usando `device_id` para identificar dispositivos específicos. Ele encapsula o `WifiOta` e adiciona funcionalidades para:

- Obter `device_id` automaticamente do MAC address do ESP32
- Construir URLs de OTA com `device_id` como parâmetro
- Verificar atualizações disponíveis via API REST
- Gerenciar eventos de progresso e status
- Integração com o [OTA Server](../tools/ota-server/README.md) incluído no projeto

## Funcionalidades

### 1. Obter Device ID

O `device_id` é gerado automaticamente a partir do MAC address do ESP32 no formato hexadecimal (ex: `A1B2C3D4E5F6`).

```cpp
auto& otaManager = OtaManager::instance();
otaManager.init();
const char* deviceId = otaManager.getDeviceId();
ESP_LOGI("APP", "Device ID: %s", deviceId);
```

### 2. Verificar Atualizações Disponíveis

```cpp
auto& otaManager = OtaManager::instance();
char availableVersion[32] = {0};

bool hasUpdate = otaManager.checkForUpdate(
    "https://api.example.com/ota",  // URL base do servidor OTA
    "1.0.0",                          // Versão atual do firmware
    availableVersion,                 // Buffer para receber versão disponível
    sizeof(availableVersion)         // Tamanho do buffer
);

if (hasUpdate) {
    ESP_LOGI("APP", "Atualização disponível: %s", availableVersion);
} else {
    ESP_LOGI("APP", "Firmware está atualizado");
}
```

### 3. Iniciar Atualização OTA

#### Opção 1: Usando URL base (recomendado)

```cpp
auto& otaManager = OtaManager::instance();
ErrorCode result = otaManager.startUpdate(
    "https://api.example.com/ota",  // URL base
    nullptr                           // Versão específica (nullptr = última versão)
);

if (result == CommonErrorCodes::None) {
    ESP_LOGI("APP", "OTA iniciado com sucesso");
} else {
    ESP_LOGE("APP", "Erro ao iniciar OTA: %s", result.description().c_str());
}
```

#### Opção 2: Usando URL completa

```cpp
std::string otaUrl = "https://api.example.com/ota/firmware.bin?device_id=A1B2C3D4E5F6";
ErrorCode result = otaManager.startUpdateFromUrl(otaUrl);
```

### 4. Construir URL de OTA

```cpp
auto& otaManager = OtaManager::instance();
std::string url = otaManager.buildOtaUrl(
    "https://api.example.com/ota",  // URL base
    "1.1.0"                          // Versão específica (opcional)
);
// Resultado: "https://api.example.com/ota?device_id=A1B2C3D4E5F6&version=1.1.0"
```

### 5. Eventos

O `OtaManager` expõe eventos para monitorar o progresso da atualização:

```cpp
auto& otaManager = OtaManager::instance();

// Evento quando OTA inicia
otaManager.onUpdateStart.addHandler([]() {
    ESP_LOGI("APP", "OTA update iniciado");
});

// Evento quando OTA completa com sucesso
otaManager.onUpdateComplete.addHandler([]() {
    ESP_LOGI("APP", "OTA update concluído! Reiniciando...");
    esp_restart();
});

// Evento quando OTA falha
otaManager.onUpdateFailed.addHandler([]() {
    ESP_LOGE("APP", "OTA update falhou");
});

// Evento de progresso (0-100)
otaManager.onProgress.addHandler([](int progress) {
    ESP_LOGI("APP", "Progresso: %d%%", progress);
});
```

## Formato de Resposta do Servidor

O servidor OTA deve responder no seguinte formato JSON quando `action=check`:

```json
{
    "update_available": true,
    "version": "1.1.0",
    "url": "https://api.example.com/ota/firmware.bin"
}
```

Para o download do firmware, o servidor deve retornar o binário diretamente.

## Exemplo Completo

```cpp
#include "OtaManager.h"
#include "NetworkManager.h"
#include "esp_log.h"

void checkAndUpdateFirmware() {
    auto& network = NetworkManager::instance();
    if (!network.isConnected()) {
        ESP_LOGE("APP", "WiFi não conectado");
        return;
    }
    
    auto& otaManager = OtaManager::instance();
    otaManager.init();
    
    // Configurar eventos
    otaManager.onUpdateStart.addHandler([]() {
        ESP_LOGI("APP", "Iniciando atualização OTA...");
    });
    
    otaManager.onUpdateComplete.addHandler([]() {
        ESP_LOGI("APP", "Atualização concluída! Reiniciando em 3 segundos...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    });
    
    otaManager.onProgress.addHandler([](int progress) {
        ESP_LOGI("APP", "Progresso: %d%%", progress);
    });
    
    // Verificar atualizações
    char availableVersion[32] = {0};
    bool hasUpdate = otaManager.checkForUpdate(
        "https://api.example.com/ota",
        "1.0.0",
        availableVersion,
        sizeof(availableVersion)
    );
    
    if (hasUpdate) {
        ESP_LOGI("APP", "Atualização disponível: %s", availableVersion);
        
        // Iniciar atualização
        ErrorCode result = otaManager.startUpdate("https://api.example.com/ota", nullptr);
        if (result != CommonErrorCodes::None) {
            ESP_LOGE("APP", "Erro ao iniciar OTA: %s", result.description().c_str());
        }
    } else {
        ESP_LOGI("APP", "Firmware está atualizado");
    }
}
```

## Usando com OTA Server (Incluído)

O projeto inclui um servidor OTA completo em `tools/ota-server/`. Para usá-lo:

```cpp
#include "OtaManager.h"
#include "esp_app_desc.h"

void check_updates() {
    auto& otaManager = OtaManager::instance();
    otaManager.init();
    
    const char* currentVersion = esp_app_get_description()->version;
    
    // Verificar atualização usando o novo método
    OtaUpdateInfo info = otaManager.checkForUpdateFromServer(
        "http://192.168.1.100:10234",  // IP do servidor OTA
        currentVersion
    );
    
    if (info.success && info.updateAvailable) {
        ESP_LOGI(TAG, "Nova versão: %s (%d bytes)", 
                 info.latestVersion, info.firmwareSize);
        ESP_LOGI(TAG, "Descrição: %s", info.description);
        
        // Iniciar download
        otaManager.startUpdate("http://192.168.1.100:10234/ota");
    }
}
```

### Iniciando o Servidor OTA

```bash
cd tools/ota-server

# Com Docker (recomendado)
docker compose up -d

# Ou sem Docker
npm install
npm start
```

**Interface Web**: http://localhost:10234

> Veja [tools/ota-server/README.md](../tools/ota-server/README.md) para documentação completa.

---

## Requisitos do Servidor OTA

O servidor OTA deve suportar:

1. **Endpoint de verificação**: `GET /ota/check?device_id=<ID>&version=<VERSION>`
   - Retorna JSON com `updateAvailable`, `latestVersion`, `firmwareSize`, `description`

2. **Endpoint de download**: `GET /ota?device_id=<ID>` ou `GET /ota`
   - Retorna o binário do firmware diretamente

3. **Suporte HTTPS**: O ESP-IDF requer HTTPS para OTA (ou HTTP se `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`)

## Notas Importantes

- O `device_id` é gerado automaticamente a partir do MAC address do ESP32
- O componente requer WiFi conectado antes de iniciar OTA
- O dispositivo reiniciará automaticamente após atualização bem-sucedida
- Certifique-se de ter partições OTA configuradas no `partitions.csv`
- Use `checkForUpdateFromServer()` para obter informações completas da atualização