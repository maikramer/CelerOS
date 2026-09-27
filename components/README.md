# Componentes do KryonOS

Componentes ESP-IDF internos do projeto — no mesmo nível dos futuros
componentes do próprio KryonOS (Kernel, Launcher, Settings, ...), sem
dependência de pacote externo. Importados da biblioteca compartilhada
`esp_components` (satisfaction-hub) em 2026-09-27 e nivelados para viver
dentro deste repo:

- `idf_component.yml` de cada um declara **apenas** dependências de registry
  (ex.: `johboh/nlohmann-json`). Dependências entre componentes irmãos são
  resolvidas pelos `REQUIRES` dos CMakeLists — nada de `path: ../...`.
- Documentação original de cada módulo (`*.md` dentro do componente) foi
  mantida.

## Componentes

| Componente | O que dá | Requer (irmãos) |
|---|---|---|
| `Utility` | `Event<T>` pub/sub, `Singleton`, `Timeout`, helpers | — (nlohmann via registry) |
| `ErrorCodes` | Sistema tipado de `ErrorCode` + `CommonErrorCodes` | Utility |
| `JsonModels` | Modelos/serialização sobre nlohmann/json | — (nlohmann via registry) |
| `Http` | `HttpClient` — wrapper de `esp_http_client` com headers/timeout | — |
| `Storage` | `Storage` (KV texto com fallback NVS), `NVS`, `Flash`, `SdCard` | Utility, JsonModels |
| `Connection` | `NetworkManager` (connect/scan/roaming/auto-reconnect), `NetworkCredentialStore` (multi-redes em NVS), `NetworkSelector` | Utility, JsonModels, ErrorCodes, Storage |
| `Wifi` | `WifiConnection`, `WifiOta` (OTA via `esp_https_ota` com eventos de progresso), `OtaManager` (check de versão + facade), `CaptivePortal` (AP + DNS + portal), `WifiAP` | Utility, Connection, ErrorCodes, Storage, Http |
| `System` | `SystemInfo` — chip, MAC, heap, flash, uptime | — |

## Não importados (por enquanto)

`Time` (o KryonOS tem `TimeManager` próprio), `Supabase`, `UI` (LVGL),
`BluetoothServer`, `SafeContainers` (ninguém inclui), `Drivers`, `IoUtility`,
`UserManaging`. Se precisar de um deles, copie do `esp_components` e aplique
o mesmo nivelamento de yml.

## Requisitos no projeto raiz

Estes componentes assumem opções de sdkconfig do projeto raiz:

```
CONFIG_COMPILER_CXX_EXCEPTIONS=y      # JsonModels/OtaManager usam try/catch
CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y     # OTA dev via HTTP na LAN
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y  # HTTPS geral
```

E partições com slots `ota_0`/`ota_1` + `otadata` para o fluxo de OTA.
