# Waveshare AMOLED 2.06 watch

**English** | [Português (BR)](/maikramer/CelerOS/wiki/Watch-Waveshare)

CelerOS's first wearable: the **Waveshare ESP32-S3-Touch-AMOLED-2.06**
(board id `waveshare-amoled206`), a round 2.06" AMOLED smartwatch on the
ESP32-S3R8 — 32 MB of flash, 8 MB of embedded PSRAM, a hardware PMU and a
proper sensor pack behind the glass. The pin map and the panel init sequences
were ported from the Rust firmware **`waveshare-watch-rs`** (a standalone
firmware for the same watch), which kept this board from needing the
JTAG-register archaeology the [robot dog](/maikramer/CelerOS/wiki/Robot-Dog)
required.

| Watchface (home screen) | Launcher |
| :---: | :---: |
| <img src="Documentation/assets/imgs/watch-watchface.png" width="230" alt="Watchface on the watch"/> | <img src="Documentation/assets/imgs/watch-launcher.png" width="230" alt="Launcher on the watch"/> |

*Captured with `celerctl screencap` over the native USB: the watch face
(wallpaper, steps, battery) is the home screen; swipe up opens the launcher.*

## Hardware

| Item | Value |
|---|---|
| SoC | **ESP32-S3R8** — dual-core LX7, 8 MB **embedded** octal PSRAM |
| Flash | 32 MB, DIO @ 80 MHz — plenty of room, firmware builds with `-O2` |
| USB | Native USB-Serial/JTAG: console, esptool **and** `celerctl` (dual CDC) over the one USB connector |
| Display | 2.06" round AMOLED 410x502, **CO5300 over QSPI** @ 80 MHz (visible glass at column offset 22) |
| Touch | FT3168 capacitive on I²C |
| PMU | AXP2101 — display rails, battery gauge and the physical power key (PEK) |
| Sensors | QMI8658 IMU (pedometer, raise-to-wake), PCF85063 RTC, ES8311 audio codec — all on the same I²C0 bus |

### Pinout

| Function | GPIO | Notes |
|---|---|---|
| AMOLED — QSPI SDIO0..3 | **4 / 5 / 6 / 7** | pixels via cmd 0x32 (quad lane), registers via 0x02 (single) |
| AMOLED — SCLK / CS / RST | **11 / 12 / 8** | RST needs a long pulse: 200 ms low (short pulses don't boot the panel) |
| I²C0 bus | **SDA 15 / SCL 14** | touch FT3168 (0x38), PMU AXP2101 (0x34), RTC PCF85063, IMU QMI8658, codec ES8311 |
| Touch FT3168 — RST / INT | **9 / 38** | needs reg 0xA5 = monitor mode at init |
| Audio ES8311 — I²S0 | **DOUT 40, BCLK 41, LRC 45, MCLK 16** | 16 kHz, MCLK 4.096 MHz; PA enable on **46** (high only while beeping) |
| Microphone — ES8311 ADC | **ASDOUT 42** | I²S1 RX slave on the codec's clocks |
| microSD — SPI3 | **CS 17, SCK 2, MOSI 1, MISO 3** | mounted at `/sd` |
| BOOT button | **0** | short = home/exit app, hold = screenshot |
| Power key | via **AXP2101 PEK** | polled by ScreenPower; wakes from deep sleep |

GPIO10 is **not** a button on this hardware: it reads LOW with a pull-up
(a trap inherited from the dog board family).

## The watch experience

* The profile's `homeApp` is the bundled **Watchface** app
  (`celeros.watchface`): the watch face **is** the home screen; swipe up
  opens the launcher. `screenInset` keeps the clock and the X buttons away
  from the rounded corners.
* **Screen power ladder** (ScreenPower): full → **dim** after 8 s →
  **AOD** at 15 s (a low-frequency always-on face with anti burn-in
  shifting, panel in SLPIN) → off → **deep sleep**, waking by EXT1 on the
  buttons. Raising the wrist lights an AOD "glance" via the IMU.
* Time survives reboots without network (PCF85063 — written after NTP sync
  or manual adjust), the pedometer's daily count is persisted to NVS before
  sleep, and the battery comes from the AXP2101 in `System.battery()`.
* Watch-specific JS surface: `Sensors.accel()/steps()/temp()` and
  `System.setVolume()/getVolume()` (both API level 13), plus
  `System.micLevel()` through the ES8311.

## Known traps

1. The **AXP2101 must be configured first**: without DCDC1 + ALDO1 at
   3.3 V the AMOLED stays black even fully initialized (`Board::init()`
   does it before the panel init).
2. The CO5300 wants a **long reset** (200 ms low) and a post-init patch
   (`0x58=0x00` contrast off, `0x36=0x00` MADCTL portrait) that the stock
   LovyanGFX init list (from the T-Watch-Ultra) doesn't have.
3. The panel only accepts **even-aligned write windows** and has no
   readback — the display runs on a full LovyanGFX framebuffer in PSRAM
   (~402 KB), which in exchange gives `readRect` back (screenshots, web
   mirror).
4. LovyanGFX's own I²C layer is broken on IDF 6.1: the FT3168 is driven by
   the IDF `i2c_master` driver in `main/Display/Touch_FT3168_IDF.h` (same
   pattern as the GT911).
5. Brightness is the AMOLED's WRDISBV (DCS 0x51) — there is no PWM
   backlight; Settings → Screen talks to the panel driver.
6. `sdkconfig.defaults` files **don't accept inline comments** — the native
   USB needs `CONFIG_TINYUSB_CDC_COUNT=2` (shell + `celerctl`), and a
   comment on that line silently breaks the USB init.
7. Log frames (`celerctl logcat`) go out on the **active session channel**
   — before the HostLink rework they were hard-wired to UART0 (which has
   no connector on this board), so logcat was silently dead here. With
   proto 2 the live log frames also carry the CRC32 like every other
   frame.

## Status in CelerOS

* **[done]** Board port `main/Boards/waveshare-watch/`, validated on
  hardware: display + touch, RTC, battery, IMU (pedometer +
  raise-to-wake), the screen ladder with AOD and deep sleep, beep + mic
  through the ES8311, volume, Celer Link BLE and `celerctl` over the
  native USB.
* **[in progress]** ULP-RISC-V motion monitoring during deep sleep
  (raise-to-wake without the main cores).

Restoring the stock Waveshare firmware is a plain esptool write over the
same USB — the CelerOS flash never touches the bootloader.
