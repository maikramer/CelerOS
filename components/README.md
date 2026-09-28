# Componentes do CelerOS

Tudo que esta em `components/` entra no build. Codigo da lib compartilhada
`esp_components` (satisfaction-hub) que o CelerOS nao compila fica em
`extras/esp_components/` (fora do alcance do IDF, preservado para reuso).
Os `idf_component.yml` declaram apenas dependencias de registry; dependencias
entre irmaos ficam nos `REQUIRES` dos CMakeLists, sem `path:`.

## Mapa (quem usa o que no main/)

| Componente | Usado por | Como |
|---|---|---|
| `Network` | `WebManager`, `WifiSetupPortal` | **NetworkManager** e o dono do radio (boot, reconnect/roaming pela background task, scans); **WifiConnection** (STA, via NM); **WifiAP** + **CaptivePortal** (portal de configuracao); **NetworkCredentialStore** (credenciais em NVS, import one-shot do `wifi.txt` legado). Junta os antigos `Connection` + `Wifi`, que se exigiam em ciclo |
| `Http` | `OTA`, `JSBindings Net.*` | `HttpClient` para GET/POST/downloads com progresso |
| `System` | `JSBindings System.getInfo` | `SystemInfo` (chip/heap/PSRAM/uptime/reset reason/MAC) |
| `Storage` | `Network` (credential store) | So a classe `NVS` (wrapper tipado sobre `nvs_flash`) |
| `Utility` | (transitivo) | `Event<>` pub/sub (handlers rodam sob mutex: so tocam flags), `Singleton<>` |
| `ErrorCodes` | (transitivo) | `ErrorCode`/`CommonErrorCodes` |
| `duktape` | `Kernel`, `Runtime` | Duktape 2.7.0 regerado com `celeros_duk_config.yaml` (ES5 enxuto) |
| `LovyanGFX` | tudo que desenha | submodulo upstream |

O flash de OTA nao depende de componente: `main/OTA/OtaManager` chama o
`esp_https_ota` direto (HTTPS com bundle de CAs; HTTP so com o opt-in
`/local/ota_allow_http.txt`).

## Patches CelerOS (divergem do esp_components)

- **`Network/CaptivePortal`**: `PortalConnState` + `reportConnectionState()` e
  a rota `GET /status` com polling na pagina — o `/connect` so entrega as
  credenciais via evento; quem conecta e o hospedeiro (`WifiSetupPortal`, no
  loop do modal — nunca no handler httpd).
- **`Network/WifiAP`**: `start()` promove para `WIFI_MODE_APSTA` quando o STA
  esta ativo e o `stop()` preserva o STA em vez de parar o radio inteiro.
- **`Network/WifiConnection`**: nao herda mais `BaseConnection` (sem
  `WifiClient`/`sendRawData`/eventos legados `onConnect`/`onDisconnect`).
- **`Network/NetworkCredentialStore`**: leitura do formato legado sem
  excecoes (`strtol` validado).
- **`Utility`**: sem `nlohmann/json` (`ListJsonKeys` removido).
- **`Storage`**: so `NVS.cpp` compila (o resto esta em `extras/`).

## extras/esp_components (fora do build)

`BluetoothServer`, `Drivers`, `IoUtility`, `SafeContainers`, `Supabase`, `UI`
(LVGL), `UserManaging`, `Time`, `JsonModels`, `config`, e as sobras de
`Connection` (BaseConnection/Commander), `Wifi` (WifiClient/WifiServer/Telnet/
WifiOta/OtaManager do upstream/WirelessDevice) e `Storage`
(Storage::initialize/SdCard/Flash). Para usar algum: mover de volta para
`components/`, garantir as dependencias do `idf_component.yml` dele e, se
precisar de try/catch, reabilitar `CONFIG_COMPILER_CXX_EXCEPTIONS`.

**Atencao:** `extras/esp_components/config/config/supabase_config.h` tem a
service_role key do Supabase commitada — trate como vazada e rotacione.

## Requisitos no projeto raiz

```
# CONFIG_COMPILER_CXX_EXCEPTIONS is not set   # nada no build usa try/catch
CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y             # OTA dev via HTTP na LAN — e ainda exige
                                              # /local/ota_allow_http.txt (guard no OtaManager)
```

Particoes com `nvs` (credential store) + `ota_0`/`ota_1` + `otadata` — ver
`partitions_16MB.csv` / `partitions_4MB.csv`.
