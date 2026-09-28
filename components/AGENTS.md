# components/ - ESP-IDF component layer

## OVERVIEW
Sibling ESP-IDF components vendored from the shared `esp_components` lib (satisfaction-hub), plus third-party `duktape` and `LovyanGFX`. Adoption map + CelerOS patch list: `README.md` (Portuguese, authoritative).

## BUILD MEMBERSHIP
| Status | Components |
|--------|------------|
| Required by `main/` | `Wifi`, `Connection`, `Http`, `System`, `LovyanGFX`, `duktape` |
| Transitive only | `Utility`, `ErrorCodes`, `JsonModels`, `Storage`, `config` |
| Excluded (root `EXCLUDE_COMPONENTS`) | `BluetoothServer`, `Drivers`, `IoUtility`, `SafeContainers`, `Supabase`, `UI`, `UserManaging`, `Time` |

- To enable an excluded one: remove it from the root list AND satisfy its `idf_component.yml` registry deps (`UI` -> `lvgl ^9`, `BluetoothServer` -> `esp-nimble-cpp`, `IoUtility` -> `espressif/button`).
- Excluded code is dormant. Chunk reports/docs that call `AuthManager`/`SupabaseClient`/`SafeVector` "used by main" are wrong - nothing in `main/` includes them.
- Name collisions: `main/` has its own `Kernel/TimeManager`, `OTA/OtaManager`, `UI/Kui` + `UI/Keyboard`. Those are NOT `components/Time`, `Wifi/OtaManager`, `components/UI` (LVGL, `ui::` namespace).

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| WiFi radio owner (boot connect, roaming, scans) | `Connection/NetworkManager.*` | Singleton; background task; `NETWORK_MANAGER.md` |
| Saved WiFi credentials | `Connection/NetworkCredentialStore.*` | NVS; one-shot import of legacy `wifi.txt` |
| STA/AP/captive portal/OTA flash | `Wifi/` | Own AGENTS.md |
| HTTP GET/POST/download w/ progress | `Http/HttpClient.*` | Used by OTA and JS `Net.*` bindings |
| Chip/heap/PSRAM/MAC/reset info | `System/SystemInfo.*` | Backs JS `System.getInfo` |
| Pub/sub, singleton base, timeouts | `Utility/Event.h`, `Singleton.h`, `Timeout.h` | Header templates |
| Error codes | `ErrorCodes/ErrorCode.*`, `CommonErrorCodes.h` | One `<Category>ErrorCodes.{h,cpp}` pair per domain |
| Pin/priority macros components expect | `config/projectConfig.h`, `config/priorities.h` | Shims so vendored includes resolve |
| JS engine | `duktape/` | Vendored single-file amalgamation (`duktape.c`, `duk_config.h`); never edit, never document |
| Display driver | `LovyanGFX/` | Vendored upstream library; board config lives in `main/Boards/<board>/` |

## CONVENTIONS
- Registry deps only in `idf_component.yml` (e.g. `johboh/nlohmann-json`); sibling deps go in CMake `REQUIRES`, never `path:` entries.
- Singletons: `class X : public Singleton<X>`, ctor takes `token`; access via `X::instance()` (`Utility/Singleton.h`, Meyers static).
- Events: `Event<Args...>` with `addHandler` / `removeHandler` / `trigger` (not subscribe/publish). Callers expose members like `onStateChanged`, `onProgress`.
- Fallible ops return `ErrorCode` (registry: `ErrorCode::define/get`, construct-on-first-use map), not raw `esp_err_t`.
- Each component ships a Portuguese/English `<NAME>.md` API guide beside its sources; headers carry Doxygen blocks.
- Flat layout: sources and headers in the component root (`INCLUDE_DIRS "."`); only `UI` uses `include/` + `src/`.
- Project sdkconfig must keep `CONFIG_COMPILER_CXX_EXCEPTIONS=y` (JsonModels/OtaManager use try/catch).

## ANTI-PATTERNS
- NEVER do real work inside an `Event` handler: `trigger()` holds the event mutex for the whole handler loop. Handlers set flags; the owner's loop acts.
- NEVER call `Storage::initialize()`: it mounts SPIFFS on the `spiffs` partition that CelerOS formats as LittleFS (`/local`). Only the `NVS` class is safe; the OS filesystem is `main/FileSystem`.
- Do not "sync" a component from upstream `esp_components` blindly: `Wifi/CaptivePortal` and `Wifi/WifiAP` carry CelerOS-only patches (see `Wifi/AGENTS.md`).
- Do not add a `#include` of an excluded component from `main/` without un-excluding it; the build fails at link or include time.
- `config/config/supabase_config.h` contains a hardcoded Supabase URL, anon key AND service_role key. Never copy it into logs, JS, or new code; treat it as a leaked secret to rotate.

## NOTES
- Known TODOs: `CONFIG_ESP_TLS_INSECURE` / skip-cert-verify still on for dev; `Supabase/SupabaseAuth.cpp` lacks a real connectivity check.
- No unit tests in any component. Verify by building and running on hardware.
- Unused in CelerOS but compiled with `Wifi`: `WifiServer`, `WifiClient`, `Telnet`, `WifiTelnet`.
