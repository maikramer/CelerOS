# NetworkManager - Gerenciador de Conectividade de Rede

O **NetworkManager** é um sistema completo de gerenciamento de conectividade de rede para ESP-IDF 6, inspirado no ConnectivityManager do Android. Ele fornece uma interface unificada e event-driven para gerenciar conexões WiFi (e futuramente Bluetooth).

## Índice

- [Características](#características)
- [Arquitetura](#arquitetura)
- [Componentes](#componentes)
- [Uso Básico](#uso-básico)
- [API Completa](#api-completa)
- [Eventos](#eventos)
- [Configuração](#configuração)
- [Exemplos Avançados](#exemplos-avançados)
- [Migração do ConnectionManager](#migração-do-connectionmanager)

---

## Características

- **Event-Driven**: Todas as operações reportam resultados através de eventos
- **Múltiplas Redes Conhecidas**: Suporte a até 20 redes WiFi salvas
- **Seleção Inteligente**: Algoritmo de seleção baseado em prioridade, sinal, segurança e histórico
- **Reconexão Agressiva**: Task em background para monitoramento e reconexão automática
- **Roaming Automático**: Troca para rede melhor quando disponível
- **Persistência NVS**: Credenciais e configurações salvas na memória não-volátil
- **Thread-Safe**: Operações protegidas por mutex
- **Preparado para Bluetooth**: Arquitetura modular para expansão futura

---

## Arquitetura

```
┌─────────────────────────────────────────────────────────────┐
│                      NetworkManager                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │    Events    │  │   Config     │  │    Stats     │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│                           │                                  │
│           ┌───────────────┼───────────────┐                  │
│           ▼               ▼               ▼                  │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐         │
│  │WiFiConnection│ │CredentialStore│ │NetworkSelector│        │
│  │  (Provider)  │ │  (Storage)   │ │ (Algorithm)  │         │
│  └──────────────┘ └──────────────┘ └──────────────┘         │
│           │               │               │                  │
│           ▼               ▼               ▼                  │
│      ESP-IDF WiFi       NVS          Scoring Logic           │
└─────────────────────────────────────────────────────────────┘
```

### Fluxo de Estados

```
                    ┌──────────────┐
                    │ Disconnected │◄────────────────┐
                    └──────┬───────┘                 │
                           │ scan()                  │
                           ▼                         │
                    ┌──────────────┐                 │
                    │   Scanning   │                 │
                    └──────┬───────┘                 │
                           │ network_found           │
                           ▼                         │
                    ┌──────────────┐                 │
             ┌─────►│  Connecting  │                 │
             │      └──────┬───────┘                 │
             │             │ auth_success            │ connection_lost
             │ retry       ▼                         │
             │      ┌──────────────┐                 │
             └──────┤  Connected   ├─────────────────┘
                    └──────┬───────┘
                           │ better_network
                           ▼
                    ┌──────────────┐
                    │   Roaming    │
                    └──────┬───────┘
                           │ handoff_complete
                           ▼
                    ┌──────────────┐
                    │  Connected   │
                    └──────────────┘
```

---

## Componentes

### NetworkManager

Gerenciador central singleton que orquestra todos os componentes.

```cpp
#include "NetworkManager.h"

NetworkManager& netMgr = NetworkManager::instance();
```

### NetworkCredentialStore

Armazenamento persistente de credenciais no NVS.

```cpp
#include "NetworkCredentialStore.h"

NetworkCredentialStore& store = NetworkCredentialStore::instance();
```

### NetworkSelector

Algoritmo de seleção inteligente de rede.

```cpp
#include "NetworkSelector.h"

NetworkSelector& selector = NetworkSelector::instance();
```

### NetworkTypes

Tipos e estruturas comuns usados em todo o sistema.

```cpp
#include "NetworkTypes.h"

// Enums disponíveis:
// - NetworkState: Disconnected, Scanning, Connecting, Connected, Roaming, Error
// - NetworkType: None, WiFi, Bluetooth
// - WiFiAuthMode: Open, WEP, WPA, WPA2, WPA3, Enterprise

// Structs disponíveis:
// - KnownNetwork: Rede conhecida com credenciais
// - ScannedNetwork: Rede encontrada em scan
// - NetworkInfo: Informações da rede ativa
// - NetworkConfig: Configurações do gerenciador
```

---

## Uso Básico

### Inicialização

```cpp
#include "NetworkManager.h"

void app_main() {
    // Obter instância singleton
    NetworkManager& netMgr = NetworkManager::instance();
    
    // Inicializar (inicia task de background automaticamente)
    ErrorCode err = netMgr.init();
    if (err != CommonErrorCodes::None) {
        ESP_LOGE("App", "Falha ao inicializar NetworkManager: %s", 
                 err.description());
        return;
    }
    
    ESP_LOGI("App", "NetworkManager inicializado!");
}
```

### Conectar a uma Rede Específica

```cpp
// Conectar e salvar credenciais automaticamente
ErrorCode err = netMgr.connect("MinhaRede", "MinhaSenha123");

if (err == CommonErrorCodes::None) {
    ESP_LOGI("App", "Conectado! IP: %s", netMgr.getIpAddress().c_str());
} else {
    ESP_LOGE("App", "Falha: %s", err.description());
}
```

### Conectar à Melhor Rede Conhecida

```cpp
// Escaneia e conecta à melhor rede disponível
ErrorCode err = netMgr.connectToKnown();
```

### Registrar Handlers de Eventos

```cpp
// Mudancas de estado (conectado, desconectado, reconectando...)
netMgr.onStateChanged.addHandler([](NetworkState oldState, NetworkState newState) {
    if (newState == NetworkState::Connected) {
        ESP_LOGI("App", "Conectado");
    } else if (newState == NetworkState::Disconnected) {
        ESP_LOGW("App", "Desconectado");
    }
});

// Detalhes da rede ativa: polling leve (getActiveNetwork/isConnected)
NetworkInfo info = netMgr.getActiveNetwork();
ESP_LOGI("App", "IP: %s", info.ip.c_str());
ESP_LOGI("App", "RSSI: %d dBm", info.rssi);

// Quando scan completar
netMgr.onScanCompleted.addHandler([](const std::vector<ScannedNetwork>& networks) {
    ESP_LOGI("App", "Encontradas %zu redes:", networks.size());
    for (const auto& net : networks) {
        ESP_LOGI("App", "  - %s (%d dBm) %s", 
                 net.ssid, net.rssi, net.isKnown ? "[conhecida]" : "");
    }
});
```

---

## API Completa

### Gerenciamento de Conexão

| Método | Descrição |
|--------|-----------|
| `init(bool startBackgroundTask = true)` | Inicializa o sistema |
| `deinit()` | Finaliza e libera recursos |
| `connect(ssid, password, saveOnSuccess = true)` | Conecta a uma rede específica |
| `connectToKnown()` | Conecta à melhor rede conhecida |
| `disconnect()` | Desconecta da rede atual |
| `reconnect()` | Reconecta à última rede |

### Gerenciamento de Redes Conhecidas

| Método | Descrição |
|--------|-----------|
| `addNetwork(KnownNetwork)` | Adiciona/atualiza rede conhecida |
| `removeNetwork(ssid)` | Remove rede conhecida |
| `setNetworkPriority(ssid, priority)` | Define prioridade (0-100) |
| `getKnownNetworks()` | Lista todas as redes conhecidas |
| `isKnownNetwork(ssid)` | Verifica se rede é conhecida |

### Scanning

| Método | Descrição |
|--------|-----------|
| `startScan(blocking = false)` | Inicia scan WiFi |
| `getLastScanResults()` | Obtém resultados do último scan |

### Estado

| Método | Descrição |
|--------|-----------|
| `getState()` | Estado atual (NetworkState) |
| `isConnected()` | Verifica se está conectado |
| `getActiveNetwork()` | Informações da rede ativa |
| `getCurrentRssi()` | Força do sinal atual |
| `getIpAddress()` | Endereço IP como string |
| `getStats()` | Estatísticas de operação |

### Configuração

| Método | Descrição |
|--------|-----------|
| `setAutoReconnect(bool)` | Habilita/desabilita reconexão automática |
| `setBackgroundScanInterval(seconds)` | Intervalo de scan em background |
| `setRoamingThreshold(dB)` | Diferença de RSSI para roaming |
| `setRoamingEnabled(bool)` | Habilita/desabilita roaming |
| `setConfig(NetworkConfig)` | Define configuração completa |
| `getConfig()` | Obtém configuração atual |

### Task de Background

| Método | Descrição |
|--------|-----------|
| `startBackgroundTask()` | Inicia task de monitoramento |
| `stopBackgroundTask()` | Para task de monitoramento |
| `isBackgroundTaskRunning()` | Verifica se task está rodando |

---

## Eventos

### Lista de Eventos

| Evento | Parâmetros | Descrição |
|--------|------------|-----------|
| `onStateChanged` | `(NetworkState old, NetworkState new)` | Estado mudou (conectado, desconectado, reconectando...) |
| `onScanCompleted` | `(vector<ScannedNetwork>& networks)` | Scan finalizado |

### Exemplo de Uso de Eventos

```cpp
// Usando lambda
netMgr.onStateChanged.addHandler([](NetworkState oldState, NetworkState newState) {
    if (newState == NetworkState::Connected) {
        // Fazer algo quando conectar
    }
});

// Usando função
void onStateChange(NetworkState oldState, NetworkState newState) {
    ESP_LOGI("App", "Estado: %d", (int)newState);
}
netMgr.onStateChanged.addHandler(onStateChange);

// Usando método de classe
class MyApp {
public:
    void setup() {
        netMgr.onStateChanged.addHandler(
            [this](NetworkState oldState, NetworkState newState) { this->handleState(newState); }
        );
    }
    
    void handleState(NetworkState state) {
        // ...
    }
};
```

---

## Configuração

### NetworkConfig

```cpp
struct NetworkConfig {
    bool autoReconnect;             // Reconexão automática (default: true)
    uint32_t backgroundScanInterval;// Intervalo de scan em segundos (default: 30)
    int8_t roamingThreshold;        // Diferença de RSSI para roaming (default: 10 dB)
    uint8_t maxRetries;             // Tentativas máximas de conexão (default: 5)
    uint32_t connectionTimeout;     // Timeout de conexão em ms (default: 10000)
    bool enableRoaming;             // Habilitar roaming automático (default: true)
};
```

### Configuração Personalizada

```cpp
NetworkConfig config;
config.autoReconnect = true;
config.backgroundScanInterval = 60;  // Scan a cada 60 segundos
config.roamingThreshold = 15;        // Exigir 15 dB de melhoria para roaming
config.maxRetries = 3;               // Apenas 3 tentativas
config.connectionTimeout = 15000;    // 15 segundos de timeout
config.enableRoaming = true;

netMgr.setConfig(config);
```

### SelectionConfig (NetworkSelector)

```cpp
SelectionConfig selConfig;
selConfig.priorityWeight = 0.4f;    // 40% peso para prioridade do usuário
selConfig.rssiWeight = 0.35f;       // 35% peso para sinal
selConfig.securityWeight = 0.15f;   // 15% peso para segurança
selConfig.historyWeight = 0.10f;    // 10% peso para histórico
selConfig.minimumRssi = -85;        // Ignorar redes com sinal < -85 dBm
selConfig.preferKnownNetworks = true;
selConfig.requireSecure = false;    // Permitir redes abertas

NetworkSelector::instance().setConfig(selConfig);
```

---

## Exemplos Avançados

### Gerenciamento Completo de Redes

```cpp
void setupNetworks() {
    NetworkManager& netMgr = NetworkManager::instance();
    
    // Adicionar rede de alta prioridade (casa)
    KnownNetwork home("MinhaRedeWiFi", "senha123");
    home.priority = 100;  // Máxima prioridade
    home.autoConnect = true;
    netMgr.addNetwork(home);
    
    // Adicionar rede de média prioridade (trabalho)
    KnownNetwork work("EmpresaWiFi", "work_pass");
    work.priority = 80;
    work.autoConnect = true;
    netMgr.addNetwork(work);
    
    // Adicionar rede de backup (hotspot celular)
    KnownNetwork mobile("MeuCelular", "hotspot123");
    mobile.priority = 30;  // Baixa prioridade
    mobile.autoConnect = true;
    netMgr.addNetwork(mobile);
    
    // Listar redes conhecidas
    auto networks = netMgr.getKnownNetworks();
    for (const auto& net : networks) {
        ESP_LOGI("App", "Rede: %s (prioridade: %d, auto: %s)", 
                 net.ssid, net.priority, net.autoConnect ? "sim" : "não");
    }
}
```

### Monitoramento de Conexão

```cpp
void monitorConnection() {
    NetworkManager& netMgr = NetworkManager::instance();
    
    // Verificar estado periodicamente
    while (true) {
        if (netMgr.isConnected()) {
            NetworkInfo info = netMgr.getActiveNetwork();
            ESP_LOGI("Monitor", "Conectado: %s | IP: %s | RSSI: %d dBm",
                     info.ssid.c_str(), info.ip.c_str(), info.rssi);
        } else {
            ESP_LOGW("Monitor", "Desconectado (estado: %s)",
                     networkStateToString(netMgr.getState()));
        }
        
        // Estatísticas
        NetworkManagerStats stats = netMgr.getStats();
        ESP_LOGI("Monitor", "Conexões: %lu | Falhas: %lu | Roaming: %lu",
                 stats.totalConnections, stats.failedConnections, stats.roamingEvents);
        
        vTaskDelay(pdMS_TO_TICKS(10000));  // A cada 10 segundos
    }
}
```

### Interface de Configuração WiFi (via comandos)

```cpp
void handleWifiCommand(const std::string& cmd, const std::string& arg1, const std::string& arg2) {
    NetworkManager& netMgr = NetworkManager::instance();
    
    if (cmd == "connect") {
        netMgr.connect(arg1, arg2);
    }
    else if (cmd == "disconnect") {
        netMgr.disconnect();
    }
    else if (cmd == "scan") {
        netMgr.startScan(true);  // Blocking
        auto results = netMgr.getLastScanResults();
        for (const auto& net : results) {
            printf("%s\t%d dBm\t%s\n", net.ssid, net.rssi,
                   net.isKnown ? "[salva]" : "");
        }
    }
    else if (cmd == "status") {
        if (netMgr.isConnected()) {
            NetworkInfo info = netMgr.getActiveNetwork();
            printf("Conectado: %s\n", info.ssid.c_str());
            printf("IP: %s\n", info.ip.c_str());
            printf("RSSI: %d dBm\n", info.rssi);
        } else {
            printf("Desconectado\n");
        }
    }
    else if (cmd == "forget") {
        netMgr.removeNetwork(arg1);
        printf("Rede %s removida\n", arg1.c_str());
    }
    else if (cmd == "priority") {
        netMgr.setNetworkPriority(arg1, std::stoi(arg2));
        printf("Prioridade de %s definida para %s\n", arg1.c_str(), arg2.c_str());
    }
    else if (cmd == "list") {
        auto networks = netMgr.getKnownNetworks();
        for (const auto& net : networks) {
            printf("%s\tprio:%d\tauto:%s\n", 
                   net.ssid, net.priority, net.autoConnect ? "sim" : "não");
        }
    }
}
```

### Uso com Task Dedicada

```cpp
void wifiTask(void* param) {
    NetworkManager& netMgr = NetworkManager::instance();
    
    // Registrar handlers
    netMgr.onStateChanged.addHandler([](NetworkState oldState, NetworkState newState) {
        // Notificar outras tasks que WiFi está disponível
        if (newState == NetworkState::Connected) {
            xEventGroupSetBits(appEventGroup, WIFI_CONNECTED_BIT);
        } else if (newState == NetworkState::Disconnected) {
            xEventGroupClearBits(appEventGroup, WIFI_CONNECTED_BIT);
        }
    });
    
    // Inicializar
    netMgr.init();
    
    // Tentar conectar
    netMgr.connectToKnown();
    
    // Task principal - o background task cuida da reconexão
    while (true) {
        // Fazer outras coisas...
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Migração do ConnectionManager

O `ConnectionManager` antigo está **deprecated**. Para migrar:

### Antes (ConnectionManager)

```cpp
#include "ConnectionManager.h"

ConnectionManager::initialize(5);
ConnectionManager::connect(123);
```

### Depois (NetworkManager)

```cpp
#include "NetworkManager.h"

NetworkManager& netMgr = NetworkManager::instance();
netMgr.init();
netMgr.connect("MinhaRede", "MinhaSenha");
```

### Principais Diferenças

| Aspecto | ConnectionManager (antigo) | NetworkManager (novo) |
|---------|---------------------------|----------------------|
| Propósito | Pool de conexões genéricas | Gerenciamento de rede |
| Credenciais | Não gerenciava | Persistência no NVS |
| Múltiplas redes | Não suportava | Até 20 redes |
| Seleção | Manual | Automática inteligente |
| Reconexão | Manual | Automática em background |
| Eventos | Limitados | Completos |
| Roaming | Não | Sim |

---

## Resolução de Problemas

### WiFi não conecta

1. Verifique as credenciais
2. Verifique se o NVS foi inicializado
3. Aumente o timeout: `netMgr.setConfig()` com `connectionTimeout` maior

### Roaming não funciona

1. Verifique se está habilitado: `netMgr.setRoamingEnabled(true)`
2. Ajuste o threshold: `netMgr.setRoamingThreshold(5)` para ser menos exigente

### Task de background não inicia

1. Verifique stack size disponível
2. Verifique prioridade da task
3. Inicie manualmente: `netMgr.startBackgroundTask()`

### Credenciais não são salvas

1. Verifique se NVS está inicializado
2. Verifique espaço no NVS
3. Use `saveOnSuccess = true` em `connect()`

---

## Licença

MIT License - Veja o arquivo LICENSE para detalhes.
