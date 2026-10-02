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

| Launcher | App Store | Settings |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-launcher.png" width="240" alt="Launcher"/> | <img src="Documentation/assets/imgs/celeros-appstore.png" width="240" alt="App Store"/> | <img src="Documentation/assets/imgs/celeros-settings.png" width="240" alt="Settings"/> |

*Captured from the real framebuffer of a SmartDisplay 4" (firmware 1.4, API level 13) via `celerctl screencap`.*

| Watchface on the Waveshare watch | Launcher on the watch |
| :---: | :---: |
| <img src="Documentation/assets/imgs/watch-watchface.png" width="200" alt="Watchface on the Waveshare watch"/> | <img src="Documentation/assets/imgs/watch-launcher.png" width="200" alt="Launcher on the watch"/> |

*Captured from the real framebuffer of the Waveshare watch via `celerctl screencap` (over the native USB).*

## What's new in 1.5

Highlights of the October 2026 releases (firmware 1.5, JS API level 16):

* **Phone Link** — the watch talks to Android through [Gadgetbridge](https://gadgetbridge.org) over BLE (it poses as a Bangle.js): notifications with a full-screen alert, music info/controls, weather, incoming calls and find-my-phone. Pairing by a 6-digit code; encrypted link with MITM protection (`Phone.*`, API 15 — [watch page](/maikramer/CelerOS/wiki/Waveshare-Watch)).
* **Watchface plugins (API 16)** — installed apps add widget lines to the watch face by shipping a `watchface.js`; a faulty plugin is quarantined, never takes the clock down. First one in the store: **Previsao** (weather forecast) — [plugins page](/maikramer/CelerOS/wiki/Watchface-Plugins).
* **A real watch experience (API 15)** — native notification center, persistent alarms with their own ringing screen, quick panels (flashlight, brightness, do-not-disturb, find phone) and six new watch apps: Timer, Weather, Music, Alarms, Phone and Activity.
* **Runtime APIs 12–16** — timers, per-app `Storage`, multi-sprites, binary file I/O, screen timeout / deep sleep / alarms, `playTone`/`playWav`, async HTTP (`Net.beginGet/pollGet/cancelGet`) and permission consent for what apps declare.
* **Celer Link grows up** — pairing by 6-digit code plus a challenge-response key per bond; the "no pairing in v1" disclaimer is retired.
* **CelerOS Flasher** — per-board packages (firmware + LittleFS image + flash plan) and a no-toolchain GUI flasher for Linux/Windows on every [GitHub release](https://github.com/maikramer/CelerOS/releases) ([tools](/maikramer/CelerOS/wiki/Tools)).
* **App SDK** — `node tools/sdk/celer.js new|lint|types|test|emu|publish`: scaffold, editor typings, lint against the real firmware API, headless emulator with PNG snapshots and store publishing ([tools](/maikramer/CelerOS/wiki/Tools)).

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
| [Watchface plugins](/maikramer/CelerOS/wiki/Watchface-Plugins) | How installed apps add widget lines to the watch face (`watchface.js`, API 16) |
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
  app in the hub. Since API 11 it pairs with a 6-digit code and a
  challenge-response key per bond.
* **Phone Link (Gadgetbridge)** — the watch pairs with Android over BLE
  posing as a Bangle.js: notifications with a full-screen alert, music
  control, weather, incoming calls and find-my-phone (`Phone.*`, API 15 —
  [watch page](/maikramer/CelerOS/wiki/Waveshare-Watch)).
* **Notifications & alarms** — a system notification center (`System.notify`;
  on the watch an incoming notification wakes the screen) and persistent
  alarms with their own ringing screen (API 15).
* **Watchface plugins** — installed apps extend the watch face with widget
  lines (`watchface.js` + `System.launchApp`, API 16 —
  [plugins page](/maikramer/CelerOS/wiki/Watchface-Plugins)).
* **CelerOS Flasher & App SDK** — no-toolchain GUI flashing from
  [GitHub releases](https://github.com/maikramer/CelerOS/releases), and
  `tools/sdk/celer.js` for scaffolding, linting, emulating and publishing
  apps ([tools](/maikramer/CelerOS/wiki/Tools)).
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
