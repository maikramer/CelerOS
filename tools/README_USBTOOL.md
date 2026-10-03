# celerctl — CelerOS USB/serial companion tool (adb-style)

**English** | [Português (BR)](README_USBTOOL.pt-BR.md)

`celerctl.py` talks to the firmware over the **HostLink** channel: a light
binary protocol that runs on the console UART — in practice, the CH340 that
the PC sees as `/dev/ttyUSB*` — on CDC1 of native USB (`CONFIG_CELEROS_USB_NATIVE`,
e.g. the Waveshare watch) or on the S3 USB-Serial/JTAG itself
(`CONFIG_CELEROS_LINK_ON_USJ`, e.g. the SpotPear dog). One tool, every board.

## Wire protocol (proto 1 and 2)

```
proto 1: [0x43 'C'][cmd u8][len u16 LE][payload]
proto 2: [0x43 'C'][cmd u8][len u16 LE][crc32 u32][payload]
```

The session format is negotiated in the HELLO: the tool sends payload
`"CELERCTL2"` and a proto-2 firmware answers `...|proto 2|chunk W|win K`,
activating CRC32 on every frame plus the sliding window for chunks. Old
firmware answers `proto 1` and the session stays exactly as before — new
tool works with old firmware and vice versa (`--proto 1` forces the legacy
path on purpose). CRC32 is the classic 0xEDB88320 (same value as Python's
`zlib.crc32`).

Proto 2 extras on the wire:

- `WRITE_CHUNK`/`OTA_CHUNK` payload is `[seq u16][data]`; the ACK is
  `[next seq u16][total applied u32]` — a re-sent chunk whose ACK was lost
  is **not** written twice, and the final CRC/size check covers the whole
  file/image end-to-end (a corrupted OTA never reaches the boot slot).
- `READ` responses are prefixed with the offset, so pulls are pipelined.
- `LS` accepts a cursor (huge directories paginate), `DELETE` accepts a
  recursive flag, `COREDUMP` accepts a keep flag.

The opcodes live in `main/USBDevice/HostLink.h` (framing itself in
`HostFrame.h`, unit-tested on the host) and the tool parses that file with
a regex: single source of truth for both sides. `test/test_celerctl.py`
exercises the tool against a simulated device (lost chunk, corrupted byte,
legacy mode).

## Install

```bash
pip install -r tools/requirements.txt   # pyserial (screencap needs Pillow)
```

## Commands

```bash
python3 tools/celerctl.py devices            # list connected boards (+ USB serial)
python3 tools/celerctl.py info               # version/board/heap/network/FS
python3 tools/celerctl.py shell              # interactive shell (help)
python3 tools/celerctl.py shell "ls /local"  # run and print
python3 tools/celerctl.py ls -l /local/apps
python3 tools/celerctl.py cat /local/wifi.txt
python3 tools/celerctl.py push app.zip /local/tmp_download/app.zip
python3 tools/celerctl.py pull /local/apps/HTTP\ Demo/app.json .
python3 tools/celerctl.py rm /local/old.txt
python3 tools/celerctl.py rm -r /local/apps/OldApp   # recursive delete
python3 tools/celerctl.py reboot
python3 tools/celerctl.py logcat             # live logs (Ctrl-C to exit)
python3 tools/celerctl.py ota push build/CelerOS.bin   # firmware without esptool
python3 tools/celerctl.py screencap shot.png # display capture -> PNG (RLE: ~10x faster)
python3 tools/celerctl.py coredump            # last crash dump (ELF) -> coredump.elf
python3 tools/celerctl.py coredump --keep     # download without erasing the dump
python3 tools/celerctl.py shell "run Snake"  # open an app (folder, name or package)
python3 tools/celerctl.py tap 120 160        # inject a tap (navigate over USB)
python3 tools/celerctl.py swipe 120 400 120 40  # inject a drag (scroll)
python3 tools/celerctl.py apps list             # installed apps (local + sd)
python3 tools/celerctl.py apps install "data/apps/Web Server"  # install folder
python3 tools/celerctl.py apps install myapp --sd              # to the SD card
python3 tools/celerctl.py apps rm "Touch Test"                 # uninstall
```

### Multiple boards on the same machine

`-p` accepts a port path **or the prefix of the board's USB serial** (the
`K...` MAC-derived serial that S3 firmwares expose). `devices` lists them:

```bash
python3 tools/celerctl.py devices          # shows serial per board
python3 tools/celerctl.py -p K7B4 screencap dog.png
```

### Iterating on the UI without touching the board

`tap`/`swipe` + `screencap` form an adb-like loop: the gesture is queued in
the firmware (opcode `KL_TOUCH`), executed by the UI's TouchPump as if it
were a physical finger — it works for the Navigator and for modals (QWERTY
keyboard) — and `screencap` reads the real framebuffer. Sample session:

```bash
python3 tools/celerctl.py tap 360 88 && python3 tools/celerctl.py screencap s.png
```

### Debugging JS apps (breakpoints, step, eval)

Boards built with `CONFIG_CELEROS_JS_DEBUGGER` (default on the S3 boards;
off on the CYD, where the OTA slot and RAM are tight) speak the Duktape
debugger protocol over the same channel (`KL_DEBUG_CTL`/`KL_DEBUG_DATA`):

```bash
python3 tools/celerctl.py debug Snake          # proxy + REPL in this terminal, app logs included
python3 tools/celerctl.py debug Snake --serve  # proxy only (dmsg on :9092, logs on :9093)
node tools/debug/dbg.js                        #   ...and the REPL in another terminal
```

The app pauses when the debugger attaches (on its first line; without an app
name, the running app attaches on its next yield), and an uncaught error
pauses at the throw. `h` lists the commands: breakpoints (`b 42 if x > 3`,
`tb`, `u <line>`, `B`, `d`, `cond`), `c`/`p` (or Ctrl-C), `s`/`n`/`o`,
inspection with objects rendered as JSON on the device (`v`, `e`, `lc`, `cs`,
`up`/`down`, `set`), watches (`w`), `l` (local source), `i` (heap) and `r`:
lint + push the edited local app folder, then restart it with the breakpoints
restored. Stdin may be a script: every command waits for its reply, and
`c`/`s`/`n`/`o` wait for the next pause.

Robustness rules: the handshake is one-way (the device sends the version
line; the client must not write before it); `q`, closing the client, killing
the proxy (even `kill -9`: the device drops the client when the host session
idles out after 8s) or pulling the cable all detach and the app keeps
running. Protocol and client are covered on the host by
`node test/debug/run.js` (fake Duktape target).

## Speeding up transfers (-b)

The UART channel starts at 115200 baud. With `-b 921600` the tool negotiates the
switch with the firmware and reopens the port faster:

```bash
python3 tools/celerctl.py -b 921600 push firmware.bin /sd/fw.bin
```

Measured with proto 2 (sliding window + CRC32; firmware 1.4.1, 1 MB file
with an md5 check on the way back): push ~78 KB/s on the S3 UART at
921600 baud (`-b 921600` — without it the push stays at ~11 KB/s on the
default 115200), ~113 KB/s on the dog's USJ and ~110 KB/s on the watch's
CDC; pull verified intact on all three. Proto 1 (stop-and-wait, for
comparison): push ~57 KB/s, pull ~190 KB/s. Window announced by the
firmware: 4 chunks on the S3 UARTs/USJ (48 KB RX ring), 1 on the CYD (no
PSRAM to hold a window), 8 on CDC. USB-native ports (CDC/USJ) have no
baud: `-b` prints a warning and continues at USB speed.

The window the firmware announces is the one its own channel can hold.
To OTA-update an OLDER firmware whose announced window overflows its RX
buffer (e.g. a watch from before the CDC buffer fix, announcing 8), cap
it by hand:

```bash
python3 tools/celerctl.py --win 2 -p <port> ota push CelerOS.bin
```

## How the channel coexists with the console

The console UART multiplexes two modes (`main/USBDevice/SerialLink.cpp`) —
the same scheme runs on the USB-Serial/JTAG when `CELEROS_LINK_ON_USJ` is
on:

- **console** — human interactive shell (echo, `celer> ` prompt) and
  visible ESP_LOG/Serial logs. Open minicom/monitor and use it.
- **link** — triggered by the arrival of a HELLO frame (`0x43 0x01 ...`);
  human typing never produces that sequence. Logs are suspended on the UART
  (keystrokes go into a ring) and the session returns to console mode
  after ~8 s without frames, restoring the baud rate. Opening a session in
  another channel (e.g. CDC1) takes over as the active one.

With `logcat`, the ring is drained (history since boot) and logs keep
flowing as live frames inside the tool itself — on the active channel
(UART or CDC).

## Note on native USB / Mass Storage

The ESP32-S3 has USB-OTG (GPIO19/20), but **on the SmartDisplay 4848S040
those pins are used by the board** (GPIO19 = GT911 touch SDA, GPIO20 = RGB
display G1 line) and the USB connector is CH340 only. That is why HostLink
runs on the UART and USB Mass Storage is not possible on these boards.

The native USB code (dual CDC via TinyUSB, `main/USBDevice/USBDevice.cpp`)
is live on the Waveshare watch (`CONFIG_CELEROS_USB_NATIVE`: CDC0 shell +
CDC1 celerctl) and dormant for boards whose GPIO19/20 become free. The
SpotPear dog takes the third road: its only USB is the USB-Serial/JTAG, so
`CONFIG_CELEROS_LINK_ON_USJ` multiplexes console+link on that same port —
no console/OpenOCD/esptool sacrifice.
