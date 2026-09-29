# PROJECT KNOWLEDGE BASE

**Generated:** 2026-09-28
**Commit:** 7e6645c
**Branch:** main

## OVERVIEW
CelerOS: an ESP-IDF (C++) firmware OS for ESP32 touch displays. It has a LovyanGFX UI, runs user apps in Duktape JS (ES5) from LittleFS or SD, and updates over the air from the hub `https://os.celer.tec.br`.

## STRUCTURE
```
CelerOS/
├── main/            # firmware core (boot, kernel, UI, OTA, FileSystem, board HAL)
│   └── Runtime/     # JS API surface: JSBindings core + Js*.cpp modules
├── components/      # everything here is built (Network, Http, System, Storage/NVS, Utility, ErrorCodes, duktape, LovyanGFX)
├── extras/esp_components/  # shared-lib code CelerOS does NOT build (dormant; outside IDF search)
├── data/            # LittleFS image contents: system JS apps + icons (flashed separately)
├── hub_apps/        # App Store apps (covered by data/AGENTS.md)
├── boards/<b>/      # per-board sdkconfig.defaults (smartdisplay, cyd)
├── tools/           # celerctl, flash_data.sh, ota_server, size_report, icon/splash generators
├── test/js_harness/ # only automated test (Node, stubbed device APIs)
├── updates/         # OTA channel dirs: smartdisplay_4848S040/, esp32/
├── Documentation/   # JS_API_Guide + App_Development_Guide (EN + .pt-BR)
├── include/, lib/   # stale PlatformIO placeholders, unused
└── partitions_16MB.csv / partitions_4MB.csv
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Boot flow, kernel, UI, OTA, board HAL | `main/AGENTS.md` | entry `main/main.cpp` |
| Add or change a JS API call / API level | `main/Runtime/AGENTS.md` | bump `CELEROS_API_LEVEL` |
| Component membership, reviving dormant ones | `components/AGENTS.md` | `components/README.md` (PT) = patch list |
| WiFi STA/AP, captive portal, credentials | `components/Network/AGENTS.md` | NetworkManager owns the radio; OTA flash is `main/OTA` |
| Firmware size / what costs flash | `python3 tools/size_report.py` | CYD OTA slot is the binding constraint |
| System apps, app.json rules | `data/AGENTS.md` | ES5 only |
| Device CLI, data flashing, local OTA server | `tools/AGENTS.md` | |
| JS API docs for app authors | `Documentation/JS_API_Guide*.md` | pt-BR is newer (885 vs 680 lines) |
| Board choice / sdkconfig | `boards/<b>/sdkconfig.defaults`, root `CMakeLists.txt` | |

## CODE MAP
| Symbol | Type | Location | Role |
|--------|------|----------|------|
| `app_main` | fn | `main/main.cpp` | calls `celerSetup()` once, then `celerLoop()` forever |
| `celerLoop` | fn | `main/main.cpp` | Navigator::tick, WebManager::tick, TimeManager::tick, delay 5 |
| `CelerKernel` | class | `main/Kernel/` | app/runtime kernel |
| `TimeManager`, `OtaManager`, `Kui` | classes | `main/Kernel`, `main/OTA`, `main/UI` | time/NTP, update check + flash, immediate-mode UI |
| `JSBindings::init` | fn | `main/Runtime/JSBindings.cpp` | registers the JS API from `kFnsN[]` tables |
| `NetworkManager::instance()` | singleton | `components/Network` | sole WiFi radio owner |
| `SystemInfo::instance()` | singleton | `components/System` | device info |
| `Singleton<T>` | CRTP template | `components/Utility/Singleton.h` | token-ctor singleton pattern |
| `Event<Args...>` | template | `components/Utility` | trigger holds mutex: handlers only set flags |
| `ErrorCode` | registry type | `components/ErrorCodes` | fallible return type (not esp_err_t) |

Boot order: Board::init -> UI::init -> FileSystem::init -> SerialLink -> USBDevice -> Backlight -> TimeManager -> WebManager::startAsync -> CelerKernel::init -> LauncherUI -> Navigator push launcher.

## CONVENTIONS
- Board selected via CMake cache `-DCELEROS_BOARD=smartdisplay|cyd` (default smartdisplay; any other value is FATAL_ERROR). smartdisplay = Guition ESP32-S3 4848S040 (16MB, PSRAM); cyd = ESP32-2432S028R (4MB).
- Version lives in TWO places: root `CMakeLists.txt` `project(CelerOS VERSION x)` and `main/CMakeLists.txt` `CELEROS_VERSION`. Keep them in sync.
- Dormant shared-lib code lives in `extras/esp_components/` (no `EXCLUDE_COMPONENTS` list). Subsystems can be compiled out per board via Kconfig: `CELEROS_WEB_SERVER`, `CELEROS_SD_CARD`, `CELEROS_JS_GPIO` (default y).
- Built with `-fno-exceptions` and an ES5-lean Duktape (`components/duktape/celeros_duk_config.yaml`) whose builtins live in ROM (base heap ~8KB); JS bindings are lightfuncs. A `std::string`/`new` that cannot grow **aborts** the device: on the no-PSRAM path use malloc/realloc (see `HttpClient::setBodySink`, `CelerKernel` source loader).
- CYD runs **unicore** with `CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY`: the free IRAM holds the app source during compile, the TLS buffers, and Duktape overflow past a 24KB DRAM reserve. Tasks must not be pinned to core 1 (use `portNUM_PROCESSORS - 1`).
- The littlefs partition has CSV subtype `spiffs` but is mounted as LittleFS at `/local`. The SD card is at `/sd`.
- JS apps use a 240x320 virtual coordinate space, scaled by `UI::sx/sy`.
- Comments, logs, and CLI output are in Portuguese (no accents). Headers use Doxygen.
- Generated but committed: `main/Assets/SplashLogo.h` (make_splash.py, PNG), `data/icons/*.png` (make_icons.py + icons.json), `components/duktape/duktape.c|h`, `duk_config.h` (Duktape configure.py). Web pages are gzipped at build time from `main/WebManager/*.html`.

## ANTI-PATTERNS (THIS PROJECT)
- NEVER call `Storage::initialize()`: it mounts SPIFFS over `/local`. Use `main/FileSystem`.
- NEVER add board `#ifdef`s outside `main/Boards/<b>/`.
- NEVER enable `CONFIG_CELEROS_USB_NATIVE` on SmartDisplay (GPIO19/20 conflict).
- NEVER use ES6+ syntax in JS apps (Duktape = ES5).
- Do not assume OTA updates apps: OTA ships firmware only.

## COMMANDS
```bash
# SmartDisplay (ESP32-S3)
idf.py -B build -DSDKCONFIG=build/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/smartdisplay/sdkconfig.defaults" \
  -DCELEROS_BOARD=smartdisplay set-target esp32s3
idf.py -B build build flash -p /dev/ttyUSB0 monitor
# CYD (ESP32): same, using build-cyd, boards/cyd/..., -DCELEROS_BOARD=cyd set-target esp32

tools/flash_data.sh [smartdisplay|cyd] [PORT]     # LittleFS data partition (needs IDF env)
python3 tools/celerctl.py devices|shell|push|pull|logcat|apps install ...   # over UART
python3 tools/celerctl.py ota push build/CelerOS.bin                        # OTA without esptool
python3 tools/ota_server.py --board smartdisplay  # local OTA server, port 10234
node test/js_harness/run.js                       # only automated test
python3 tools/size_report.py --baseline f.json    # image vs OTA slot, per-library deltas (needs IDF env)
```

## NOTES
- **SECURITY:** `extras/esp_components/config/config/supabase_config.h` commits the Supabase URL, the anon key, AND the **service_role key**. Rotate the key and move it out of git (it is still in history).
- `components/duktape` and LovyanGFX are vendored third-party code. Do not edit or document them.
- There is no firmware unit-test suite and no CI. Verify by building and running on hardware.
- TLS validates certificates (bundle: FULL on SmartDisplay, CMN on CYD). Hub/Google TLS needs `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`.
- `sdkconfig.defaults` changes only reach an existing build dir after deleting `build*/sdkconfig` (it is regenerated).
- OTA: device reads update.json v2 (`version`, `api_version`, `firmware_url`, `changelog`) from the hub, or from the URL in `/local/ota_url.txt` when set. The web UI `/update` also accepts uploads.
- Formerly KryonOS: leftover `kryonctl` pyc files and old author strings remain.
- `test/README`, `include/`, `lib/` are stale PlatformIO leftovers.
