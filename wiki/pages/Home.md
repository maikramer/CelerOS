# CelerOS

**English** | [Português (BR)](/maikramer/CelerOS/wiki/Home-(Português))

<p align="center">
  <img src="Documentation/assets/celeros_logo.png" alt="CelerOS" width="420"/>
</p>

CelerOS is a lightweight, open-source GUI operating system with a JavaScript
app runtime for ESP32 microcontrollers. It turns cheap display boards into a
small "smartwatch-grade" device: an immediate-mode UI on top of LovyanGFX, a
Duktape JS engine running sandboxed apps from flash or SD, an App Store with
over-the-air updates, and a USB companion tool (`celerctl`) for day-to-day
development.

| Boot splash | Launcher | Terminal (docked keyboard) |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-splash.png" width="240" alt="Boot splash"/> | <img src="Documentation/assets/imgs/celeros-launcher.png" width="240" alt="Launcher"/> | <img src="Documentation/assets/imgs/celeros-terminal.png" width="240" alt="Terminal"/> |

*Captured from the real framebuffer of a SmartDisplay 4" via `celerctl screencap`.*

| Launcher on the CYD | App Store on the CYD | Settings on the CYD |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/cyd-launcher.png" width="240" alt="Launcher on the CYD"/> | <img src="Documentation/assets/imgs/cyd-appstore.png" width="240" alt="App Store on the CYD"/> | <img src="Documentation/assets/imgs/cyd-settings.png" width="240" alt="Settings on the CYD"/> |

*CYD (320x240, no PSRAM): same core, simpler experience — see
[Supported boards](/maikramer/CelerOS/wiki/Supported-Boards#cyd-classic-esp32).*

## Wiki map

| Page | What it covers |
|---|---|
| [Supported boards](/maikramer/CelerOS/wiki/Supported-Boards) | The SmartDisplay 4", the CYD family, the robot dog and the Waveshare watch — and how to add a new board |
| [Building and flashing](/maikramer/CelerOS/wiki/Building-and-Flashing) | ESP-IDF 6.1, per-board builds, the LittleFS data partition and the test harness |
| [Troubleshooting](/maikramer/CelerOS/wiki/Troubleshooting) | Common build, flash, touch, Wi-Fi and web problems — and their fixes |
| [Architecture](/maikramer/CelerOS/wiki/Architecture) | Boot flow, firmware layers, the JS runtime and code conventions |
| [Robot dog](/maikramer/CelerOS/wiki/Robot-Dog) | The SpotPear/ZZPET robot dog: full reverse-engineered pinout, bring-up firmware and the CelerOS board + BLE "Celer Link" remote |
| [Waveshare watch](/maikramer/CelerOS/wiki/Waveshare-Watch) | The AMOLED 2.06 smartwatch board: pinout ported from the Rust firmware, the Watchface home app, the AOD/deep-sleep screen ladder and the `Sensors` API |
| [System apps](/maikramer/CelerOS/wiki/System-Apps) | What lives in `data/`, the `app.json` rules and how apps reach the device |
| [Web interface](/maikramer/CelerOS/wiki/Web-Interface) | File manager, firmware upload and the live screen mirror in the browser |
| [Tools](/maikramer/CelerOS/wiki/Tools) | `celerctl`, `celerhub`, the local OTA server and asset generators |
| [celerctl (USB)](/maikramer/CelerOS/wiki/celerctl-USB) | Command reference and the HostLink wire protocol |
| [OTA updates](/maikramer/CelerOS/wiki/OTA-Updates) | The `update.json` manifest, channels and web upload |
| [App development guide](/maikramer/CelerOS/wiki/App-Development-Guide) | How to package a JS app: `app.json`, folders, icons |
| [JS API](/maikramer/CelerOS/wiki/JS-API) | Full reference of the runtime (`System`, `Net`, `FS` globals) |
| [Contributing](/maikramer/CelerOS/wiki/Contributing) | Conventions, tests, CI and how to send a PR |

The pages above are the English originals; the **Português (BR)** section in
the sidebar has the Portuguese translations.

## Feature summary

* **JavaScript app runtime** — interactive ES5 apps run natively on Duktape:
  canvas-style drawing, touch and the coupled on-screen keyboard, file
  system, HTTP/JSON networking.
* **Immediate-mode UI** — the same apps scale from 240x320 up to 480x480.
* **System apps in JS** — Settings, App Store, Installer, Help, Web Server,
  Terminal, Snake and the demos live in the LittleFS partition.
* **App Store & Installer** — install apps from the
  [CelerOS Hub](https://os.celer.tec.br) over Wi-Fi or from the SD card.
* **Over-the-air updates** — from the device itself, from the browser or via
  `celerctl ota push`.
* **Live screen from the browser** — `/screen` mirrors the display over
  Wi-Fi and forwards your clicks as touches.
* **Board hardware in JS** — RGB LED, light sensor with auto-brightness,
  speaker (`System.beep`), relay lines and servos (`System.gpio.servo`) on
  supported boards; robot boards add battery, microphone, capacitive touch
  pad and NeoPixel (`System.battery`/`micLevel`/`touchPad`/`neopixel`);
  the watch adds an IMU with pedometer and raise-to-wake
  (`Sensors.accel/steps/temp`), a battery, an RTC that keeps time without
  network and audio volume (`System.setVolume`).
* **Watch-grade power management** — the screen ladder dims after a few
  seconds, falls back to an always-on face with anti burn-in, then goes to
  deep sleep (EXT1 wake); the watch boots straight into its watch face.
* **Celer Link (BLE)** — a Bluetooth LE link between nearby CelerOS devices
  (API 9): a board on a robot runs `CelerLink.start()`, another one drives it
  with `scan()`/`connect()`/`send()` — see the
  [robot dog](/maikramer/CelerOS/wiki/Robot-Dog) page and the Celer Remote
  app in the hub. No pairing in v1: toys and prototypes, nothing sensitive.
* **Wi-Fi via captive portal** — a `CelerOS-Setup-XXXX` access point to
  configure it from your phone.
* **`celerctl`** — an adb-style USB companion: shell, push/pull, logcat,
  screencap and in-place firmware updates.

## Links

* [Repository](https://github.com/maikramer/CelerOS) ·
  [README in English](https://github.com/maikramer/CelerOS/blob/main/README.md) ·
  [README em português](https://github.com/maikramer/CelerOS/blob/main/README.pt-BR.md)
* [Issues](https://github.com/maikramer/CelerOS/issues) — bugs and ideas
* History: CelerOS started as a fork of
  [KryonOS](https://github.com/Haris16-code/KryonOS), by Haris.

---

This wiki is generated automatically by CI from
[`wiki/`](https://github.com/maikramer/CelerOS/tree/main/wiki) in the
repository — edit there, not on the web.
