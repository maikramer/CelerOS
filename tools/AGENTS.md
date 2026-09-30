# tools/ - host-side Python/shell/Node tooling

## OVERVIEW
Host utilities: device control over serial (`celerctl`), the JS app linter (`app_lint`), the LAN OTA test server, the hub publisher, data-partition flashing, and asset generators. It is a distinct domain (score ~9; own `requirements.txt`). Docs: `README_USBTOOL.md`, `README_OTA.md` (both also in `.pt-BR`).

## WHERE TO LOOK
| Tool | Role | Key usage |
|------|------|-----------|
| `celerctl.py` (822) | HostLink client (device side: `main/USBDevice/`) | `devices [-l]`, `info`, `shell [cmd]`, `ls/cat/rm/mkdir/mv`, `push LOCAL REMOTE`, `pull`, `reboot`, `logcat [--dump]`, `ota push FW.bin [--no-reboot]`, `screencap out.png`, `tap X Y [ms]`, `swipe X0 Y0 X1 Y1 [ms]`, `apps list/install <dir>/rm` |
| `app_lint/` | static "compiler" for JS apps: acorn (ES5, same profile as the device Duktape) + checks against the firmware API. The manifest is DERIVED from source on every run (`JSBindings.cpp` tables, `Js*.cpp` bodies for min arity, `main/CMakeLists.txt` for API level, pt-BR guide for per-fn levels) — never hand-maintained | `node tools/app_lint/lint.js [paths]` (default `data/apps hub_apps`; `--json`, `--strict`, `--dump-manifest`), `... check` = drift code/docs/harness-stubs; wired into CI, `celerhub publish` (errors block) and `celerctl apps install` (`--pula-lint` to force); own tests: `node test/app_lint/run.js` |
| `flash_data.sh` | build the LittleFS image of `data/` and write the `littlefs` partition | `tools/flash_data.sh [smartdisplay\|cyd] [PORT]` (needs `IDF_PATH` exported; uses `bin/mklittlefs.bin` + IDF `parttool.py`) |
| `ota_server.py` | local update.json + firmware server | `--board smartdisplay\|cyd`, `--bin`, `--port` (default 10234), `--version`; the device picks it up through `/local/ota_url.txt` |
| `celerhub.py` | publish/list/delete apps on the hub | validates app folders (runs `app_lint`; errors block publish); `MAX_API_LEVEL` is read from `main/CMakeLists.txt`; token via args/env; hub via `--hub` or `CELER_HUB` (default `https://os.celer.tec.br`) |
| `make_icons.py` | `icons.json` prompts -> `icons_src/*.png` (512px, local FLUX via text2d) -> `data/icons/*.png` 64px RGB565-dithered | `--regen` forces art regeneration; also writes `docs/assets/icons_preview.png` |
| `make_splash.py` | PNG -> `main/Assets/SplashLogo.h` (64-color PNG composed over THEME_BG, drawn with `drawPng`) | default source is `Documentation/assets/celeros_logo.png`; `--colors N` |
| `size_report.py` | image size vs OTA slot per board, top libraries, deltas | `--baseline f.json`, `--save-baseline f.json`, `--min-free-kb 64` (exit 1 when a slot is tighter); run in the IDF env after building `build/` + `build-cyd/` |

## CONVENTIONS
- The generators are manual steps, not part of `idf.py build`. Commit their outputs (`data/icons/`, `SplashLogo.h`).
- `celerctl` defaults to the UART (CH340, `/dev/ttyUSB0`) through SerialLink. On boards built with `CELEROS_USB_NATIVE` it talks to CDC1 instead. `-b` sets the baud.
- The littlefs size is read from `partitions_{16MB,4MB}.csv`, which is the single source of truth. Never duplicate it in scripts.
- CLI help and output text are Portuguese.

## COMMON FLOWS
```bash
pip install -r tools/requirements.txt                        # pyserial only; make_icons also needs Pillow (not listed)
python3 tools/celerctl.py apps install "data/apps/Settings"  # push one app without reflashing littlefs (linta antes)
python3 tools/celerctl.py ota push build/CelerOS.bin         # firmware over serial, no esptool
python3 tools/celerctl.py logcat --dump                      # device log ring buffer
python3 tools/celerhub.py list                               # local vs hub versions
node tools/app_lint/lint.js "data/apps/Settings"             # linta um app (parse ES5 + API do firmware)
node tools/app_lint/lint.js check                            # drift: codigo x docs x stubs do harness
```

## ANTI-PATTERNS
- Replacing icon art by editing `data/icons/` directly. Delete `icons_src/<id>.png` and/or edit `icons.json`, then rerun `make_icons.py`. A hand-made 512px PNG in `icons_src/` is respected.
- Committing `__pycache__/` (stray `kryonctl` pyc files are leftovers from the old project name).
- Running `celerctl` while `idf.py monitor` holds the same port.
