# Building and flashing

**English** | [Português (BR)](/maikramer/CelerOS/wiki/Compilando-e-Gravando)

CelerOS 1.2+ is plain **ESP-IDF 6.1** (no Arduino/PlatformIO layer).

## Prerequisites

```bash
git clone https://github.com/maikramer/CelerOS.git && cd CelerOS
git submodule update --init          # LovyanGFX
source ~/esp/v6.1/esp-idf/export.sh  # ESP-IDF v6.1 installed
```

Third-party components (ArduinoJson, esp_littlefs, nlohmann/json) are pulled
in by the ESP-IDF component manager; LovyanGFX is a submodule — clone with
`--recurse-submodules` or run `git submodule update --init`.

## Build and flash per board

The target is chosen with `-DCELEROS_BOARD=` (`smartdisplay` is the
default):

```bash
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
```

Each board uses its own build directory (`build/`, `build-cyd/`) with its
cached `sdkconfig` — that way you can switch targets without reconfiguring.

## The data partition (LittleFS)

`data/` (system apps, icons and demos) becomes the image of the `littlefs`
partition, mounted at `/local`:

```bash
tools/flash_data.sh smartdisplay /dev/ttyUSB0   # or: cyd <port>
```

The script needs the IDF environment exported (`IDF_PATH`) and uses the
`bin/mklittlefs.bin` + `parttool.py` from the IDF. The partition size is
read from `partitions_{16MB,4MB}.csv` — a single source of truth.

Tip: to push a single app without reflashing the whole partition,

```bash
python3 tools/celerctl.py apps install "data/apps/Settings"
```

## Testing without hardware (desktop harness)

The Node harness covers the pre-installed apps (Terminal, Snake, App Store
and the hub_apps) with the device APIs stubbed:

```bash
node test/js_harness/run.js
```

## Local OTA test server

To test the OTA flow without the hub:

```bash
python3 tools/ota_server.py --board smartdisplay
```

The device discovers it through `/local/ota_url.txt` (or by pointing
`ota_url` to `http://<your-ip>:10234`). Details in
[OTA updates](/maikramer/CelerOS/wiki/OTA-Updates).

## sdkconfig caveats

* The project requires `CONFIG_COMPILER_CXX_EXCEPTIONS=y` (JsonModels and
  OtaManager use try/catch) — already in the defaults.
* Hub/Google via Cloudflare need
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`.
* `CONFIG_CELEROS_USB_NATIVE` (S3, dual TinyUSB CDC) **never** on the
  SmartDisplay 4848S040 — GPIO conflict. See
  [Supported boards](/maikramer/CelerOS/wiki/Supported-Boards).
