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

## What's new in 1.5

Highlights of the October 2026 releases (firmware 1.5, JS API level 16):

* **Phone Link** — the watch talks to Android through [Gadgetbridge](https://gadgetbridge.org) over BLE (it poses as a Bangle.js): phone notifications land on the wrist with a full-screen alert, plus music info and controls, weather, incoming calls (accept/reject) and find-my-phone/watch. Pairing is a 6-digit code shown on the watch; the link is encrypted with MITM protection. JS API: `Phone.*` (API 15).
* **Watchface plugins (API 16)** — any installed app can add widget lines to the watch face by shipping a `watchface.js` ([wiki](https://github.com/maikramer/CelerOS/wiki/Watchface-Plugins)); a faulty plugin is quarantined, never takes the clock down. First one in the store: **Previsao** (weather forecast).
* **A real watch experience (API 15)** — a native notification center, persistent alarms with their own ringing screen, quick panels (flashlight, brightness, do-not-disturb, find phone) and six new watch apps: Timer, Weather, Music, Alarms, Phone and Activity.
* **Runtime APIs 12–16** — timers, per-app `Storage`, multi-sprites, binary file I/O, screen timeout / deep sleep / alarms with persistent time, `playTone`/`playWav`, async HTTP (`Net.beginGet/pollGet/cancelGet`) and permission consent for what apps declare.
* **Celer Link grows up** — pairing by 6-digit code plus a challenge-response key per bond; the old "no pairing in v1" disclaimer is retired.
* **CelerOS Flasher** — every [GitHub release](https://github.com/maikramer/CelerOS/releases) ships per-board packages (firmware + LittleFS image + flash plan) and a no-toolchain GUI flasher for Linux/Windows with esptool embedded.
* **App SDK** — `node tools/sdk/celer.js new|lint|types|test|emu|publish`: scaffold, editor typings, lint against the real firmware API, headless emulator with PNG snapshots and store publishing — zero npm dependencies.

## Screenshots

| Launcher | App Store | Settings |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-launcher.png" width="260" alt="Launcher"/> | <img src="Documentation/assets/imgs/celeros-appstore.png" width="260" alt="App Store"/> | <img src="Documentation/assets/imgs/celeros-settings.png" width="260" alt="Settings"/> |
| **Terminal (docked keyboard)** | **Snake** | |
| <img src="Documentation/assets/imgs/celeros-terminal.png" width="260" alt="Terminal"/> | <img src="Documentation/assets/imgs/celeros-snake.png" width="260" alt="Snake"/> | |

*Screenshots captured from the real framebuffer of a SmartDisplay 4" (firmware 1.4, API level 13) via `celerctl screencap`.*

### Waveshare AMOLED 2.06 watch

| Watchface (home screen) | Launcher |
| :---: | :---: |
| <img src="Documentation/assets/imgs/watch-watchface.png" width="220" alt="Watchface"/> | <img src="Documentation/assets/imgs/watch-launcher.png" width="220" alt="Watch launcher"/> |

*The watch boots into its watch face (wallpaper, steps, battery); swipe up opens the launcher. Since 1.5 the face also carries phone notifications, weather, music and plugin widgets. Same `celerctl screencap`, over the native USB.*

### CYD (2.8" 320x240, no PSRAM)

The CYD runs the same firmware and the same apps, but it is a much smaller
machine than the SmartDisplay (an ESP32 running on one core, ~320 KB of RAM
and no PSRAM, a 2.8" SPI panel and a resistive touch), so the experience is
noticeably simpler:

* **Stretched apps.** JS apps are designed for a 240x320 portrait canvas; on
  the 320x240 landscape glass they are scaled 1.33x wide and 0.75x tall, so
  text and shapes look squashed.
* **Apps can flicker.** There is no RAM for an off-screen frame, so JS apps
  draw straight to the panel (`System.isBuffered()` is `false`). The system
  UI (launcher, dialogs, app top bar) is composed in two small bands and does
  not flicker, but an app that clears and redraws the whole screen on every
  event will.
* **Slower.** A full-screen redraw is bound by the 40 MHz SPI bus (~31 ms);
  big apps take 1–2 s to compile when they open (Settings, App Store).
* **Tight memory.** An app gets ~220 KB of internal RAM (part of it slower
  IRAM); the practical ceiling is a `main.js` of ~60 KB, and big apps take a
  few seconds to open. Bigger apps are marked "Requer PSRAM" in the store.
* **Resistive touch.** It needs a firmer press and a calibration on first
  boot; targets are small (48 px icons, 20 px top bar). You can also drive
  the screen from the browser with the live mirror.
* **Board hardware still in JS.** The RGB LED on the back, the light sensor
  (auto-brightness) and the speaker connector all work through `System.led`
  / `lightLevel` / `beep`.
* **No SD card** for now (the slot shares the display bus).

## Features

* **JavaScript app runtime** — interactive apps written in ES5 run natively via Duktape (API level 16): canvas-style drawing, touch and coupled on-screen keyboard (`System.keypad*`), file system with per-app storage, timers, HTTP/JSON networking (blocking and async).
* **Immediate-mode UI** — adaptive layout (`main/Display/Layout.h`): the same apps scale from 240x320 up to 480x480, with PNG icons decoded to an RGB565+A4 cache.
* **Pre-installed apps in JS** — Settings, App Store, Installer, Help, Web Server, Terminal, Snake and the demos (HTTP Demo, Touch Test) live in the LittleFS partition; the firmware carries only the core (that shaved ~330 KB off the CYD image).
* **App Store & Installer** — browse and install apps from the [CelerOS Hub](https://os.celer.tec.br) over Wi-Fi, or sideload from the SD card.
* **Over-the-air updates** — firmware updates from the device (Settings → System Updates), from the browser (`/update` upload page), or via `celerctl ota push`. See [tools/README_OTA.md](tools/README_OTA.md).
* **Captive portal Wi-Fi setup** — no credentials stored? The device opens a `CelerOS-Setup-XXXX` access point; you configure Wi-Fi from your phone. Wi-Fi auto-reconnects on router drops.
* **Live screen from the browser** — `/screen` mirrors the display over Wi-Fi (RLE frames served row-block by row-block, so the device keeps running smoothly) and forwards your clicks as touches.
* **Board hardware in JS** — RGB LED (`System.led`), light sensor with auto-brightness (`System.lightLevel`), speaker (`System.beep`), relay lines on the SmartDisplay "Y" SKUs (`System.relay`), servos (`System.gpio.servo`) and robot-board hardware — battery, microphone, capacitive touch pad, NeoPixel (`System.battery`/`micLevel`/`touchPad`/`neopixel`). The watch adds an IMU with pedometer and raise-to-wake (`Sensors.*`), battery, an RTC that keeps time without network and audio volume (`System.setVolume`).
* **Watch-grade power management** — on the Waveshare watch the screen ladder dims after a few seconds, falls back to an always-on face with anti burn-in, then goes to deep sleep (EXT1 wake on the buttons); the device boots straight into its watch face.
* **Celer Link (BLE)** — Bluetooth LE link between nearby CelerOS devices (API 9): put a board on a robot, drive it from another CelerOS app (`CelerLink.scan/connect/send` — the Celer Remote hub app does exactly that). Since API 11 it pairs with a 6-digit code and a challenge-response key per bond.
* **Phone Link (Gadgetbridge)** — the watch pairs with Android over BLE posing as a Bangle.js ([Gadgetbridge](https://gadgetbridge.org) app): notifications with a full-screen alert, music control, weather, incoming calls and find-my-phone (`Phone.*`, API 15).
* **Notifications & alarms** — a system notification center (apps raise toasts/history via `System.notify`; on the watch an incoming notification wakes the screen with a full-screen alert) and persistent alarms with their own ringing screen (API 15).
* **Watchface plugins** — installed apps can extend the watch face with widget lines (`watchface.js` + `System.launchApp`, API 16); see the [wiki](https://github.com/maikramer/CelerOS/wiki/Watchface-Plugins).
* **Flash without a toolchain** — every [GitHub release](https://github.com/maikramer/CelerOS/releases) carries per-board packages (firmware + LittleFS image) and the **CelerOS Flasher**, a GUI flasher for Linux/Windows with esptool embedded.
* **App SDK** — `tools/sdk/celer.js`: scaffold, lint against the firmware's real API, editor typings, headless emulator (PNG snapshots), on-device dev loop and publishing, with zero npm dependencies.
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
| **SmartDisplay 4"** (Guition ESP32-S3-4848S040) | ESP32-S3-N16R8 | 4" IPS 480x480 RGB (ST7701) | Capacitive GT911 | 16 MB flash / 8 MB PSRAM, microSD (`/sd`), I2S speaker (NS4168 — `System.beep`); "Y" wall-switch SKUs with 1 or 3 relays (`System.relay`) |
| **CYD** (ESP32-2432S028R, "Cheap Yellow Display") | ESP32 | 2.8" ILI9341 240x320 SPI | Resistive XPT2046 | Classic witnessmenow variant (TFT on HSPI 14/13/12, touch on dedicated pins, backlight GPIO21); RGB LED, light sensor and speaker (GPIO26) in JS; SD slot off for now; touch calibration on first boot |
| **CYD-VSPI** (untested variant) | ESP32 | 2.8" ILI9341 240x320 SPI | Resistive XPT2046 | Legacy pinout (TFT on VSPI 18/23/19, shared touch bus, backlight GPIO22) kept for boards wired that way — **never tested on hardware**; build with `-DCELEROS_BOARD=cyd-vspi` |
| **Robot dog** (SpotPear ESP32-S3 AI Robot Dog, ZZPET `zzpet-s3`) | ESP32-S3R8 | 1.3" OLED SH1106 128x64 (face) | Capacitive pad (GPIO10) | 16 MB flash / 8 MB PSRAM (embedded), 4 leg servos, I²S mic + speaker, 2x WS2812, battery ADC; boots into the **Dog Face** app (expressive eyes, ramped gaits with a dead-man keepalive); driven from another CelerOS board over Celer Link BLE ([wiki](https://github.com/maikramer/CelerOS/wiki/Robot-Dog)) |
| **Waveshare AMOLED 2.06 watch** (ESP32-S3-Touch-AMOLED-2.06) | ESP32-S3R8 | 2.06" round AMOLED 410x502 QSPI (CO5300) | Capacitive FT3168 | 32 MB flash / 8 MB PSRAM (embedded), AXP2101 PMU, PCF85063 RTC + QMI8658 IMU (pedometer) + ES8311 audio codec on I²C, microSD; boots into the **Watchface** app; screen ladder with always-on display and deep sleep; `celerctl` over native USB ([wiki](https://github.com/maikramer/CelerOS/wiki/Waveshare-Watch)) |

Board definitions live in `boards/<board>/` (sdkconfig defaults) and
`main/Boards/<board>/` (pin map and display driver). Select the target with
`-DCELEROS_BOARD=<board>` (the ids in the table). The UI is
resolution-adaptive, so adding a panel is mostly a new board profile.

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

# Waveshare AMOLED 2.06 watch (ESP32-S3, native USB-Serial/JTAG)
idf.py -B build-watch -DSDKCONFIG=build-watch/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/waveshare-watch/sdkconfig.defaults" \
  -DCELEROS_BOARD=waveshare-watch set-target esp32s3
idf.py -B build-watch build flash -p /dev/ttyACM0 monitor

# SpotPear robot dog (ESP32-S3, native USB-Serial/JTAG)
idf.py -B build-dog -DSDKCONFIG=build-dog/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/spotpear-dog/sdkconfig.defaults" \
  -DCELEROS_BOARD=spotpear-dog set-target esp32s3
idf.py -B build-dog build flash -p /dev/ttyACM0 monitor

# LittleFS image from data/ (system apps + icons + demos)
tools/flash_data.sh smartdisplay /dev/ttyUSB0    # or: cyd|spotpear-dog|waveshare-watch <port>

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
* [JavaScript API Guide](Documentation/JS_API_Guide.md) ([em português](Documentation/JS_API_Guide.pt-BR.md)) — full reference of the JS runtime and native bindings (API level 16).
* [tools/README_USBTOOL.md](tools/README_USBTOOL.md) ([em português](tools/README_USBTOOL.pt-BR.md)) — `celerctl` command reference and the wire protocol.
* [tools/README_OTA.md](tools/README_OTA.md) ([em português](tools/README_OTA.pt-BR.md)) — OTA manifest scheme (`update.json`) and update channels.
* [components/README.md](components/README.md) — vendored helper components and local patches.

Desktop JS harness for the bundled apps (no hardware needed):

```bash
node test/js_harness/run.js
```

## Roadmap

* More boards (help with a bring-up is welcome — board profiles are small and self-contained; the [SpotPear robot dog](https://github.com/maikramer/CelerOS/wiki/Robot-Dog) and the [Waveshare AMOLED 2.06 watch](https://github.com/maikramer/CelerOS/wiki/Waveshare-Watch) are the latest two to land).
* More hardware APIs in the JS runtime (I2C/SPI sensors, ULP-assisted sensing during deep sleep).

## History & Credits

CelerOS started as a fork of [KryonOS](https://github.com/Haris16-code/KryonOS)
by Haris and has since diverged heavily: the Arduino/PlatformIO base was
replaced by ESP-IDF 6.1, the graphics stack moved to LovyanGFX, system apps
moved to JavaScript, and the tooling was rebuilt around `celerctl` and the
CelerOS Hub. Thanks, Haris, for the great starting point!

## License

CelerOS is licensed under the [GNU General Public License v3.0](./LICENSE).
