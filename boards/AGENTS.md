# boards/ - per-board sdkconfig, data overlay and what makes each board different

## OVERVIEW
One directory per board holding its `sdkconfig.defaults`. Board code itself lives in `main/Boards/<board>/` (Board.cpp, BoardDisplay.h, BoardTraits.h — exactly one is compiled in); this tree is only configuration plus, on the watch, the factory-image overlay. The `-DCELEROS_BOARD=<b>` CMake cache variable selects the board (validated in `main/CMakeLists.txt`; anything else is FATAL_ERROR).

## THE FIVE BOARDS
| Board | Chip / flash / RAM | Display & touch | Partitions | Notes |
|-------|--------------------|-----------------|------------|-------|
| `smartdisplay` | ESP32-S3 N16R8, 16MB QIO, PSRAM octal | Guition 4848S040 480x480 capacitive GT911 | `partitions_16MB.csv` | Default board. **Never** `CELEROS_USB_NATIVE` here (GPIO19/20 = touch I2C + RGB lane). Optional relay header (`CELEROS_SMARTDISPLAY_RELAYS`) |
| `cyd` | ESP32 classic, 4MB, no PSRAM | CYD 2432S028R 320x240 resistive, landscape | `partitions_4MB.csv` | Unicore + IRAM-as-heap tricks; OTA slot (1.75MB) is THE size constraint of the project. USB via CH340 UART |
| `cyd-vspi` | ESP32 classic, 4MB, no PSRAM | CYD variant, display on VSPI — **UNTESTED** (see `main/Boards/cyd-vspi/BoardDisplay.h`) | `partitions_4MB.csv` | Same budget rules as `cyd`; exists to validate the variant in CI |
| `spotpear-dog` | ESP32-S3R8, 16MB DIO, PSRAM octal | SpotBear/ZZPET robot (no main display use) | `partitions_16MB.csv` | Derived from smartdisplay. Console+celerctl on the USB-Serial/JTAG (`CELEROS_LINK_ON_USJ`, the only USB connector). Home app = Dog Face |
| `waveshare-watch` | ESP32-S3R8, 32MB DIO, PSRAM octal | Waveshare AMOLED 2.06" 410x502 round-ish, FT3168 capacitive | `partitions_32MB.csv` | The smartwatch. Native USB dual CDC (`CELEROS_USB_NATIVE`, CDC_COUNT=2), NimBLE (Celer Link + Phone Link/Gadgetbridge), PM/light-sleep (`PowerPolicy`), RTC PCF85063 + AXP2101 + QMI8658 IMU, `screenInset` for the rounded glass |

## PER-BOARD FACTORY IMAGE
`tools/flash_data.sh <board>` stages `data/` and then:
1. **overlays** `boards/<board>/data/` on top (the watch adds Timer/Clima/Musica/Alarmes/Celular/Atividade), and
2. **excludes** paths listed in `boards/<board>/data-exclude.txt` (one per line, relative to `data/`; `#` comments) — the watch drops Terminal, HTTP Demo, Touch Test and Web Server (tight littlefs, keyboard-centric apps).

Partition size comes from the CSV picked above (single source of truth). New board with a data overlay: add the case to `flash_data.sh` and the overlay dir; the repo-wide app lint already globs `boards/*/data/apps` automatically.

## ADDING A BOARD (checklist)
1. `main/Boards/<b>/`: `Board.cpp` (BoardProfile), `BoardDisplay.h` (LovyanGFX panel + touch), `BoardTraits.h` — all board `#ifdef`s stay here, nowhere else.
2. `elseif(CELEROS_BOARD STREQUAL "<b>")` in `main/CMakeLists.txt`.
3. `boards/<b>/sdkconfig.defaults` (+ partition CSV if a new size).
4. `updates/<channel>/update.json` OTA channel and, if needed, a case in `tools/flash_data.sh` (port default + partition CSV).
5. Matrix entry in `.github/workflows/build.yml` (`firmware` job) — lint coverage is automatic via the `boards/*/data/apps` glob.
6. Fill `BoardProfile` runtime hooks (`readBatteryPct`, `imuAccel`, `screenSleep`, `watchGestures`, ...) instead of adding ifdefs elsewhere.

## HARDWARE NOTES FROM THE BENCH
- **S3 boards with octal PSRAM: GPIO33..37 are DQ4..7/DQS** — always denied in `gpioDeniedMask` (`0x3E00000000ULL`, note the zero count; an earlier off-by-one masked 29..33 instead).
- Serial ports on the bench: `/dev/ttyUSB0` SmartDisplay, `/dev/ttyUSB1` CYD, `/dev/ttyACM0` dog **or** watch (flash id disambiguates: 16MB vs 32MB).
- The watch is flashed over the air (`celerctl ota push`) after first esptool load; a CDC timeout during push is retriable, and an UNKNOWN state heals with a reboot.
- `sdkconfig.defaults` edits only reach an existing build dir after deleting `build*/sdkconfig`.
- `dependencies.lock` records the last-built target; don't commit its build churn.
