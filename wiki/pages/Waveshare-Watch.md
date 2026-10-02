# Waveshare AMOLED 2.06 watch

**English** | [Português (BR)](/maikramer/CelerOS/wiki/Watch-Waveshare)

CelerOS's first wearable: the **Waveshare ESP32-S3-Touch-AMOLED-2.06**
(board id `waveshare-amoled206`), a 2.06" AMOLED smartwatch with a rectangular, rounded-corner glass, on the
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
| Display | 2.06" AMOLED 410x502 (rectangular, rounded corners), **CO5300 over QSPI** @ 80 MHz (visible glass at column offset 22) |
| Touch | FT3168 capacitive on I²C |
| PMU | AXP2101 — display rails, battery voltage + fuel gauge (%), charge/USB state and the physical power key (PEK) |
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
| Power key | via **AXP2101 PEK** | polled by ScreenPower (short = wake screen, hold = deep sleep). The PMU IRQ is not on a GPIO, so it **cannot** wake from deep sleep — BOOT does |
| IMU INT1 | **21** | active low; wakes from deep sleep on motion when the `imu_wake` setting is on |

GPIO10 is **not** a button on this hardware: it reads LOW with a pull-up
(a trap inherited from the dog board family).

## The watch experience

* **Home:** the profile's `homeApp` is the bundled **Watchface**
  (`celeros.watchface`). BOOT at the launcher root opens it and the launcher
  returns to it after `home_idle_s` seconds idle (default 30). The face has
  three styles — digital, analog, minimal — switched with a long press, and
  shows battery %, steps against the goal, the next alarm, a running timer,
  unread notifications and the phone's weather.
* **Edge gestures** (`BoardProfile::watchGestures`): from the top edge pull
  down for **quick settings** (brightness, volume, WiFi, Do Not Disturb,
  raise-to-wake, always-on, flashlight, settings, phone link, find phone);
  from the bottom edge pull up for the **notification center** (tap to
  expand, swipe sideways to dismiss, clear all); from the left edge swipe
  right to leave an app. Opening a panel from the watch face returns to it.
* **Launcher** is a vertical list (`launcherList`) with the battery % and
  charging state in the status bar.
* **Screen power ladder** (ScreenPower): full → **dim** after 8 s →
  **AOD** at 15 s on the watch face (once-a-minute face with anti burn-in
  shifting, battery % and the latest unread notification; the panel stays
  awake at low brightness) → off (panel in SLPIN) → **deep sleep** on a
  long press of the power key. Raising the wrist wakes the screen at full
  brightness; a new notification lights an AOD "glance".
* **Power** (`Hardware/PowerPolicy`): `CONFIG_PM_ENABLE` + tickless idle —
  240 MHz while the screen is lit, DFS down to 40 MHz with automatic light
  sleep when dim/off (not while USB is plugged in, so `celerctl` keeps
  working). Idle WiFi turns off after `wifi_sleep_min` minutes of dark
  screen (default 10) and comes back when the screen lights up.
* **Alarms & timer** live in the system scheduler (`Kernel/Alarms`, NVS):
  up to 8 alarms with weekdays plus a countdown timer. They ring full
  screen even with the screen off or in AOD, snooze 5 min, and the watch
  wakes from deep sleep by timer for the next event.
* **Phone link** (`CONFIG_CELEROS_PHONE_LINK`): add the watch in
  **Gadgetbridge** (Android) as a **Bangle.js** and type the 6-digit code
  the watch shows. Phone notifications go to the notification center (with
  a glance + beep unless Do Not Disturb), the clock and time zone are set
  from the phone, and music control, weather and "find device" work both
  ways. The watch reports battery and steps.
* **Settings → Watch** exposes raise-to-wake, sensitivity, glance length,
  always-on, screen-off time, return-to-face time, step goal, WiFi sleep
  and motion wake.
* **Watch apps** (`boards/waveshare-watch/data/apps`): Alarms, Timer,
  Activity (steps, goal, last 7 days), Music and Weather.
  `boards/waveshare-watch/data-exclude.txt` keeps Terminal, HTTP Demo,
  Touch Test and Web Server off the watch.
* Time survives reboots without network (PCF85063 — written after NTP,
  phone sync or manual adjust). The pedometer rolls over at midnight and
  keeps 7 closed days.
* Watch JS surface: API 13 `Sensors.*`, `System.setVolume()/getVolume()`,
  `System.micLevel()`; API 15 `System.batteryInfo()`, `getInfo().inset/
  shape/board/screenW/screenH`, `Sensors.stepHistory()`, alarms/timer
  calls, `System.unreadNotifications()` and the `Phone` object.

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
* **[done, needs hardware validation]** API 15 watch experience: fuel
  gauge, edge gestures + quick settings + notification center, list
  launcher, persistent alarms/timer, PM + light sleep, Gadgetbridge phone
  link, watch apps and the Watch settings page.
* **[todo]** Measure current draw in AOD / screen off with PM on, and
  ULP-RISC-V motion monitoring during deep sleep (raise-to-wake without the
  main cores).

Restoring the stock Waveshare firmware is a plain esptool write over the
same USB — the CelerOS flash never touches the bootloader.
