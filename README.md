<p align="center">
  <img src="Documentation/assets/celeros_logo.png" alt="CelerOS" width="480"/>
</p>

# CelerOS

**English** | [Português (BR)](README.pt-BR.md)

CelerOS is an open-source, lightweight GUI operating system and JavaScript
app runtime for ESP32 microcontrollers. It turns cheap display boards into a
small "smartwatch-grade" device: an immediate-mode UI on top of LovyanGFX, a
Duktape JS engine running sandboxed apps from flash or SD, an App Store with
over-the-air updates, and a USB companion tool (`celerctl`) for day-to-day
development.

## Screenshots

| Boot splash | Launcher | Terminal (docked keyboard) |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-splash.png" width="260" alt="Boot splash"/> | <img src="Documentation/assets/imgs/celeros-launcher.png" width="260" alt="Launcher"/> | <img src="Documentation/assets/imgs/celeros-terminal.png" width="260" alt="Terminal"/> |
| **Settings → About** | **Snake** | |
| <img src="Documentation/assets/imgs/celeros-about.png" width="260" alt="About"/> | <img src="Documentation/assets/imgs/celeros-snake.png" width="260" alt="Snake"/> | *CYD screenshots coming soon* |

*Screenshots captured from the real framebuffer of a SmartDisplay 4" via `celerctl screencap`.*

## Features

* **JavaScript app runtime** — interactive apps written in ES5 run natively via Duktape (API level 5): canvas-style drawing, touch and coupled on-screen keyboard (`System.keypad*`), file system, HTTP/JSON networking.
* **Immediate-mode UI** — adaptive layout (`main/Display/Layout.h`): the same apps scale from 240x320 up to 480x480, with PNG icons decoded to an RGB565+A4 cache.
* **Pre-installed apps in JS** — Settings, App Store, Installer, Help, Web Server, Terminal, Calculator and Snake live in the LittleFS partition; the firmware carries only the core (that shaved ~330 KB off the CYD image).
* **App Store & Installer** — browse and install apps from the [CelerOS Hub](https://os.celer.tec.br) over Wi-Fi, or sideload from the SD card.
* **Over-the-air updates** — firmware updates from the device (Settings → System Updates), from the browser (`/update` upload page), or via `celerctl ota push`. See [tools/README_OTA.md](tools/README_OTA.md).
* **Captive portal Wi-Fi setup** — no credentials stored? The device opens a `CelerOS-Setup-XXXX` access point; you configure Wi-Fi from your phone. Wi-Fi auto-reconnects on router drops.
* **`celerctl` USB companion** — adb-style tool over the serial link: interactive shell, file push/pull, live logcat, in-place firmware update and screencap. See [tools/README_USBTOOL.md](tools/README_USBTOOL.md).
* **Settings PIN lock** — optional numeric PIN (salted SHA-256, handled natively) protects Settings, with a 60 s unlock session.
* **Web file manager with authentication** — files, text editor and firmware upload over the browser, guarded by HTTP Basic Auth (password shown in the Web Server app or `celerctl info`).
* **File management** — file explorer and text editor over LittleFS and SD card.

## Supported Boards

| SmartDisplay 4" | CYD |
| :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-home.jpg" width="300" alt="SmartDisplay running CelerOS"/> | <img src="Documentation/assets/imgs/CYD2432S028R.jpg" width="300" alt="CYD"/> |

| Board | SoC | Display | Touch | Notes |
|---|---|---|---|---|
| **SmartDisplay 4"** (Guition ESP32-S3-4848S040) | ESP32-S3-N16R8 | 4" IPS 480x480 RGB (ST7701) | Capacitive GT911 | 16 MB flash / 8 MB PSRAM, native USB option |
| **CYD** (ESP32-2432S028R, "Cheap Yellow Display") | ESP32 | 2.8" ILI9341 240x320 SPI | Resistive XPT2046 | CH340 serial; needs touch calibration on first boot |

Board definitions live in `boards/<board>/` (sdkconfig defaults) and
`main/Boards/<board>/` (pin map and display driver). Select the target with
`-DCELEROS_BOARD=smartdisplay|cyd`. The UI is resolution-adaptive, so adding
a panel is mostly a new board profile.

## Building & Flashing

CelerOS 1.2+ is plain **ESP-IDF 6.1** (no Arduino/PlatformIO layer).

```bash
git clone https://github.com/maikramer/CelerOS.git && cd CelerOS
git submodule update --init          # LovyanGFX
source ~/esp/v6.1/esp-idf/export.sh  # ESP-IDF v6.1

# SmartDisplay 4" (ESP32-S3)
idf.py -B build -DSDKCONFIG=build/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/smartdisplay/sdkconfig.defaults" \
  -DCELEROS_BOARD=smartdisplay set-target esp32s3
idf.py -B build build flash -p /dev/ttyUSB0 monitor

# CYD (classic ESP32)
idf.py -B build-cyd -DSDKCONFIG=build-cyd/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/cyd/sdkconfig.defaults" \
  -DCELEROS_BOARD=cyd set-target esp32
idf.py -B build-cyd build flash -p /dev/ttyUSB0 monitor

# LittleFS image from data/ (system apps + icons + demos)
tools/flash_data.sh smartdisplay /dev/ttyUSB0    # or: cyd <port>

# Local OTA test server
python3 tools/ota_server.py --board smartdisplay
```

Third-party components (ArduinoJson, esp_littlefs, nlohmann/json) are pulled
in by the ESP-IDF component manager; LovyanGFX is a git submodule — clone with
`--recurse-submodules` or run `git submodule update --init`.

### Reproducibility & CI

* **Toolchain pinned**: ESP-IDF **v6.1** (see `.tool-versions`); LovyanGFX is
  pinned to an exact commit by the submodule.
* **CI** ([`.github/workflows/build.yml`](.github/workflows/build.yml)): the
  JS harness and the host C++ tests run on every push/PR, then both board
  firmwares build in the official ESP-IDF container and are uploaded as
  artifacts. Host tests cover the pure logic extracted to `main/Utils`
  (`g++ -std=c++17 test/cpp/run_tests.cpp`).

## JS Apps & Documentation

* [Wiki](https://github.com/maikramer/CelerOS/wiki) — architecture, build, boards, tools and guides (CI-generated from [`wiki/`](wiki/) in the repo).
* [App Development Guide](Documentation/App_Development_Guide.md) ([em português](Documentation/App_Development_Guide.pt-BR.md)) — how to package a JS app (`app.json`, folder layout, icons).
* [JavaScript API Guide](Documentation/JS_API_Guide.md) ([em português](Documentation/JS_API_Guide.pt-BR.md)) — full reference of the JS runtime and native bindings (API level 5).
* [tools/README_USBTOOL.md](tools/README_USBTOOL.md) ([em português](tools/README_USBTOOL.pt-BR.md)) — `celerctl` command reference and the wire protocol.
* [tools/README_OTA.md](tools/README_OTA.md) ([em português](tools/README_OTA.pt-BR.md)) — OTA manifest scheme (`update.json`) and update channels.
* [components/README.md](components/README.md) — vendored helper components and local patches.

Desktop JS harness for the bundled apps (no hardware needed):

```bash
node test/js_harness/run.js
```

## Roadmap

* More boards (help with a bring-up is welcome — board profiles are small and self-contained).
* More hardware APIs in the JS runtime (Bluetooth, I2C/SPI sensors, deeper power management).

## History & Credits

CelerOS started as a fork of [KryonOS](https://github.com/Haris16-code/KryonOS)
by Haris and has since diverged heavily: the Arduino/PlatformIO base was
replaced by ESP-IDF 6.1, the graphics stack moved to LovyanGFX, system apps
moved to JavaScript, and the tooling was rebuilt around `celerctl` and the
CelerOS Hub. Thanks, Haris, for the great starting point!

## License

CelerOS is licensed under the [GNU General Public License v3.0](./LICENSE).
