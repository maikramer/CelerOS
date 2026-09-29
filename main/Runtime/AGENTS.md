# main/Runtime/ - JS API surface (Duktape bindings)

## OVERVIEW
The C++ side of the JS API: exposes the firmware to apps as the `System`, `Net` and `FS` globals (~124 bindings). Distinct domain because every app in `data/` and `hub_apps/` depends on this contract.

## LAYOUT
| File | Holds |
|------|-------|
| `JSBindings.cpp` | Core: topbar/chrome engine (`s_tb*`, `pollAppChrome`, `drawAppTopbar`), automatic frame (`s_frame`, `present`), custom topbar + icon/PNG/copy bindings, and `init()` with the registration tables |
| `JsInternal.h` | Shared state (`s_jsTft`, `s_topbarFixed`, `s_frame`, `s_frameDirty`) and the 240x320 mapping helpers `jsc/jsx/jsy/jsu/jsH/appSh/appScaleY` |
| `JsGfx.cpp` | Sprites, drawing primitives, text, color/screen size, `drawBMP` |
| `JsSystem.cpp` | Touch, time, info, restart, IP |
| `JsNet.cpp` | `Net.*` (get/getJSON/post/download) |
| `JsFs.cpp` | `FS.*` |
| `JsKeypad.cpp` | `System.prompt` + docked keypad session |
| `JsSystemApps.cpp` | System-app support (API 3+): brightness, PIN, web auth, OTA, WiFi, time settings |
| `JsGpio.cpp` | `System.gpio`; compiled only with `CONFIG_CELEROS_JS_GPIO` |

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Add a binding | define `JSBindings::js_x` in the module file, declare it in `JSBindings.h`, add `{"name", js_x, nargs}` to the right `kFnsN[]` table in `init()` |
| Per-app reset of all binding state | `JSBindings::init(ctx, tft, appTitle, topbarFixed)`, called by CelerKernel on each app launch |
| API level reported to apps | `System.getAPILevel()` pushes `CELEROS_API_LEVEL` (defined in `main/CMakeLists.txt`, currently 6) |
| Streaming download | `Net.download` in `JsNet.cpp` (API 6; older firmware truncated bodies at 32KB) |
| Public docs of every call | `Documentation/JS_API_Guide.md` (EN) / `.pt-BR.md` |

## API LEVEL HISTORY
1 base (draw/touch/GPIO/FS/time) · 2 `Net` · 3 system apps moved into LittleFS JS (W8) · 5 docked keypad (W9) · 6 custom topbar + streaming `Net.download` · 7 keypad/prompt `mask`, PT-BR accent keyboard page, Latin-1 text in every font (apps with accented strings declare `api: 7`), `System.led`/`lightLevel`/auto-brightness and working `beep` on the CYD (`Hardware/BoardIO`).

## CONVENTIONS
- JS works in virtual 240x320 coordinates. Every draw and touch conversion goes through `UI::sx/sy` (in) and the inverse (touch out). Don't expose physical pixels to JS.
- Bindings are `static duk_ret_t js_*(duk_context*)` members of `JSBindings`. File-scope state uses the `s_` prefix; keep it `static` in its module unless another module needs it (then declare it in `JsInternal.h`).
- A Kconfig-gated module keeps its `init()` registration inside the same `#if CONFIG_CELEROS_<X>`; the JS object is simply absent when off, so apps feature-detect with `typeof`.
- Any new state that survives between frames **must be reset in `init()`**, because an app that exits without cleanup (sprite, keypad, topbar) otherwise leaks into the next app.
- Adding or changing a JS call means you must:
  1. Bump `CELEROS_API_LEVEL` if apps can feature-detect it.
  2. Document it in both `JS_API_Guide` languages.
  3. Stub it in `test/js_harness/run.js`.
- Duktape is built ES5-lean (`components/duktape/celeros_duk_config.yaml`): no Proxy, Reflect, Symbol, typed arrays/ArrayBuffer, TextEncoder, CBOR, `Duktape.Thread`. Network/FS calls stay blocking and synchronous.

## ANTI-PATTERNS
- Throwing C++ exceptions or `abort()` on bad JS args. Use `duk_error`/type coercion so a bad app can't crash the OS.
- Drawing directly to `tft` when a frame/sprite is bound, which bypasses `present()` and causes tearing.
- Removing or renaming existing JS calls, which breaks installed hub apps with no migration path.
