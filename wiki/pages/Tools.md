# Tools

**English** | [Português (BR)](/maikramer/CelerOS/wiki/Ferramentas)

Host-side utilities (Python/shell) in [tools/](tools). Install with
`pip install -r tools/requirements.txt` (pyserial; `make_icons` also needs
Pillow, which is not in the list).

| Tool | Role |
|---|---|
| [celerctl.py](tools/celerctl.py) | HostLink client: device control over serial — full reference in [celerctl (USB)](/maikramer/CelerOS/wiki/celerctl-USB) |
| [flash_data.sh](tools/flash_data.sh) | Builds the LittleFS image from `data/` and flashes the `littlefs` partition |
| [ota_server.py](tools/ota_server.py) | Local server of `update.json` + firmware for OTA tests |
| [celerhub.py](tools/celerhub.py) | Publishes/lists/removes apps on the hub (validates app folders; token via args/env; `--hub` or `CELER_HUB`, default `https://os.celer.tec.br`) |
| [sdk/celer.js](tools/sdk/celer.js) | App SDK: scaffold, lint, editor typings, headless emulator and store publishing (Node, zero npm deps) |
| [make_icons.py](tools/make_icons.py) | `icons.json` → `icons_src/*.png` (512 px) → `data/icons/*.png` (64 px, dithered RGB565) |
| [make_splash.py](tools/make_splash.py) | PNG → `main/Assets/SplashLogo.h` (RGB565 over THEME_BG) |

## celerctl — the everyday swiss knife

```bash
python3 tools/celerctl.py devices            # list devices
python3 tools/celerctl.py shell              # interactive shell on the device
python3 tools/celerctl.py push main.js /local/apps/MyApp/main.js
python3 tools/celerctl.py logcat --dump      # log ring buffer (repeatable; --ts/--grep)
python3 tools/celerctl.py debug MyApp        # JS debugger: breakpoints/step/eval
python3 tools/celerctl.py ota push build/CelerOS.bin   # firmware over serial
python3 tools/celerctl.py screencap out.png  # framebuffer capture
python3 tools/celerctl.py tap 120 160        # inject a touch
```

It talks over the console UART (CH340, `/dev/ttyUSB0`; `-b` changes the
baud) — over CDC1 on boards with `CELEROS_USB_NATIVE` (watch) and over the
USB-Serial/JTAG itself on `CELEROS_LINK_ON_USJ` boards (dog). With a
proto-2 firmware every frame carries a CRC32 and transfers use a sliding
window (negotiated in the HELLO; old firmware stays on the legacy path —
see `tools/README_USBTOOL.md`). Multiple boards: `-p` accepts the USB
serial prefix shown by `devices`. The opcodes live in
`main/USBDevice/HostLink.h` and the tool parses that file with a regex:
one source of truth for both sides. Do not run `celerctl` while
`idf.py monitor` holds the same port.

`logcat --dump` copies the ring buffer without draining it (repeatable);
`--ts` prefixes host-side timestamps and `--grep` filters lines on the
host. Every app error is persisted to `/local/lastcrash.txt` (OS version,
date, uptime, app + full stack) and survives a reboot — the serial shell's
`lasterror` reports its size and write time; the web file manager reads the
file.

`celerctl debug MyApp` (boards with `CONFIG_CELEROS_JS_DEBUGGER`, default
on the ESP32-S3 targets) brings up a TCP proxy and the Duktape debugger
REPL: the app pauses on attach (first line) and at the throw of an
uncaught error, with breakpoints, stepping, eval, watches and `r`
(restart, rereading `main.js` and keeping breakpoints). Full reference in
[`tools/README_USBTOOL.md`](tools/README_USBTOOL.md).

## celerhub — publishing to the hub

```bash
python3 tools/celerhub.py list               # local versions vs hub
python3 tools/celerhub.py publish hub_apps/2048
```

## celer.js — the app SDK

```bash
node tools/sdk/celer.js new MeuApp      # scaffold: app.json + main.js + icon + editor typings
node tools/sdk/celer.js lint MeuApp     # ES5 + API check against the real firmware manifest (app_lint)
node tools/sdk/celer.js test MeuApp     # runs the app in the Node harness (stubbed device APIs)
node tools/sdk/celer.js emu MeuApp --frames "0,600,1500"   # emulator: PNG per clock mark + pixel diff
python3 tools/celerctl.py dev MeuApp    # live on the device (push + relaunch)
node tools/sdk/celer.js publish MeuApp  # publishes to the hub store
```

`lint`/`check` delegate to `tools/app_lint` (the manifest is derived from the
firmware source, so the linter knows the real API surface); `dev`/`publish`
delegate to `celerctl`/`celerhub`. `emu` runs the app headlessly and
snapshots the screen to PNG; with `--frames "0,600,1500"` it renders one
PNG per clock mark (`tela-0000.png`, ..., in the app's `.dev/` folder) and
prints the pixel diff between consecutive frames. Zero npm dependencies —
acorn is vendored and the PNG encoder uses Node's zlib.

## Flashing without a toolchain — CelerOS Flasher

Every `v*` tag publishes, on
[GitHub Releases](https://github.com/maikramer/CelerOS/releases):

* one zip per board — firmware + LittleFS image + `flash.json` (the flash
  plan), plus a `README.txt` with the equivalent `esptool` command line if
  you'd rather flash manually;
* the **CelerOS Flasher**, a GUI flasher for Linux and Windows with esptool
  embedded — download it plus your board's zip into the same folder, pick the
  board, plug the device, flash. No ESP-IDF install needed.

## Asset generators

`make_icons.py` and `make_splash.py` are manual steps (not part of
`idf.py build`); their outputs are committed (`data/icons/`,
`SplashLogo.h`).

To change an icon's art: edit `tools/icons.json` and/or delete
`icons_src/<id>.png` and run `make_icons.py --regen` — never edit
`data/icons/*.png` by hand (a hand-made 512 px PNG in `icons_src/` is
respected as is).
