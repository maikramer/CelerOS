# Componentes do CelerOS

Componentes ESP-IDF internos do projeto — vendados da lib compartilhada
`esp_components` (satisfaction-hub) e **nivelados**: os `idf_component.yml`
declaram apenas dependências de registry (ex.: `johboh/nlohmann-json`);
dependências entre irmãos ficam nos `REQUIRES` dos CMakeLists, sem `path:`.

## Mapa de adoção (quem usa o quê no main/)

| Componente | Usado por | Como |
|---|---|---|
| `Connection` | `WebManager` | **NetworkManager** é o dono do rádio: boot (`init(true)` + `connectToKnown`), reconnect/roaming pela background task, scans; **NetworkCredentialStore** guarda as credenciais em NVS (import one-shot do `wifi.txt` legado) |
| `Wifi` | `WebManager`, `WifiSetupPortal`, `OTA/OtaManager` | **WifiConnection** (via NM), **WifiAP** (AP do portal, com coexistência APSTA), **CaptivePortal** (página+DNS+httpd do portal; ver patch abaixo), **WifiOta** (flash OTA com `Event<int> onProgress`) |
| `Http` | `OTA`, `JSBindings Net.*`, `AppStoreUI`, `HelpCenterUI` | `HttpClient` para GET/POST/downloads com progresso |
| `System` | `JSBindings System.getInfo`, `SettingsUI::drawAbout` | `SystemInfo` (chip/heap/PSRAM/uptime/reset reason/MAC) |
| `Utility` | (transitivo, todos) | `Event<>` pub/sub usado por NM/WifiOta/portal — handlers rodam sob mutex: só tocam flags |
| `ErrorCodes` | (transitivo) | `ErrorCode`/`CommonErrorCodes` |
| `JsonModels` | (transitivo p/ Connection/Storage) | modelos nlohmann |
| `Storage` | — (só a classe `NVS` é utilizável de graça) | **`Storage::initialize()` NÃO é chamado**: ele montaria SPIFFS na partição `spiffs` que o CelerOS usa como LittleFS (`esp_littlefs`, ponto de montagem `/local`). O FileSystem do OS é próprio (`main/FileSystem`, POSIX VFS) |

Não usados pelo `Wifi` do satisfaction-hub: `WifiServer`, `WifiClient`, `Telnet`.

## Patches CelerOS nos componentes (divergem do esp_components)

- **`Wifi/CaptivePortal`**: ganhou `PortalConnState` + `reportConnectionState()`
  e a rota `GET /status` com polling na página — o `/connect` segue apenas
  entregando credenciais via evento; quem conecta é o hospedeiro
  (`WifiSetupPortal`, no loop do modal — nunca no handler httpd).
- **`Wifi/WifiAP`**: `start()` promove para `WIFI_MODE_APSTA` quando o STA está
  ativo (portal aberto por um dispositivo conectado não derruba a estação) e o
  `stop()` preserva o STA em vez de parar o rádio inteiro.

## Fora do build (vendados para o futuro)

`BluetoothServer` (precisa `h2zero/esp-nimble-cpp`), `Drivers`, `IoUtility`
(precisa `espressif/button`), `SafeContainers`, `Supabase`, `UI` (precisa
`lvgl ^9`), `UserManaging`, `Time` (o CelerOS tem `TimeManager` próprio com
config em arquivo). A lista vive no `EXCLUDE_COMPONENTS` do `CMakeLists.txt`
raiz — para ativar um, remova-o da lista e garanta as dependências do
`idf_component.yml` dele.

## Requisitos no projeto raiz

```
CONFIG_COMPILER_CXX_EXCEPTIONS=y      # JsonModels/OtaManager usam try/catch
CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y     # OTA dev via HTTP na LAN — e ainda exige
                                      # /local/ota_allow_http.txt (guard no OtaManager)
```

TLS valida certificados desde a 1.3 (`ESP_TLS_INSECURE`/`SKIP_CERT_VERIFY`
saíram do `sdkconfig.defaults`): HttpClient e o OTA anexam
`esp_crt_bundle_attach`.

Partições com `nvs` (credential store) + `ota_0`/`ota_1` + `otadata` — ver
`partitions_16MB.csv` / `partitions_4MB.csv`. `config/projectConfig.h` e
`config/priorities.h` suprem includes que os componentes esperam.
