# Supported boards

**English** | [Português (BR)](/maikramer/CelerOS/wiki/Placas-Suportadas)

| SmartDisplay 4" | CYD |
| :---: | :---: |
| <img src="Documentation/assets/imgs/celeros-home.jpg" width="300" alt="SmartDisplay running CelerOS"/> | <img src="Documentation/assets/imgs/CYD2432S028R.jpg" width="300" alt="CYD"/> |

| Board | SoC | Display | Touch | Notes |
|---|---|---|---|---|
| **SmartDisplay 4"** (Guition ESP32-S3-4848S040) | ESP32-S3-N16R8 | 4" IPS 480x480 RGB (ST7701) | Capacitive GT911 | 16 MB flash / 8 MB PSRAM, microSD, I2S speaker (NS4168); "Y" SKUs with relays |
| **CYD** (ESP32-2432S028R, "Cheap Yellow Display") | ESP32 | 2.8" ILI9341 320x240 SPI | Resistive XPT2046 | No PSRAM; CH340 serial; asks for touch calibration on first boot; simpler UI ([see below](#cyd-classic-esp32)) |
| **CYD-VSPI** (untested variant) | ESP32 | 2.8" ILI9341 320x240 SPI | Resistive XPT2046 | Legacy pinout (TFT on VSPI 18/23/19, shared touch bus, backlight GPIO22) kept for boards wired that way — **never tested on hardware**; build with `-DCELEROS_BOARD=cyd-vspi` |
| **Robot dog** (SpotBear/ZZPET `zzpet-s3`) | ESP32-S3R8 (8 MB embedded PSRAM) | 1.3" OLED SH1106 128x64 (face) | Capacitive pad (GPIO10) | 4 servos (legs), mic + speaker I²S, 2x WS2812, battery ADC; boots into the Dog Face app (profile `homeApp`); driven by the Celer Remote app over Celer Link BLE; build with `-DCELEROS_BOARD=spotpear-dog` — see [Robot dog](/maikramer/CelerOS/wiki/Robot-Dog) |

## Where a board is defined

* `boards/<board>/sdkconfig.defaults` — per-target sdkconfig defaults.
* `main/Boards/<board>/` — pin map, display driver and `BoardTraits.h`
  (compile-time differences, e.g. `largeUi`, `hasPsram`).
* Selection happens at build time with `-DCELEROS_BOARD=smartdisplay|cyd`
  (any other value is a fatal CMake error). See
  [Building and flashing](/maikramer/CelerOS/wiki/Building-and-Flashing).

The UI is resolution-adaptive — everything is drawn on a virtual 240x320
canvas and scaled — so adding a new panel is mostly creating a new board
profile.

## Adding a board

1. `main/Boards/<board>/` with the three files (`Board.cpp`,
   `BoardDisplay.h`, `BoardTraits.h`).
2. An `elseif` in [main/CMakeLists.txt](main/CMakeLists.txt) adding the
   board directory to the private include path.
3. `boards/<board>/sdkconfig.defaults`.
4. An `updates/<channel>/update.json` OTA channel.

## Per-board notes

### SmartDisplay 4" (ESP32-S3)

* Do not enable `CONFIG_CELEROS_USB_NATIVE` on this board: GPIO19/20 are
  the GT911's I2C and an RGB data line — enabling native USB conflicts with
  touch and video. `celerctl` talks to it over the UART (the USB connector
  is a CH340 on top of UART0, pins 43/44).
* RGB + PSRAM allow a full-frame sprite; the CYD doesn't have that luxury.

**Board peripherals used by the system** (from the manufacturer's material
for the "4.0inch_ESP32-4848S040"): microSD on SPI shared with the panel init
(CS=42, SCK=48, MISO=41, MOSI=47, mounted at `/sd`), speaker through a
Nsiway NS4168 digital amplifier over I2S (DOUT=GPIO40, BCLK=GPIO1,
LRC=GPIO2, no MCLK — `System.beep` plays a sine wave), and the "Y" SKUs
(86 wall-switch box with 1 or 3 relays): L1=GPIO40, L2=GPIO2, L3=GPIO1 —
the same pins as the speaker. Firmware built with
`CONFIG_CELEROS_SMARTDISPLAY_RELAYS=N` trades the speaker for N relays
controllable with `System.relay` (they start off at boot).

**Free GPIOs for apps** (`System.gpio`): IO35, IO36 and IO37 on the header
(IO0 is BOOT and the display's R4 line; IO43/44 are the console serial).

### CYD (classic ESP32)

| Launcher | App Store | Settings |
| :---: | :---: | :---: |
| <img src="Documentation/assets/imgs/cyd-launcher.png" width="240" alt="Launcher on the CYD"/> | <img src="Documentation/assets/imgs/cyd-appstore.png" width="240" alt="App Store on the CYD"/> | <img src="Documentation/assets/imgs/cyd-settings.png" width="240" alt="Settings on the CYD"/> |
| **Terminal** | **HTTP Demo** | **Snake** |
| <img src="Documentation/assets/imgs/cyd-terminal.png" width="240" alt="Terminal on the CYD"/> | <img src="Documentation/assets/imgs/cyd-httpdemo.png" width="240" alt="HTTP Demo on the CYD"/> | <img src="Documentation/assets/imgs/cyd-snake.png" width="240" alt="Snake on the CYD"/> |

**What to expect:**

The CYD runs the same firmware and the same apps, but it is a much smaller
machine than the SmartDisplay (an ESP32 with ~320 KB of RAM and no PSRAM, a
2.8" SPI panel and a resistive touch), so the experience is noticeably
simpler:

* **Stretched apps.** JS apps are designed for a 240x320 portrait canvas; on
  the 320x240 landscape glass they are scaled 1.33x wide and 0.75x tall —
  text and shapes look squashed.
* **Apps can flicker.** There is no RAM for an off-screen frame, so JS apps
  draw straight to the panel (`System.isBuffered()` is `false`). The system
  UI (launcher, dialogs, app top bar) is composed in two small bands and
  does not flicker, but an app that clears and redraws the whole screen on
  every event will.
* **Slower.** A full-screen redraw is bound by the 40 MHz SPI bus (~31 ms);
  big apps take 1–2 s to compile when they open (Settings, App Store).
* **Tight memory.** An app gets ~220 KB of internal RAM (part of it slower
  IRAM); the practical ceiling is a `main.js` of ~60 KB, and big apps take a
  few seconds to open. Bigger apps are marked "Requer PSRAM" in the store.
* **Resistive touch.** It needs a firmer press and a calibration on first
  boot; targets are small (48 px icons, 20 px top bar). You can also drive
  the screen from the browser with the
  [live screen mirror](/maikramer/CelerOS/wiki/Web-Interface).
* **No SD card** for now (the slot shares the display bus).

**Board peripherals used by the system:** RGB LED on the back
(`System.led`), light sensor next to the screen (`System.lightLevel` and
auto-brightness in Settings → Screen) and the speaker output on GPIO26
(`System.beep`).

**Under the hood (for firmware hackers):**

* 4 MB of flash: the image is tight — moving the system apps to JS on
  LittleFS (which saved ~330 KB) was decisive to make it fit.
* Resistive XPT2046 touch: calibration runs on the first boot.
* No PSRAM: avoid heap churn in hot paths — the full-frame sprite only
  exists when the board profile has PSRAM. The system UI is composed in two
  ping-pong bands; the icon cache and the bands are released when an app
  opens.
* Unicore with the free IRAM byte-accessible (compile source, TLS buffers
  and JS heap overflow area). Details and numbers in
  [Documentation/ENGINE_NOTES.md](Documentation/ENGINE_NOTES.md).
