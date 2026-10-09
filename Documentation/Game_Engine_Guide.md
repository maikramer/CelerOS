# CelerOS Game Engine Guide

**English** | [Português (BR)](Game_Engine_Guide.pt-BR.md)

A complete 2D game engine for CelerOS apps, shipped as two plain JS modules you vendor into your app folder — no firmware changes, no extra permissions:

- **`engine.js`** — game loop with scenes, touch gesture recognition, drawing with camera, sprites, particles, tweens/timers, chiptune audio with a beat clock, and NVS saves.
- **`physics.js`** (optional) — arcade 2D physics: circles/AABB, gravity, bounce, friction, anti-tunneling substeps, tilemaps and Verlet ropes/cloth. Pure math, zero dependencies.

Both are ES5 (Duktape) and feature-detect the firmware at runtime, so the same game runs on every board — and unchanged in the Node test harness and emulator.

## 1. Requirements

| | |
|---|---|
| Firmware API | 23+ (newer features — smooth primitives, `playMusic`, native canvas — are auto-detected) |
| Intended boards | The ESP32-S3 boards with PSRAM: SmartDisplay, Waveshare watch, SpotPear dog |
| Size budget | `engine.js` ≈ 33 KB + `physics.js` ≈ 16 KB. Declare `"requires": ["psram"]` in `app.json` to lift the JS budget from 48 KB to 128 KB (the store then blocks installing on non-PSRAM boards — which is what you want for engine games) |
| App flavor | `"topbar": false` for fullscreen games (like Supernova) is recommended; the exit button lives in your title menu (`System.exitApp()`) |
| CYD (no PSRAM) | Engine games don't fit the 48 KB budget. Either vendor `engine.js` alone (≈ 33 KB, leaving ~15 KB for your code) or write plain-canvas games |

## 2. Getting started

```bash
# a ready-to-run game scaffold (app.json + Quica sample + engine vendored):
node tools/sdk/celer.js new MyGame --game

# add/update the engine in an EXISTING app folder:
node tools/sdk/celer.js engine path/to/MyApp

# iterate (lint runs on save; emulator renders a PNG; device does live reload):
node tools/sdk/celer.js lint MyGame
node tools/sdk/celer.js emu MyGame
python3 tools/celerctl.py dev MyGame
```

`new --game` scaffolds **Quica**, a complete keep-up game (title/game/over scenes, drag paddle, physics balls, particles, sfx, high score) — read its `main.js`; it is the canonical example.

## 3. Quickstart: a complete game in ~30 lines

```js
// main.js
var E = require("engine");
var P = require("physics");

E.init({ dir: "Bolas", fps: 30, save: "bolas." });
var W = E.W, H = E.H;

var world = P.world({ gravity: { x: 0, y: 300 },
                      bounds: { x: 0, y: 0, w: W, h: H }, walls: "contain" });
var balls = [];

E.run({
  jogo: {
    update: function (dt) {
      if (E.input.tap) {
        balls.push(world.add({ x: E.input.tap.x, y: E.input.tap.y, r: 8,
                               vx: E.m.rand(-120, 120), vy: 0, bounce: 0.85 }));
        E.audio.sfx("ui");
      }
      world.step(dt);
    },
    draw: function () {
      System.fillScreen(E.theme.bg);
      for (var i = 0; i < world.count; i++) {
        var b = world.all[i];
        E.gfx.circle(b.x, b.y, b.r, E.theme.accent);
      }
      E.gfx.text(balls.length + " bolas", W / 2, 8,
                 { align: "center", color: E.theme.textDim, font: 1 });
      E.fx.draw();
    }
  }
}, "jogo");
```

The engine **owns the loop**: you describe scenes and the engine calls `update(dt)`/`draw()` at your target fps, polling touch, ticking effects/timers/tweens/audio for you. That loop is exec-timeout safe (it yields every frame).

## 4. Scenes and the game loop

```js
E.init({ dir: "MyGame",   // app folder name → asset base (/local/apps/MyGame/assets/)
         fps: 30,          // frame target (default 30, 0 = uncapped)
         native: false,    // true = native canvas (physical pixels, API 28)
         save: "mygame.",  // NVS key prefix for E.save
         particles: 96 }); // fx pool size
E.run(scenes, "titulo");
```

A scene is `{ enter, update(dt), draw, exit }` — all optional. `E.goto("fim")` switches at the start of the next frame (running the old scene's `exit` first); `E.quit()` ends `E.run()`. Use `E.data` for state that must survive scene changes (it lives for the whole app), and `E.sceneName`/`E.dt`/`E.fps` for introspection. A scene may override the global frame target with `fps` (menus at 30, the action uncapped with `fps: 0`):

```js
E.run({
  menu:    { fps: 30, update, draw },
  jogando: { fps: 0,  update, draw },   // runs as fast as the board allows
}, "menu");
```

Per frame the engine runs, in order: pending scene switch → dt/fps → input poll → audio keep-alive → camera → fx → timers → tweens → `update(dt)` → `draw()` → frame pacing.

**Leaving scenes cleanly:** timers and tweens created in a scene outlive it. Call `E.clearTimers()`/`E.clearTweens()` in `exit()` (or cancel individual handles with `E.cancel(t)`).

## 5. Input

`E.input` is refreshed once per frame:

| Field | Meaning |
|---|---|
| `x, y, down` | current touch point/state |
| `justDown` / `justUp` | pressed / released this frame |
| `dx, dy` | movement since last frame |
| `holdDx, holdDy` | movement since press (relative drag, e.g. paddle control) |
| `moved` | drag passed the threshold (`dragThresh`, default 12 px) |
| `tap` | `{x, y}` on release, if short and (almost) still (`tapMs` 350 ms) |
| `swipe` | `{dir, dx, dy, dist}` on release, if dragged ≥ `swipeMin` (30 px) |
| `longpress` | held ≥ `longMs` (600 ms) without dragging; fires once per touch |
| `btn` | physical button (devkit): 1 short, 2 long |

Thresholds are fields on `E.input` — tune them per game. Hit-testing:

```js
if (E.input.tap) { ... }                    // raw tap anywhere
if (E.hit(rect)) { ... }                    // tap inside {x,y,w,h}
if (E.press(rect)) { ... }                  // finger currently down inside (button highlight)
```

## 6. Drawing (E.gfx)

All functions take **world coordinates** and apply the camera; pass `{screen: true}` to draw HUD on top of it. Colors are RGB565 ints — use `E.theme` (`bg, card, raised, stroke, accent, accentD, onAccent, text, textDim, ok, warn, err`) or constants (`BLACK`, `WHITE`, ...). Everything falls back to plain primitives on old firmware:

```js
E.gfx.rect(x, y, w, h, color, { fill: true, r: 8 });       // r = rounded corners (API 22)
E.gfx.circle(x, y, r, color, { smooth: true });             // anti-aliased fill (API 22)
E.gfx.line(x0, y0, x1, y1, color, { w: 3 });                // wide line (API 22)
E.gfx.tri(x0, y0, x1, y1, x2, y2, color);
E.gfx.gradient(x, y, w, h, c1, c2, { dir: "y" });
E.gfx.arc(x, y, r0, r1, a0, a1, color);                     // degrees, -90 = up
E.gfx.text("SCORE 42", x, y, { size: 2, font: 4, align: "center",
                               valign: "middle", color: 0xFFFF, bg: 0x0000 });
E.gfx.button("JOGAR", x, y, w, h, { primary: true });        // returns the rect for E.hit()
E.gfx.bar(x, y, w, h, 0.75);                                 // meter (health/charge)
E.gfx.panel(x, y, w, h);                                     // card with border
```

`bg` omitted in `text` = transparent background (single-arg `setTextColor`). Mixing colors: `System.mixColor(c1, c2, pct)` (API 22).

## 7. Camera

```js
E.cam.follow(player, 0.15);   // lerp toward a {x, y} target (screen-centered)
E.cam.bounds = { x: 0, y: 0, w: 960, h: 320 };   // clamp within the world
E.cam.shake(5, 0.3);          // power (px), duration (s) — decays automatically
E.cam.center(x, y);           // snap
E.cam.reset();                // identity (call on scene enter)
```

`wx()/wy()` convert world→screen if you draw with raw `System.*` calls.

## 8. Entities: groups and pools

```js
var enemies = E.group();
enemies.add({ x: 10, y: 10, update: function (dt) { this.x += 20 * dt; },
              draw: function () { E.gfx.circle(this.x, this.y, 6, 0xF800); } });
enemies.update(E.dt);   // sets o.dead = true → auto-removed (swap-pop)
enemies.draw();

var bullets = E.pool(32, function () { return { x: 0, y: 0, vx: 0, vy: 0 }; });
var b = bullets.spawn();        // recycled object, or null when full
b.x = 100; b.y = 200; b.dead = false;
bullets.update(E.dt); bullets.draw();
```

Pools pre-allocate everything: no per-frame allocation means no GC hiccups.

## 9. Sprites and animation

The firmware sprite pool has **4 slots** (PSRAM-backed). `E.spr.load` decodes each PNG **once** into a slot and keeps a procedural `paint` fallback, so the game runs even with missing assets or on boards without sprites:

```js
E.spr.load([
  { name: "nave", file: "nave", w: 72, h: 72,
    paint: function (w, h, x, y) {           // fallback: draw with primitives
      E.gfx.tri(x + w / 2, y, x, y + h, x + w, y + h, E.theme.accent);
    } },
  { name: "inimigo", file: "inimigo", w: 56, h: 56, paint: ... },
]);
E.spr.blit("nave", x, y, { cx: true, cy: true, key: 0x0000 });  // key = chroma color
```

- Assets are probed in `E.spr.bases` (set by `init({dir})` to `/local/apps/<dir>/assets/` and `/sd/apps/<dir>/assets/`).
- **Transparency convention:** masterize PNGs with a *pure black* background (the chroma key) and turn interior blacks into near-black `(0,0,8)` — same trick as Supernova. Only slot-backed sprites support the key; painter fallbacks draw whatever you draw.
- More defs than slots → extras become painter-only automatically. `E.spr.backed(name)` tells you whether a sprite got a real slot (fast chroma-key blit) or will draw through its painter.

Flipbook animation without extra slots: frames are painter functions (or loaded sprite names):

```js
var boom = E.anim([function (x, y) { ... }, function (x, y) { ... }], 12, false);
// per frame: boom.tick(E.dt); if (!boom.done) boom.draw(x, y);
```

## 10. FX

```js
E.fx.burst(x, y, { n: 14, colors: [0xFFE0, 0xFD20], speed: 120, life: 0.6,
                   grav: 200, shape: "dot" });   // dot | spark | ring
E.fx.ring(x, y, { speed: 700, color: 0xFFE0 });  // shockwave (expanding circle)
E.fx.popText(x, y, "+10", { color: 0xFFE0 });    // floating score
E.fx.flash(0xFFFF, 150);                         // fullscreen flash
var stars = E.fx.stars(60, { colors: [0x39E7, 0xC5F9] });  // parallax (keep the ref)
stars.update(E.dt); stars.draw();
E.fx.draw();                                     // call at the END of your scene draw
```

Particles/floaters/flash are ticked by the loop; only `draw()` is yours.

## 11. Tweens and timers

Engine-ticked (not `setTimeout` — no 8-timer limit, deterministic under the harness):

```js
E.tween(btn, { y: 160 }, 500, { ease: E.m.outBack, onDone: function () {} });
E.after(1200, function () { E.goto("fim"); });
var id = E.every(2.5 * 1000, spawnWave);
E.cancel(id); E.clearTimers(); E.clearTweens();
```

Easings: `E.m.linear/inQuad/outQuad/inOutQuad/outBack`.

## 12. Audio

One speaker slot on the device: `playMusic` OR `playWav`/`playTone` at a time. `E.audio` manages the music side and keeps sfx polite:

```js
E.audio.music({ bpm: 132, loops: 0, tracks: [
  { wave: "sq",  vol: 70, notes: [[69,4],[71,4],[72,4],[69,4]] },   // [midi, 16ths]
  { wave: "tri", vol: 60, drum: true, notes: [[36,2],[0,2],[38,2],[0,2]] },
]});
E.audio.beat();        // beat position (1.0 = downbeat); -1 when not playing
E.audio.sfx("coin");   // named sfx (skipped while music plays; sfxOverMusic=true overrides)
E.audio.sfx([880, 60]);        // or a raw [freq, ms]
E.audio.sfx([[660,60],[880,80]]); // or a short melody (BLOCKING — keep it short)
E.audio.stop(); E.audio.mute(true); E.audio.volume(80);
```

Music auto-restarts when its loops end (keep-alive). Spawn-on-the-beat: `if (Math.floor(E.audio.beat()) !== lastBeat) spawn()`. Default sfx table: `ui, ok, back, bad, hit, coin, boom` — replace entries in `E.audio.sfxTable`. Boards without a speaker (CYD) no-op everything.

## 13. Save (high scores, settings)

NVS-backed (no permission needed), namespaced by the `save` prefix from `init`:

```js
E.save.set("skin", "azul");
var skin = E.save.get("skin", "verde");
var best = E.save.num("recorde", 0);
if (E.save.best("recorde", score)) { /* new record! */ }
```

## 14. Math and RNG

`E.m`: `clamp, lerp, map, rand(a,b), randInt, pick, dist, dist2, ang, approach, wrap, sign` + easings. `E.rng(seed)` returns a deterministic PRNG function — seed your level generation and your tests become reproducible.

## 15. Physics (`physics.js`, optional)

`require("physics")` — pure math, no `System` calls, so it unit-tests anywhere. Coordinates: y grows **down** (screen); body `x, y` is the **center**; circle bodies have `r`, boxes `w/h`.

```js
var P = require("physics");
var w = P.world({ gravity: { x: 0, y: 900 },
                  bounds: { x: 0, y: 0, w: 240, h: 320 },
                  walls: "contain" });          // contain | wrap | none

var ball = w.add({ x: 120, y: 40, r: 8, bounce: 0.8, friction: 0.1 });
var paddle = w.add({ x: 120, y: 300, w: 64, h: 10, static: true });

w.step(dt);   // once per frame, after your input handling
```

Body options: `vx, vy, ax, ay, gravity` (multiplier), `bounce` (0..1), `friction` (0..1), `drag`, `mass`, `static`, `sensor` (events only), `group`/`mask` (bitmask: pair collides when `a.mask & b.group && b.mask & a.group`), `tiles: false` (skip tilemap), `drop` (ignore one-way platforms), `onCollide(me, other, info{nx,ny,overlap})` (fires once per pair per step), and `grounded` (set when resting on something).

Fast objects are automatically **sub-stepped** so nothing tunnels through thin walls (`world.maxSub` caps the work, default 8). Bounds walls: `"contain"` (clamp + bounce), `"wrap"` (Pac-Man edges) or `"none"`.

### Tilemaps (platformers)

```js
var grid = [
  "............",
  "..==...==...",
  "............",
  "####...####.",
];
var tiles = P.tiles(grid, 16, 16);   // '#' solid, '=' one-way (customize via opts)
w.addTiles(tiles);
tiles.tileAt(px, py); tiles.setTile(col, row, "#");
```

Per-axis resolution, `grounded` on landing, one-way platforms only catch you falling from above (`body.drop = true` to fall through on purpose).

### Rope / cloth / softbody (Verlet)

```js
var pts = []; for (var i = 0; i < 8; i++) pts.push({ x: 120, y: 30 + i * 8 });
var rope = P.verlet({ points: pts, sticks: [{ a: 0, b: 1 }, ...], iterations: 4 });
rope.pin(0);
rope.step(dt);       // opts: gravity, damp, bounds, bounce
```

### Recipes

- **Platformer:** hero = AABB body (`friction` 1, `bounce` 0); move by setting `vx`; jump when `grounded`; camera `follow`s hero.
- **Breakout:** paddle = `static` body you reposition; ball = circle with `bounce: 1`; bricks = dynamic-mass boxes you `remove()` on hit (or a tilemap + `setTile`).
- **Top-down shooter:** `gravity: {x:0, y:0}`, `drag` for friction feel; enemies/bullets in pools; `sensor` bodies for pickups.

## 16. Native canvas (fullscreen, API 28)

`E.init({ native: true })` switches drawing/touch to **physical glass pixels** (e.g. 480×480 on the SmartDisplay instead of the scaled 240×320) — crisper and faster for fullscreen games, but shapes are not uniform-scaled anymore. Requires `"topbar": false` in `app.json` (the request fails gracefully otherwise and `E.caps.native` stays false — always branch on it). `E.W/E.H` reflect the active mode.

## 17. Testing your game

The harness runs your game headless with a virtual clock — physics, engine and all:

```js
// MyApp/test.js (dev-only, never published)
module.exports.wire = function (env) {
  env.__harness.tap(120, 178);                       // press JOGAR
  env.setTimeout(function () {
    if (typeof __harness !== "undefined") { /* app exposed hooks on __harness */ }
  }, 2000);
};
```

```bash
node tools/sdk/celer.js test MyGame       # headless run
node tools/sdk/celer.js emu MyGame --ms 2000 --out tela.png
node tools/sdk/celer.js emu MyGame --frames 0,600,1500   # PNG per milestone + pixel diff
```

In-app, expose an introspection hook like `if (typeof __harness !== "undefined") __harness.myGame = { state: ... };` and drive everything deterministically (see Quica's `test.js` pattern in Supernova).

## 18. Size and performance

- Engine+physics+game go over 48 KB → keep `"requires": ["psram"]`. Sizes today: `engine.js` ≈ 33 KB, `physics.js` ≈ 16 KB.
- **No allocations per frame**: use `E.pool`, `swap-pop` removal, and reuse objects. A `new`/`[...]` per frame per entity is what triggers GC pauses.
- Full-screen `fillScreen` + redraw everything at 30 fps is fine on the S3 boards; for few moving objects prefer dirty-erase (erase old position, draw new).
- `world.step` is O(n²) on body count in the worst case, but a **sweep-and-prune** (bodies sorted by x each substep, early break on x distance) keeps it near-linear for spread-out scenes — shooters with ~40 bodies run comfortably.
- `E.audio.sfx` melodies are **blocking** (`playTone`): keep them under ~300 ms.
- Pure-JS bursts longer than ~1 s trip the firmware exec-timeout — the `E.run` loop yields every frame, so stay inside it.

## 19. API cheat sheet

| Call | Does |
|---|---|
| `E.init(opts)` / `E.run(scenes, first)` | detect caps / own the loop |
| `E.goto(name)` / `E.quit()` | switch scene / leave the loop |
| `E.input`, `E.hit(r)`, `E.press(r)` | touch state, tap in rect, held in rect |
| `E.gfx.*`, `E.cam` | camera-aware drawing, scroll/shake/follow |
| `E.group()`, `E.pool(n, f)` | entity lists, pre-allocated recycling |
| `E.spr.load/blit`, `E.anim(frames, fps)` | PNG sprites with painter fallback, flipbooks |
| `E.fx.burst/popText/flash/stars/draw` | juice |
| `E.tween/after/every/cancel/clear*` | engine-ticked time |
| `E.audio.music/beat/sfx/stop/mute/volume` | chiptune + polite sfx |
| `E.save.get/set/num/best` | NVS persistence |
| `E.m.*`, `E.rng(seed)` | math, deterministic RNG |
| `P.world/add/step`, `P.tiles`, `P.verlet`, `P.hit` | arcade physics (optional module) |

Editor autocomplete ships in the scaffold: `engine.d.ts` (engine + physics) alongside `celer.d.ts` (firmware API).
