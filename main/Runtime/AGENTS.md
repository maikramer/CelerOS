# main/Runtime/ - JS API surface (Duktape bindings)

## OVERVIEW
`JSBindings.cpp` (2182 LOC, ~180 property registrations) exposes the C++ firmware to apps as the `System`, `Net` and `FS` globals. It is a distinct domain (score ~10) because it is the contract every app in `data/` and `hub_apps/` depends on.

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Per-app reset of all binding state | `JSBindings::init(ctx, tft, appTitle, topbarFixed)` (~line 1822), called by CelerKernel on each app launch |
| API level reported to apps | `System.getAPILevel()` pushes `CELEROS_API_LEVEL` (defined in `main/CMakeLists.txt`, currently 6) |
| Topbar (fixed vs retractile, custom text/buttons, tap FIFO) | `s_tb*` / `s_bar*` statics, `retractTick`, `drawAppTopbar` |
| Automatic frame / double buffer | `s_frame` (PSRAM sprite, allocated once and reused across apps), `createSprite`/`pushSprite`/`present` |
| Docked keyboard | `keypad*` session API + `System.prompt()` (blocking) |
| Streaming download | `Net.download` (API 6; older firmware truncated bodies at 32KB) |
| Public docs of every call | `Documentation/JS_API_Guide.md` (EN) / `.pt-BR.md` |

## API LEVEL HISTORY
1 base (draw/touch/GPIO/FS/time) · 2 `Net` · 3 system apps moved into LittleFS JS (W8) · 5 docked keypad (W9) · 6 custom topbar + streaming `Net.download`.

## CONVENTIONS
- JS works in virtual 240x320 coordinates. Every draw and touch conversion goes through `UI::sx/sy` (in) and the inverse (touch out). Don't expose physical pixels to JS.
- Bindings are `static duk_ret_t js_*(duk_context*)` functions. File-scope state uses the `s_` prefix.
- Any new state that survives between frames **must be reset in `init()`**, because an app that exits without cleanup (sprite, keypad, topbar) otherwise leaks into the next app.
- Adding or changing a JS call means you must:
  1. Bump `CELEROS_API_LEVEL` if apps can feature-detect it.
  2. Document it in both `JS_API_Guide` languages.
  3. Stub it in `test/js_harness/run.js`.
- There is no `Promise` builtin (compiled out), so network/FS calls stay blocking and synchronous.

## ANTI-PATTERNS
- Throwing C++ exceptions or `abort()` on bad JS args. Use `duk_error`/type coercion so a bad app can't crash the OS.
- Drawing directly to `tft` when a frame/sprite is bound, which bypasses `present()` and causes tearing.
- Removing or renaming existing JS calls, which breaks installed hub apps with no migration path.
- Open TODO (~line 133): the PSRAM full-screen sprite path is incomplete. Check it before touching frame logic.
