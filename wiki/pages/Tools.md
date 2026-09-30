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
| [make_icons.py](tools/make_icons.py) | `icons.json` → `icons_src/*.png` (512 px) → `data/icons/*.png` (64 px, dithered RGB565) |
| [make_splash.py](tools/make_splash.py) | PNG → `main/Assets/SplashLogo.h` (RGB565 over THEME_BG) |

## celerctl — the everyday swiss knife

```bash
python3 tools/celerctl.py devices            # list devices
python3 tools/celerctl.py shell              # interactive shell on the device
python3 tools/celerctl.py push main.js /local/apps/MyApp/main.js
python3 tools/celerctl.py logcat --dump      # log ring buffer
python3 tools/celerctl.py ota push build/CelerOS.bin   # firmware over serial
python3 tools/celerctl.py screencap out.png  # framebuffer capture
python3 tools/celerctl.py tap 120 160        # inject a touch
```

It talks over the console UART (CH340, `/dev/ttyUSB0`; `-b` changes the
baud) — or over CDC1 on boards with `CELEROS_USB_NATIVE`. The opcodes live
in `main/USBDevice/HostLink.h` and the tool parses that file with a regex:
one source of truth for both sides. Do not run `celerctl` while
`idf.py monitor` holds the same port.

## celerhub — publishing to the hub

```bash
python3 tools/celerhub.py list               # local versions vs hub
python3 tools/celerhub.py publish hub_apps/2048
```

## Asset generators

`make_icons.py` and `make_splash.py` are manual steps (not part of
`idf.py build`); their outputs are committed (`data/icons/`,
`SplashLogo.h`).

To change an icon's art: edit `tools/icons.json` and/or delete
`icons_src/<id>.png` and run `make_icons.py --regen` — never edit
`data/icons/*.png` by hand (a hand-made 512 px PNG in `icons_src/` is
respected as is).
