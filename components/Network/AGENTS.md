# components/Network - radio owner, STA/AP, captive portal, credentials

## OVERVIEW
The whole WiFi stack of CelerOS in one component (no namespace). It merges the old `Connection` (NetworkManager, credential store) and `Wifi` (STA/AP/portal) components, which required each other in a CMake cycle. It earns its own file because it is a distinct domain and carries CelerOS-only patches.

## WHERE TO LOOK
| Task | File | Notes |
|------|------|-------|
| Boot connect, reconnect/roaming, scans | `NetworkManager.*` | Singleton, background task; owns the `WifiConnection` |
| STA state machine, scan/connect/retry, RSSI | `WifiConnection.*` | 9 `Event<>` members (`onStateChanged`, `onConnected`, `onAuthFailed`, `onRetrying`, ...); standalone class (no `BaseConnection`) |
| Saved networks | `NetworkCredentialStore.*`, `NetworkSelector.*` | Per-field NVS keys (`net<i>s/w/pr/...`); legacy pipe string read-only + migrated on load |
| Soft-AP for onboarding | `WifiAP.*` | **Patched**: APSTA coexistence |
| Portal page + DNS spoof + httpd | `CaptivePortal.*` | **Patched**: `PortalConnState`, `GET /status`; HTML template inline in the .cpp |
| Docs | `NETWORK_MANAGER.md`, `WIFI.md` | Portuguese; `WIFI.md` predates the CelerOS patches |

Consumers in `main/`: `WebManager` (NetworkManager + `WifiConnection` for timeouts), `WifiSetupPortal` (`CaptivePortal`, `WifiAP`). Firmware flashing is NOT here: `main/OTA/OtaManager` calls `esp_https_ota` directly.

## OWNERSHIP RULES
- `NetworkManager` owns the radio. Do not call `WifiConnection::connect` / `esp_wifi_*` mode changes directly from `main/`; go through `NetworkManager::instance()` (or `WebManager`).
- `NetworkManager.h` forward-declares `WifiConnection` so its header stays light; include `WifiConnection.h` only where needed.

## CELEROS PATCHES (diverge from esp_components - preserve on any resync)
- `CaptivePortal`: the `/connect` handler ONLY emits the credentials event. The host (`main/WebManager/WifiSetupPortal`, in its modal loop) connects, then calls `reportConnectionState(state, ip)`. The page polls `GET /status`, which reads the `volatile _connState` from the httpd task.
- `WifiAP::start()`: switches to `WIFI_MODE_APSTA` when STA is active, so opening the portal from a connected device keeps the station up. `stop()` in APSTA drops only the AP; it stops the whole radio only in pure AP mode.
- `WifiConnection`: no `BaseConnection`/`WifiClient` base, no legacy `onConnect`/`onDisconnect` events.
- `NetworkCredentialStore`: exception-free legacy parse (`strtol` + end-pointer check); the firmware builds with `-fno-exceptions`.

## ANTI-PATTERNS
- NEVER connect to WiFi inside an httpd handler or an `Event` handler. Handlers run under the event mutex or on the httpd task; they set state, and the owner loop acts.
- NEVER call `esp_wifi_set_mode(WIFI_MODE_AP)` unconditionally; it kills the STA link that the patches preserve.
- NEVER add `try`/`catch` or throwing parsers here (no exceptions in the build).
- SSID/PSK copies into `wifi_config_t` must be length-bounded (a truncation bug was fixed in commit 9de4793). Keep the `memcpy(..., std::min(size, sizeof(field)))` pattern in `WifiConnection.cpp` (no NUL needed: 32-byte SSID / 64-byte PSK may fill the field).
