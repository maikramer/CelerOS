# KryonOS
KryonOS is an **open-source**, lightweight, high-performance **GUI** Operating System and JavaScript App Runtime designed specifically for the ESP32 microcontroller. It provides a complete desktop-like experience on embedded devices, featuring an integrated JS engine (Duktape) for executing standalone JavaScript applications, double-buffered graphics for smooth 2D/3D rendering, an App Store, file management, and direct hardware API access.

| ESP32 + ILI9341 2.8" (Touch) | Multiple Devices (Lab Overview) | M5Stack Cardputer | CYD (Cheap Yellow Display) | LilyGO T-HMI |
| :---: | :---: | :---: | :---: | :---: |
| **Stable** 🟢 | *Experimental* 🧪 | *Experimental* 🧪 | *Experimental* 🧪 | *Experimental* 🧪 |
| <img src="Documentation/assets/imgs/kryonos-home.jpg" width="220" alt="ESP32 ILI9341 Stable"/> | <img src="Documentation/assets/imgs/Devices.jpg" width="220" alt="Hardware Overview"/> | <img src="Documentation/assets/imgs/Cardputer-V1.1.jpg" width="220" alt="Cardputer"/> | <img src="Documentation/assets/imgs/CYD2432S028R.jpg" width="220" alt="CYD"/> | <img src="Documentation/assets/imgs/Lilygo-T-HMI.jpg" width="220" alt="T-HMI"/> |

## Features

* **JavaScript App Runtime:** Execute interactive JS apps natively on the ESP32 using the optimized Duktape engine.
* **Rich UI & Graphics:** Built-in graphics library with double-buffering support for smooth, tear-free 2D and 3D rendering.
* **App Store & Installer:** Browse, download, and install JavaScript apps and updates dynamically over Wi-Fi.
* **Over-the-Air Updates:** Firmware updates straight from the device (Settings → System Updates → INSTALL) or via browser upload on the web file manager (`/update`). See [tools/README_OTA.md](tools/README_OTA.md).
* **Captive Portal Wi-Fi Setup:** No credentials? The device opens a `KryonOS-Setup-XXXX` access point and you configure Wi-Fi from your phone's browser.
* **Wi-Fi Auto-Reconnect:** If the router drops, KryonOS reconnects by itself (no reboot needed).
* **JS Networking (`Net.*`):** HTTP GET/POST/JSON from JavaScript apps (API level 2) — see the [JS API Guide](Documentation/JS_API_Guide.md).
* **Settings PIN Lock:** Optional numeric PIN protects the Settings area (MD5-hashed, 60 s unlock session).
* **Brightness Control:** Adjustable backlight with persistent level on the SmartDisplay 4" board.
* **File Management:** Fully functional file explorer and text editor utilizing the SD Card for storage.
* **Hardware API:** Easy-to-use JavaScript APIs for controlling GPIO, reading touch input, accessing the SD card, and reading sensors.
* **Multitasking Feel:** Launch, suspend, and switch between utility apps, games, and hardware monitors.

## Hardware Needed
Want your board supported in KryonOS? [Request board support here](https://github.com/Haris16-code/KryonOS/issues/new?template=board-support.yml).
* **ESP32 Development Boards** (Supported: ESP32 WROOM-32, ESP32-S2, ESP32-S3, ESP32-C3)
* **ILI9341 2.8" TFT Display** (SPI interface with XPT2046 Touch Controller)
* **MicroSD Card Module** (SPI interface)
* **Breadboard & Jumper Wires**

## Pin Connections

KryonOS requires an ILI9341 2.8 Inch Touch display with and an SD card module. To achieve the best performance and avoid bus collisions, KryonOS uses **split SPI buses**.

*   **VSPI:** Used exclusively for the TFT Display and Touch controller.
*   **HSPI:** Used exclusively for the SD Card Module.

> [!NOTE]
> **ESP32 Marauder Compatibility**
> Out-of-the-box, the display and touch pinouts in KryonOS perfectly match the **ESP32 Marauder (v4, v6, and v6.1)** hardware!

## Default Pin Configuration

| ILI9341 2.8 Inch Touch Display Pins | ILI9341 Display Pin Labels | ESP32 Pin |
| :--- | :--- | :--- |
| **1** | VCC | 3.3V |
| **2** | GND | GND |
| **3** | CS | D17 (TXD 2) |
| **4** | RESET | D5 |
| **5** | DC | D16 (RXD 2) |
| **6** | SDI (MOSI) | D23 |
| **7** | SCK | D18 |
| **8** | LED | D32 |
| **9** | SDO (MISO) | D19 |
| **10** | T_CLK | D18 |
| **11** | T_CS | D21 |
| **12** | T_DIN | D23 |
| **13** | T_DO | D19 |
| **14** | T_IRQ | X (Not Connected) |

### SD Card Module (HSPI)
| SD Card Module | ESP32 Pin | Notes |
| :--- | :--- | :--- |
| **MOSI** | GPIO 13 | SD SPI MOSI |
| **MISO** | GPIO 26 | SD SPI MISO |
| **SCK / CLK** | GPIO 14 | SD SPI Clock |
| **CS** | GPIO 15 | SD Card Chip Select |

## Changing Pins

If your specific hardware setup uses different pins, you will need to recompile the OS:

1.  **To change Display/Touch pins:** Edit `main/Display/Display.h` (the `KryonGFX` class of each board has the pins concentrated in the constructors).
2.  **To change SD Card pins:** Open `src/FileSystem/FileSystem.cpp` and modify the `sdSPI.begin()` and `SD.begin()` lines.

---

## Supported Boards

The UI is resolution-adaptive (`src/Display/Layout.h`): screens scale from the physical
display size, so apps and system UI work on any panel (fonts, list rows, footer and
the app "X" button are derived from `tft.width()/height()`).

### 1. Classic ESP32 + ILI9341 (env `esp32doit-devkit-v1`)
ESP32 DevKit + 2.8" SPI 240x320 ILI9341 + resistive touch XPT2046 (see pin tables above).

### 2. SmartDisplay ESP32-S3 4.0" — Guition ESP32-S3-4848S040 (env `smartdisplay_4848S040`)
ESP32-S3-N16R8 (16 MB flash QIO + 8 MB PSRAM OPI), 4" IPS 480x480 **ST7701** RGB panel,
capacitive touch **GT911**, backlight PWM. Graphics via **LovyanGFX** (TFT_eSPI does not
support the S3 RGB peripheral); board config lives in `src/Display/Display.h`.

| Function | Pins |
|---|---|
| RGB data (D0–D15) | B: 4,5,6,7,15 · G: 8,20,3,46,9,10 · R: 11,12,13,14,0 |
| HSYNC / VSYNC / DE / PCLK | 16 / 17 / 18 / 21 (12 MHz) |
| Panel init SPI (3-wire) | CS=39, SCK=48, MOSI=47 |
| Backlight | GPIO 38 (PWM) |
| Touch GT911 (I2C) | SDA=19, SCL=45, addr 0x5D |
| SD card (SPI, shared bus) | MOSI=47, SCK=48, MISO=41, CS=42 |

Capacitive touch needs no calibration — the resistive Touch Calibrator is hidden on
this target. Build and flash with:

```bash
pio run -e smartdisplay_4848S040 -t upload      # firmware
pio run -e smartdisplay_4848S040 -t uploadfs    # LittleFS (demo app em data/)
```

---

## How to Flash

> KryonOS 1.1+ e baseado em **ESP-IDF 6.1 puro** (sem Arduino/PlatformIO).

### Option 1: Using Precompiled Binaries
You can download the latest precompiled firmware `.bin` files directly from our [Releases Page](https://github.com/Haris16-code/KryonOS/releases).

### Option 2: Build it Yourself (ESP-IDF)

Placas: **SmartDisplay 4"** (Guition ESP32-S3-4848S040) e **Cheap Yellow Display** (ESP32-2432S028R).

```bash
git clone https://github.com/Haris16-code/KryonOS.git && cd KryonOS
source ~/esp/v6.1/esp-idf/export.sh          # ESP-IDF v6.1 instalado

# SmartDisplay (ESP32-S3)
idf.py -B build -DSDKCONFIG=build/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/smartdisplay/sdkconfig.defaults" \
  set-target esp32s3
idf.py -B build build flash -p /dev/ttyUSB0 monitor

# Cheap Yellow Display (ESP32 classico)
idf.py -B build-cyd -DSDKCONFIG=build-cyd/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/cyd/sdkconfig.defaults" \
  -DKRYONOS_BOARD=cyd set-target esp32
idf.py -B build-cyd build

# Gravar o LittleFS de data/ (icones + app demo) na particao "littlefs"
tools/flash_data.sh smartdisplay /dev/ttyUSB0    # ou: cyd <porta>

# Servidor OTA local de testes (firma na LAN)
python tools/ota_server.py --board smartdisplay
```

Componentes de terceiros resolvem sozinhos pelo component manager
(ArduinoJson, esp_littlefs, nlohmann); o LovyanGFX e um git submodule
(`git clone --recurse-submodules`).

## USB/Serial Debugging (`kryonctl`)

An adb-style companion tool talks to the firmware over the USB serial link
(the CH340 port on supported boards): interactive shell, file push/pull,
live logcat, in-place firmware update and screen capture — no esptool
needed for day-to-day development:

```bash
pip install -r tools/requirements.txt
python3 tools/kryonctl.py devices
python3 tools/kryonctl.py shell            # interactive shell on the device
python3 tools/kryonctl.py -b 921600 push app.zip /local/tmp_download/app.zip
python3 tools/kryonctl.py logcat           # live logs (also replays boot)
python3 tools/kryonctl.py ota push build/KryonOS.bin
python3 tools/kryonctl.py screencap tela.png
```

See [tools/README_USBTOOL.md](./tools/README_USBTOOL.md) for the full
command reference and how the link coexists with the serial console.

## Documentation & Community
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/Haris16-code/KryonOS)
* [App Development Guide](./Documentation/App_Development_Guide.md) - Learn how to build and structure JavaScript applications for KryonOS.
* [JavaScript API Guide](./Documentation/JS_API_Guide.md) - Learn how to access system hardware using KryonOS's JS API.
* [KryonOS Wiki](https://github.com/Haris16-code/KryonOS/wiki) - Official wiki for detailed guides, tutorials, and system architecture.
* [Discussions](https://github.com/Haris16-code/KryonOS/discussions) - Join the community, ask questions, and share your ideas.

## Roadmap

* **Expanded Board Support:** Future updates will bring support for a wider variety of microcontrollers and ESP32 variants.
* **More Hardware APIs:** Continuous expansion of the JavaScript API to expose more low-level hardware features (e.g., Bluetooth, I2C, SPI sensors, advanced PWM, and deep sleep).

## Support KryonOS

Help the KryonOS team purchase new development boards and hardware for testing, development, and expanding support for more devices.

<a href="https://harislab.lemonsqueezy.com/checkout/buy/9b37ee2c-e26a-4626-990f-f18834916276?logo=0"> <img src="https://img.shields.io/badge/❤️%20Support%20KryonOS-FF6B35?style=for-the-badge" alt="Support KryonOS"> </a>
## License

KryonOS is licensed under the [GNU General Public License v3.0](./LICENSE).
