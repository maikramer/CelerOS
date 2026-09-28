# components/Wifi - STA/AP radio, captive portal, OTA flashing

## OVERVIEW
ESP-IDF WiFi wrappers (no namespace). Earned its own file (score ~10, distinct domain): 25 files, ~4.6k LOC, and the only vendored component carrying CelerOS-only patches.

## WHERE TO LOOK
| Task | File | Notes |
|------|------|-------|
| STA state machine, scan/connect/retry, RSSI | `WifiConnection.*` (739 LOC) | Extends `Connection/BaseConnection`; 10 `Event<>` members (`onStateChanged`, `onConnected`, `onAuthFailed`, `onRetrying`, ...) |
| Soft-AP for onboarding | `WifiAP.*` | **Patched**: APSTA coexistence |
| Portal page + DNS spoof + httpd | `CaptivePortal.*` (791 LOC) | **Patched**: `PortalConnState`, `GET /status`; HTML is inline in the .cpp |
| Flash a firmware image | `WifiOta.*` | `Event<int> onProgress`; used by `main/OTA/OtaManager.cpp` |
| Update-server client (check/download) | `OtaManager.*` | Upstream singleton; `main/` uses its OWN `main/OTA/OtaManager`, not this one |
| Docs | `WIFI.md`, `OTA_USAGE.md` | Portuguese; predate the CelerOS patches |

Consumers in `main/`: `WebManager` (`WifiConnection` via NetworkManager), `WifiSetupPortal` (`CaptivePortal`), `OTA/OtaManager` (`WifiOta`).
Compiled but unused by CelerOS: `WifiServer`, `WifiClient`, `Telnet`, `WifiTelnet`, `WirelessDevice`.

## OWNERSHIP RULES
- `Connection/NetworkManager` owns the radio. Do not call `WifiConnection::connect` / `esp_wifi_*` mode changes directly from `main/`; go through `NetworkManager::instance()`.
- Circular dep: `Wifi` REQUIRES `Connection` (for `BaseConnection`), `Connection` PRIV_REQUIRES `Wifi`. Keep it that way; moving either to public REQUIRES creates a CMake cycle.

## CELEROS PATCHES (diverge from esp_components - preserve on any resync)
- `CaptivePortal`: the `/connect` handler ONLY emits the credentials event. The host (`main/WebManager/WifiSetupPortal`, in its modal loop) connects, then calls `reportConnectionState(state, ip)`. The page polls `GET /status`, which reads the `volatile _connState` from the httpd task.
- `WifiAP::start()`: switches to `WIFI_MODE_APSTA` when STA is active, so opening the portal from a connected device keeps the station up. `stop()` in APSTA drops only the AP; it stops the whole radio only in pure AP mode.

## ANTI-PATTERNS
- NEVER connect to WiFi inside an httpd handler or an `Event` handler. Handlers run under the event mutex or on the httpd task; they set state, and the owner loop acts.
- NEVER call `esp_wifi_set_mode(WIFI_MODE_AP)` unconditionally; it kills the STA link that the patches preserve.
- Do not trust `WIFI.md` for portal flow or AP stop semantics; the code and this file are current.
- SSID/PSK copies into `wifi_config_t` must be length-bounded (a truncation bug was fixed in commit 9de4793). Keep the `memcpy(..., std::min(size, sizeof(field)))` pattern in `WifiConnection.cpp` (no NUL needed: 32-byte SSID / 64-byte PSK may fill the field).
