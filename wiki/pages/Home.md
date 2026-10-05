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

*Captured from the real framebuffer of a SmartDisplay 4" (firmware 1.5) via `celerctl screencap`.*

| Watchface on the Waveshare watch | Launcher on the watch |
| :---: | :---: |
| <img src="Documentation/assets/imgs/watch-watchface.png" width="200" alt="Watchface on the Waveshare watch"/> | <img src="Documentation/assets/imgs/watch-launcher.png" width="200" alt="Launcher on the watch"/> |

*Captured from the real framebuffer of the Waveshare watch via `celerctl screencap` (over the native USB).*

## What's new

Highlights of the October 2026 rounds (firmware 1.6–1.7, JS API level 24):

* **The dog talks (API 24)** — `AI.speak()` turns text into voice straight on the speaker: the OpenRouter TTS (Gemini voices, 30 natural voices with Portuguese out of the box) plays **live** as it downloads — nothing goes through RAM. Ask the robot dog anything and it answers out loud, in the language you spoke ([JS API](/maikramer/CelerOS/wiki/JS-API), [robot dog](/maikramer/CelerOS/wiki/Robot-Dog)).
* **The AI writes the choreography** — the dog's `dog_script` tool: the LLM writes its own ES5 JavaScript and runs it on the dog in a choreography sandbox (face, legs, LEDs and sound together; clamps, deadline and touch-to-abort). The hop gait walks at 660 ms/cycle, an asymmetric hop steers, and a spin drops it on its belly — all live-tunable from the Celer Remote's AFINAR panel over BLE, no cable ([robot dog](/maikramer/CelerOS/wiki/Robot-Dog)).
* **QOA audio** — a ~60-line fixed-point decoder (phoboslab's QOA, MIT) replaces the MP3 stack: real AI-rendered barks ride as 5x-smaller `.qoa` files, and app packages can ship `.qoa` sound assets.
* **celerctl over Wi-Fi — the Celer Debug Bridge** — shell, push/pull, logcat, the JS debugger, screencap and OTA over TCP, no cable; `devices` discovers bridges, `pair` once, `provision` sets a board up in one command ([tools](/maikramer/CelerOS/wiki/Tools), [celerctl](/maikramer/CelerOS/wiki/celerctl-USB)).
* **Profiling & persistent logs** — `celerctl top` (per-task CPU, heap, stack watermarks, app fps) and Linux-style `/local/log/kern.log` + `apps.log` with rotation: `dmesg`/`appslog` in the Terminal, `celerctl cat` from the host — they survive reboots.
* **Robustness round** — long blocking bindings renew the exec-timeout window (no RangeError after a slow TLS call), UI sounds moved off the UI thread, the captive portal's DNS survived malformed packets, and failure paths around big buffers no longer leak or abort.

Firmware 1.6 in the same October: the **UI toolkit** (`UI.*`, API 22), **multi-file apps** with `require()` (API 23) and multi-file App Store installs, **voice 2.0** (the LLM choreographs the dog), **Celer Link sealed** with AES-GCM frames (API 21) and the Celer Remote rewritten on the toolkit.

Earlier in the 1.5 cycle: on-device AI (`AI.chat`, API 18–20), voice and the **wake word "Hi Celer"** on-device (API 19–20), the JavaScript debugger over USB, watch deep sleep with a ULP-RISC-V sentinel, **Phone Link** over Gadgetbridge (`Phone.*`, API 15), **watchface plugins** (API 16), runtime APIs 12–17, the barebone devkit board, the **CelerOS Flasher** and the **App SDK**.

## Wiki map

| Page | What it covers |
|---|---|
| [Supported boards](/maikramer/CelerOS/wiki/Supported-Boards) | The SmartDisplay 4", the CYD family, the robot dog, the Waveshare watch and the barebone devkit — and how to add a new board |
| [Building and flashing](/maikramer/CelerOS/wiki/Building-and-Flashing) | ESP-IDF 6.1, per-board builds, the LittleFS data partition and the test harness |
| [Troubleshooting](/maikramer/CelerOS/wiki/Troubleshooting) | Common build, flash, touch, Wi-Fi and web problems — and their fixes |
| [Architecture](/maikramer/CelerOS/wiki/Architecture) | Boot flow, firmware layers, the JS runtime and code conventions |
| [Robot dog](/maikramer/CelerOS/wiki/Robot-Dog) | The SpotPear/ZZPET robot dog: full reverse-engineered pinout, bring-up firmware, the CelerOS board, the BLE "Celer Link" remote and voice commands ("hi celer, senta") |
| [Waveshare watch](/maikramer/CelerOS/wiki/Waveshare-Watch) | The AMOLED 2.06 smartwatch board: pinout ported from the Rust firmware, the Watchface home app, the AOD/deep-sleep screen ladder (with the ULP sentinel) and the `Sensors` API |
| [Barebone devkit](/maikramer/CelerOS/wiki/Barebone-Devkit) | The headless 4 MB ESP32 devkit: `System.button()` + LED, headless apps and the OTA channel |
| [System apps](/maikramer/CelerOS/wiki/System-Apps) | What lives in `data/`, the `app.json` rules and how apps reach the device |
| [Web interface](/maikramer/CelerOS/wiki/Web-Interface) | File manager, firmware upload and the live screen mirror in the browser |
| [Tools](/maikramer/CelerOS/wiki/Tools) | `celerctl`, the JS debugger, `celerhub`, the local OTA server and asset generators |
| [celerctl (USB)](/maikramer/CelerOS/wiki/celerctl-USB) | Command reference and the HostLink wire protocol |
| [OTA updates](/maikramer/CelerOS/wiki/OTA-Updates) | The `update.json` manifest, channels and web upload |
| [App development guide](/maikramer/CelerOS/wiki/App-Development-Guide) | How to package a JS app: `app.json`, folders, icons |
| [Watchface plugins](/maikramer/CelerOS/wiki/Watchface-Plugins) | How installed apps add widget lines to the watch face (`watchface.js`, API 16) |
| [JS API](/maikramer/CelerOS/wiki/JS-API) | Full reference of the runtime (`System`, `Net`, `FS`, `AI`, `Mic`, `WakeWord` globals) |
| [Contributing](/maikramer/CelerOS/wiki/Contributing) | Conventions, tests, CI and how to send a PR |

The pages above are the English originals; the **Português (BR)** section in
the sidebar has the Portuguese translations.

## Feature summary

* **JavaScript app runtime** — interactive ES5 apps run natively on Duktape
  (API level 20): canvas-style drawing, touch and the coupled on-screen
  keyboard, file system, HTTP/JSON networking.
* **On-device AI & voice** — `AI.chat()` talks to DeepSeek or OpenRouter
  from any app (async, TLS, function calling; keys stay outside the JS
  sandbox), `Mic.*` records 16 kHz WAV behind a `mic` permission, and the
  bundled **Chat IA** and **Qwen** apps are a chat client and a push-to-talk
  voice assistant. On the robot dog the wake word **"Hi Celer"** runs
  on-device and voice commands ("hi celer, senta") drive the legs
  (`WakeWord.*`, API 20 — [robot dog](/maikramer/CelerOS/wiki/Robot-Dog)).
* **JavaScript debugger** — `celerctl debug MyApp`: Duktape debugger over
  USB with breakpoints, stepping, eval, watches and an automatic pause on
  uncaught errors ([tools](/maikramer/CelerOS/wiki/Tools)).
* **Immediate-mode UI** — the same apps scale from 240x320 up to 480x480.
* **System apps in JS** — Settings, App Store, Installer, Help, Web Server,
  Terminal, Snake, Chat IA, Qwen and the demos live in the LittleFS
  partition; boards overlay their home apps (Watchface, Dog Face, Barebone).
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
  network and audio volume (`System.setVolume`); the headless devkit
  exposes the BOOT button (`System.button()`, API 17).
* **Watch-grade power management** — the screen ladder dims after a few
  seconds, falls back to an always-on face with anti burn-in, then goes to
  automatic deep sleep guarded by a ULP-RISC-V sentinel that watches the
  buttons, the battery and the cable (PWR wakes it in ~100 ms); the watch
  boots straight into its watch face.
* **Celer Link (BLE)** — a Bluetooth LE link between nearby CelerOS devices
  (API 9): a board on a robot runs `CelerLink.start()`, another one drives it
  with `scan()`/`connect()`/`send()` — see the
  [robot dog](/maikramer/CelerOS/wiki/Robot-Dog) page and the Celer Remote
  app in the hub. Since API 11 it pairs with a 6-digit code and a
  challenge-response key per bond. Runs on the SmartDisplay, the watch and
  the robot dog.
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
  `tools/sdk/celer.js` for scaffolding, linting, emulating (one PNG per
  clock milestone with pixel diffs) and publishing apps
  ([tools](/maikramer/CelerOS/wiki/Tools)).
* **Wi-Fi via captive portal** — a `CelerOS-Setup-XXXX` access point to
  configure it from your phone.
* **`celerctl`** — an adb-style USB companion: shell, push/pull, logcat
  (timestamps, filters, non-destructive dump), the JS debugger, screencap
  and in-place firmware updates. The last app error persists to
  `/local/lastcrash.txt` (`lasterror` in the shell).

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
