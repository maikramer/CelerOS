# celerctl — CelerOS USB/serial companion tool (adb-style)

**English** | [Português (BR)](README_USBTOOL.pt-BR.md)

`celerctl.py` talks to the firmware over the **HostLink** channel: a light
binary protocol (`[0x43 'C'][cmd][len u16 LE][payload]`) that runs on the
console UART — in practice, the CH340 that the PC sees as `/dev/ttyUSB*`
(or CDC1 of native USB, on boards with `CONFIG_CELEROS_USB_NATIVE`).

The opcodes live in `main/USBDevice/HostLink.h` and the tool parses that
file with a regex: there is a single source of truth for both sides.

## Install

```bash
pip install -r tools/requirements.txt   # pyserial (screencap needs Pillow)
```

## Commands

```bash
python3 tools/celerctl.py devices            # list connected boards
python3 tools/celerctl.py info               # version/board/heap/network/FS
python3 tools/celerctl.py shell              # interactive shell (help)
python3 tools/celerctl.py shell "ls /local"  # run and print
python3 tools/celerctl.py ls -l /local/apps
python3 tools/celerctl.py cat /local/wifi.txt
python3 tools/celerctl.py push app.zip /local/tmp_download/app.zip
python3 tools/celerctl.py pull /local/apps/HTTP\ Demo/app.json .
python3 tools/celerctl.py rm /local/old.txt
python3 tools/celerctl.py reboot
python3 tools/celerctl.py logcat             # live logs (Ctrl-C to exit)
python3 tools/celerctl.py ota push build/CelerOS.bin   # firmware without esptool
python3 tools/celerctl.py screencap shot.png # display capture -> PNG (RLE: ~10x faster)
python3 tools/celerctl.py coredump            # last crash dump (ELF) -> coredump.elf
python3 tools/celerctl.py shell "run Snake"  # open an app (folder, name or package)
python3 tools/celerctl.py tap 120 160        # inject a tap (navigate over USB)
python3 tools/celerctl.py swipe 120 400 120 40  # inject a drag (scroll)
python3 tools/celerctl.py apps list             # installed apps (local + sd)
python3 tools/celerctl.py apps install "data/apps/Web Server"  # install folder
python3 tools/celerctl.py apps install myapp --sd              # to the SD card
python3 tools/celerctl.py apps rm "Touch Test"                 # uninstall
```

### Iterating on the UI without touching the board

`tap`/`swipe` + `screencap` form an adb-like loop: the gesture is queued in
the firmware (opcode `KL_TOUCH`), executed by the UI's TouchPump as if it
were a physical finger — it works for the Navigator and for modals (QWERTY
keyboard) — and `screencap` reads the real framebuffer. Sample session:

```bash
python3 tools/celerctl.py tap 360 88 && python3 tools/celerctl.py screencap s.png
```

## Speeding up transfers (-b)

The channel starts at 115200 baud. With `-b 921600` the tool negotiates the
switch with the firmware and reopens the port faster:

```bash
python3 tools/celerctl.py -b 921600 push firmware.bin /sd/fw.bin
```

Measured on the SmartDisplay (CH340): push ~57 KB/s, pull ~190 KB/s, OTA ~60 KB/s.

## How the channel coexists with the console

The console UART multiplexes two modes (`main/USBDevice/SerialLink.cpp`):

- **console** — human interactive shell (echo, `celer> ` prompt) and
  visible ESP_LOG/Serial logs. Open minicom/monitor and use it.
- **link** — triggered by the arrival of a HELLO frame (`0x43 0x01 ...`);
  human typing never produces that sequence. Logs are suspended on the UART
  (keystrokes go into an 8 KB ring) and the session returns to console mode
  after ~8 s without frames, restoring the baud rate.

With `logcat`, the ring is drained (history since boot) and logs keep
flowing as live frames inside the tool itself.

## Note on native USB / Mass Storage

The ESP32-S3 has USB-OTG (GPIO19/20), but **on the SmartDisplay 4848S040
those pins are used by the board** (GPIO19 = GT911 touch SDA, GPIO20 = RGB
display G1 line) and the USB connector is CH340 only. That is why HostLink
runs on the UART and USB Mass Storage is not possible on these boards.

The native USB code (dual CDC via TinyUSB, `main/USBDevice/USBDevice.cpp`)
stays in the repository, dormant behind `CONFIG_CELEROS_USB_NATIVE`
(CelerOS menu), ready for boards whose GPIO19/20 are free — in that
scenario HostLink migrates to CDC1 with no protocol changes.
