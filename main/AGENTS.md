# main/ - CelerOS firmware core

## OVERVIEW
The single ESP-IDF app component (~50 files, 13 subdirs; score 17). It holds the boot, UI framework, launcher, JS kernel, FS, network/web, OTA and USB/serial link. Sources are listed explicitly in `CMakeLists.txt` (no glob).

## STRUCTURE
```
main/
├── main.cpp          # app_main -> celerSetup() once, then celerLoop() forever
├── Boards/<board>/   # Board.cpp + BoardDisplay.h + BoardTraits.h (one board compiled in)
├── Display/          # Layout.h (240x320 virtual -> physical), Theme.h, Icon (PNG->RGB565+A4 cache), Backlight
├── UI/               # Kui.{h,cpp}: kui:: Canvas/Screen/Widget/TouchPump/Navigator; Keyboard
├── Launcher/         # LauncherUI (scans /local/apps/ + /sd/apps/), Screens.cpp (system screens)
├── Kernel/Core/      # CelerKernel: Duktape heap, runs app main.js; Kernel/TimeManager (NTP/tz)
├── Runtime/          # JS API surface: JSBindings.cpp core + Js*.cpp modules (own AGENTS.md)
├── FileSystem/       # static FileSystem:: LittleFS(/local) + SD(/sd), atomic writes, MD5
├── WebManager/       # WiFi boot/reconnect, httpd file manager + /update OTA upload (gzip pages), captive portal host
├── OTA/              # OtaManager: update.json v2 check + direct esp_https_ota flash (the only OTA path)
├── USBDevice/        # CelerShell, CelerLink (celerctl protocol), SerialLink (UART), LogSink
├── Compat/           # Arduino.h shim (millis/delay/pinMode...) over IDF - include path root
├── Settings/         # TouchCalibrator
└── Assets/           # SplashLogo.h (GENERATED: 64-color PNG drawn with drawPng)
```

## BOOT FLOW (main.cpp)
Board::init -> UI::init(w,h) -> FileSystem::init -> SerialLink::init -> USBDevice::init -> Backlight -> TimeManager -> WebManager::startAsync -> CelerKernel::init -> LauncherUI/TouchCalibrator -> kui::Navigator::begin + push(launcher).
Loop: `Navigator::tick()`, `WebManager::tick()` (deferred reboot after web OTA), `TimeManager::tick()`, `delay(5)`. Runs on the main task (32KB stack via sdkconfig).

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Add a source file | append to `CELEROS_SRCS` in `main/CMakeLists.txt` (it is not globbed) |
| Bump firmware version / API level | `main/CMakeLists.txt` `CELEROS_VERSION`, `CELEROS_API_LEVEL`, plus root `project(VERSION)` |
| New board | `Boards/<b>/` (3 files) + `elseif` in `main/CMakeLists.txt` + `boards/<b>/sdkconfig.defaults` + `updates/<channel>/update.json` |
| Touch gestures / tap vs swipe | `UI/Kui.cpp` TouchPump; injected touches (celerctl tap) go through TouchInjector |
| celerctl device side | `USBDevice/CelerLink.cpp` (opcodes), `SerialLink.cpp` (UART transport) |
| App discovery / launch | `Launcher/LauncherUI.cpp` (appDirs, `main.js`) |
| Web file-manager / firmware-upload page | edit `WebManager/filemanager.html` / `ota_upload.html`; the build gzips and embeds them (`main/CMakeLists.txt`), served with `Content-Encoding: gzip` |
| Turn a subsystem off for a board | `Kconfig.projbuild`: `CELEROS_WEB_SERVER`, `CELEROS_SD_CARD`, `CELEROS_JS_GPIO` (all default y); set `# CONFIG_... is not set` in `boards/<b>/sdkconfig.defaults` |

## CONVENTIONS
- Board HAL: `#include "Boards/Board.h"` / `"BoardDisplay.h"` resolve through the board dir on the PRIVATE include path. **Never add a board `#ifdef`** anywhere else. Put compile-time differences in `BoardTraits.h` (e.g. `largeUi`).
- All UI geometry is designed at 240x320: scale it with `UI::sx()/sy()` and pick fonts with `UI::font(n)`. Don't hardcode physical pixels.
- Modules are static classes/namespaces (`FileSystem::`, `OtaManager::`, `kui::Navigator::`). The only global instance is `Board::display()`.
- Logging goes through `celer_log_printf/println` (LogSink -> UART/CDC and the logcat buffer), not bare `printf`.
- Use `FileSystem::writeTextFile` for persistent state (tmp + rename; power-loss safe).
- Comments and log strings are Portuguese without accents. Match that style.
- Feature code behind a Kconfig flag uses `#if CONFIG_CELEROS_<X>` with a no-op `#else` stub for the public entry point, so callers never need `#if`.
- `CONFIG_CELEROS_USB_NATIVE` (Kconfig.projbuild, S3 only, default n) swaps SerialLink for TinyUSB dual CDC. **Keep it off on SmartDisplay 4848S040**: GPIO19/20 are the GT911 touch SDA and an RGB data line. Enabling it also needs `CONFIG_TINYUSB_CDC_COUNT=2`.

## ANTI-PATTERNS
- Hand-editing `Assets/SplashLogo.h`: regenerate it with `tools/make_splash.py`.
- Blocking WiFi or network calls in the UI tick. WebManager is async by design, and `scanNetworks`/`connect` block for seconds.
- Hot-path heap churn on the no-PSRAM board (CYD/ESP32): the frame sprite exists only when `Board::profile().hasPsram` is set. There the Kui canvas renders in two ping-pong bands (async DMA: never draw into a band still being pushed); screens skip off-band work with `Canvas::visible()`. The band buffer and icon caches are released while a JS app runs (`CelerKernel::runFile`).
- The `DEBUG:` heap prints in `celerSetup` are always on. They are known noise; don't copy the pattern.

## HOTSPOTS
`WebManager/WebManager.cpp` ~890, `Runtime/JSBindings.cpp` ~880 (core only), `UI/Kui.cpp` 820, `Launcher/Screens.cpp` 745, `FileSystem/FileSystem.cpp` 578.
