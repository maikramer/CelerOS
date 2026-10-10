# CelerOS Game Engine Guide

**English** | [Português (BR)](Game_Engine_Guide.pt-BR.md)

A complete 2D game engine for CelerOS apps, shipped as two plain JS modules — since API 30, **shared hub dependencies** (`"deps"` in `app.json`): the store installs them into the public `/local/modules` cache and `require()` resolves from there, one copy per version on the device (your game package gets ~53 KB lighter). No extra permissions; vendoring a copy into the app folder still works (and wins):

- **`celeros.engine`** — game loop with scenes, touch gesture recognition, drawing with camera, sprites, particles, tweens/timers, chiptune audio with a beat clock, and NVS saves.
- **`celeros.physics`** (optional) — the physics module: native-accelerated Verlet ropes/cloth (`P.verletFast`, API 31) and rigid bodies that rotate and topple (`P.rigid`, API 33). **Most games need none of it** — see the physics ladder in §15.

Both are ES5 (Duktape) and feature-detect the firmware at runtime, so the same game runs on every board — and unchanged in the Node test harness and emulator.

## 1. Requirements

| | |
|---|---|
| Firmware API | 23+ for modules; **30+** for the shared deps (newer features — smooth primitives, `playMusic`, native canvas — are auto-detected) |
| Intended boards | The ESP32-S3 boards with PSRAM: SmartDisplay, Waveshare watch, SpotPear dog |
| Size budget | `celeros.engine` ≈ 35 KB (+ `celeros.physics` ≈ 17 KB **only if declared**) — deps count toward the app ceiling EVEN shared (the engine still compiles inside each game's heap): declare `"requires": ["psram"]` in `app.json` to lift the JS budget from 48 KB to 128 KB (the store then blocks installing on non-PSRAM boards — which is what you want for engine games) |
| App flavor | `"topbar": false` for fullscreen games (like Supernova) is recommended; the exit button lives in your title menu (`System.exitApp()`) |
| CYD (no PSRAM) | Engine games don't fit the 48 KB budget — not even as deps (the sum still counts). Either vendor the engine alone (≈ 35 KB, leaving ~13 KB for your code) or write plain-canvas games |

## 2. Getting started

```bash
# a ready-to-run game scaffold (app.json with deps + Quica sample; the
# engine is NOT copied — it comes from the hub at install time):
node tools/sdk/celer.js new MyGame --game

# app.json deps vs versions on the hub (and the local tree):
node tools/sdk/celer.js deps MyGame
node tools/sdk/celer.js deps set celeros.engine ^1.0.0 MyGame

# publish the engine deps to the hub repository (publishes the canonical
# tools/sdk/engine/ tree; needs a token with the deps scope):
python3 tools/celerhub.py publish-dep tools/sdk/engine/celeros.engine.js --min-api 28
python3 tools/celerhub.py publish-dep tools/sdk/engine/celeros.physics.js --min-api 31

# iterate (lint runs on save; emulator renders a PNG; device does live
# reload — on the PC require resolves deps from the tools/sdk/engine tree):
node tools/sdk/celer.js lint MyGame
node tools/sdk/celer.js emu MyGame
python3 tools/celerctl.py dev MyGame
```

`new --game` scaffolds **Quica**, a complete keep-up game (title/game/over scenes, drag paddle, bouncing balls, particles, sfx, high score) — read its `main.js`; it is the canonical example. Its ball physics is hand-rolled on purpose: the physics ladder starts at zero (§15).

## 3. Quickstart: a complete game in ~30 lines

```js
// main.js
var E = require("celeros.engine");

E.init({ dir: "Bolas", fps: 30, save: "bolas." });
var W = E.W, H = E.H;

var balls = [];                    // pool: no allocation inside the frame

E.run({
  jogo: {
    update: function (dt) {
      if (E.input.tap) {
        balls.push({ x: E.input.tap.x, y: E.input.tap.y, r: 8,
                     vx: E.m.rand(-120, 120), vy: 0 });   // bounce: 0.85 below
        if (balls.length > 40) balls.shift();             // pool cap
        E.audio.sfx("ui");
      }
      for (var i = 0; i < balls.length; i++) {
        var b = balls[i];
        b.vy += 900 * dt;                                 // gravity
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        if (b.x - b.r < 0) { b.x = b.r; b.vx = Math.abs(b.vx) * 0.85; }
        if (b.x + b.r > W) { b.x = W - b.r; b.vx = -Math.abs(b.vx) * 0.85; }
        if (b.y + b.r > H) { b.y = H - b.r; b.vy = -Math.abs(b.vy) * 0.85; }
      }
    },
    draw: function () {
      System.fillScreen(E.theme.bg);
      for (var i = 0; i < balls.length; i++) {
        E.gfx.circle(balls[i].x, balls[i].y, balls[i].r, E.theme.accent);
      }
      E.gfx.text(balls.length + " bolas", W / 2, 8,
                 { align: "center", color: E.theme.textDim, font: 1 });
      E.fx.draw();
    }
  }
}, "jogo");
```

That is the whole physics this game needs: integrate, move, reflect on the
walls (§15, rung 0 of the ladder — a few bodies and planes is always cheaper
by hand than any engine).

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

**Static scenes (menus, pause, game over):** `static: true` makes the scene draw only on entry, on `E.redraw()` and when the finger enters/leaves an `E.gfx.button` — a still menu repaints (and pushes) nothing per frame. On the SmartDisplay's RGB panel the full redraw of a menu at 30 fps was what made the glass "shake with flashes":

```js
menu: { static: true,
        update: function () { if (E.hit(this.btn)) E.goto("jogo"); },
        draw: function () { /* everything once */ this.btn = E.gfx.button("JOGAR", ...); } }
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
E.gfx.text("SCORE 42", x, y, { ts: "big", align: "center",       // role (scales with E.U)
                               valign: "middle", color: 0xFFFF, bg: 0x0000 });
E.gfx.text("SUPERNOVA", x, y, { px: E.u(21), fit: W - 20 });      // pixel height, shrink to fit
E.gfx.measure("SCORE 42", { ts: "big" });                         // width in pixels
E.gfx.button("JOGAR", x, y, w, h, { primary: true });        // returns the rect for E.hit()
E.gfx.bar(x, y, w, h, 0.75);                                 // meter (health/charge)
E.gfx.panel(x, y, w, h);                                     // card with border
```

`bg` omitted in `text` = transparent background (single-arg `setTextColor`). Mixing colors: `System.mixColor(c1, c2, pct)` (API 22) — `pct`% of the way from `c1` to `c2`.

**Scale and type (1.2):** lay out in project units — `E.u(v)` turns a measure of the 240-wide design into pixels of the current screen (`E.U` = min(W, H)/240: 1 on the virtual canvas, 2 on the 480 native canvas, ~1.7 on the watch). Text goes by **pixel height**: `px`, or a role in `E.ts` (`tiny 9, small 11, body 13, label 15, big 20, title 28, huge 40`, scaled by `E.U`). `E.font(px)` picks among the firmware fonts (already promoted on big screens: 13/25/42 px line on the 480) and prefers the crisp native glyph over a blocky `textSize` blow-up. Avoid raw `size`: on a big screen the firmware has already doubled the font and `size: 3` on top gives 75-120 px titles.

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

The firmware sprite pool has had **8 slots** on PSRAM boards since API 29 (`System.spriteSlots()` reports the board's limit; engine 1.1 queries it automatically). `E.spr.load` decodes each PNG **once** into a slot and keeps a procedural `paint` fallback, so the game runs even with missing assets or on boards without sprites:

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
- More defs than slots → extras become painter-only automatically. `E.spr.backed(name)` tells you whether a sprite got a real slot (fast chroma-key blit) or will draw through its painter. `E.caps.slots` holds the probed limit (8 on API 29 PSRAM boards, 4 on older firmware) — budget your defs against it.

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
E.fx.flash(0xF800, 150);                         // edge glow (frame that fades)
E.fx.flash(0xFFFF, 400, { full: true });         // fullscreen flash (save it for the big moment)
// stride: N > 1 moves each star once every N frames (spread by index), so only
// n/N stars dirty a box per frame - a full-sky field otherwise makes the dirty
// union cover the whole screen again. Average speed is preserved.
var stars = E.fx.stars(60, { colors: [0x39E7, 0xC5F9], stride: 4 });  // parallax
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

Since API 32 sound effects are **mixed on top of the track** by the synth (`System.sfx`): `E.audio.sfx` does not block the loop and plays with music on (`E.caps.mix`). On older firmware there is one speaker slot — `playMusic` OR `playWav`/`playTone` — and `E.audio` falls back to the blocking `playTone`, polite with the music:

```js
E.audio.music({ bpm: 132, loops: 0, tracks: [
  { wave: "sq",  vol: 70, notes: [[69,4],[71,4],[72,4],[69,4]] },   // [midi, 16ths]
  { wave: "tri", vol: 60, drum: true, notes: [[36,2],[0,2],[38,2],[0,2]] },
]});
E.audio.beat();        // beat position (1.0 = downbeat); -1 when not playing
E.audio.sfx("coin");   // named sfx (API 32: mixed over the track; older: skipped while music plays)
E.audio.sfx([880, 60]);        // or a raw [freq, ms]
E.audio.sfx([[660,60],[0,20],[880,80]]); // or a short melody (freq 0 = rest; one effect at a time: the newest wins)
E.audio.stop(); E.audio.mute(true); E.audio.volume(80);
E.audio.duck(600);  // older firmware: mutes the track for ms (a loud sfx takes
                    // the channel) and resumes from where it stopped; no-op
                    // with E.caps.mix (the effect already sounds on top)
```

Music auto-restarts when its loops end (keep-alive). Spawn-on-the-beat: `if (Math.floor(E.audio.beat()) !== lastBeat) spawn()`. Resume after a pause: save `System.musicPos()` and call `E.audio.music(song, { startMs: pos })`. Default sfx table: `ui, ok, back, bad, hit, coin, boom, shot, power, over, record, win` — replace entries in `E.audio.sfxTable`. Boards without a speaker (CYD) no-op everything.

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

## 15. Physics (`celeros.physics`, optional)

**Most games need no physics module at all.** On a Duktape device an
object-per-body world pays GC pressure that grows with body count — the
store's own games learned it in production (Supernova dropped the module
when ~85 sensor bodies per frame brought GC stalls) and converged on the
same ladder. Climb it only as far as the game demands:

| Rung | Game style | Tool | Reference |
|---|---|---|---|
| **0 — none** | Arcade with a few dozen bodies or fewer: pong, breakout, runners, shooters | Pre-allocated pools + distance/AABB tests + reflection, by hand | Quica template, Supernova |
| **1 — Verlet** | Ropes, cloth, soft bodies, point sandboxes | `P.verletFast` → native `System.verlet*` (API 31, **every board**; falls back to the JS verlet on old firmware by itself) | Physics Drop |
| **2 — Rigid** | Stacks that topple, destruction, slingshots | `P.rigid` → native `System.rigid*` (API 33, S3 boards with PSRAM) | Arrasa! |

Pursuit/chase fields are **not** physics: `celeros.grid` (BFS) is the module
for that (Detna's chasers use it).

Coordinates everywhere: y grows **down** (screen); body `x, y` is the
**center**; circle bodies have `r`, boxes `w/h`. No `System` calls in the
math, so everything unit-tests anywhere.

### Rung 0: pools + reflection (the default)

Keep bodies in a pre-allocated array (`{x, y, vx, vy, r}` — plain fields,
nothing created per frame), integrate by hand, reflect on walls, and test
paddle/platform hits by **crossing**: the body crossed the surface when
`prevY + r <= top && newY + r > top` and it is horizontally within reach —
that is bulletproof against tunneling and needs no sub-steps. The Quica
template is the complete worked example; Supernova runs bullets/enemies in
pools with distance checks the same way. This rung is also the GC-friendliest:
zero allocations inside the frame.

### Rung 1: ropes / cloth / soft bodies (`P.verletFast`)

```js
var P = require("celeros.physics");
var w = P.verletFast({ iterations: 4, radius: 2.5 });   // radius > 0 = point bodies collide
var a = w.add(120, 30), b = w.add(120, 60);
w.stick(a, b);                    // len defaults to the current distance
w.pin(a);                         // nail a point in the air
w.step(dt, { gravity: { x: 0, y: 900 }, damp: 0.999,
             bounds: { x: 0, y: 0, w: 240, h: 320 }, bounce: 0.8 });
var xy = w.xy();                  // flat [x0, y0, x1, y1, ...]
```

On API 31+ the world lives in C++ floats outside the Duktape heap
(`w.native === true`); old firmware transparently runs the JS verlet with
the same interface (without point-point collision). Index-based access,
`delPoint`/`delStick` for surgery, `pins()` to read the nails. Full example:
Physics Drop (paint, grab-and-throw, pin, scissors, eraser).

### Rung 2: rigid bodies (`P.rigid`, API 33)

For castles of planks that tip over, wheels and "throw a rock at the tower"
games, `P.rigid()` drives the firmware's native rigid-body solver (API 33,
S3 boards; `System.rigid*`): rotating boxes and circles, friction,
restitution, sleeping stacks and anti-tunnel sub-steps. Index-based like
`verletFast`; `state()` returns 6 numbers per body (`x, y, angle, hit, speed,
flags`) and `hit` (the impact impulse of the last step) is what you turn into
damage. **No JS fallback**: it returns `null` on firmware without the
binding — tell the player to update.

```js
var w = P.rigid({ iterations: 10 });
w.box(160, 300, 400, 20, { static: true });                  // ground
var plank = w.box(200, 260, 48, 6, { density: 0.6, friction: 0.7 });
var rock = w.circle(40, 200, 6, { density: 3, bounce: 0.3 });
w.set(rock, 40, 200, 0, 420, -40, 0);                        // launch
w.step(dt, { gravity: { x: 0, y: 400 } });
var s = w.state();
if (w.hit(plank, s) / plankMass > 55) w.remove(plank);       // it broke
System.drawSprite(sprPlank, w.x(plank, s), w.y(plank, s), w.angle(plank, s) * 57.2958, 1, 1, 0);
```

`System.drawSprite` (also API 33) draws a sprite rotated and scaled around
its centre — the natural pair for rotating bodies; with `smooth` it also
resizes art once at load time to the board's screen. Full example:
`hub_apps/Arrasa` (slingshot vs. goblin fortresses).

### Static overlap: `P.hit`

`P.hit(a, b)` answers "do these two circles/boxes overlap right now" (all
combinations) — handy for pickups and UI-ish tests, no world involved.

### Legacy: `P.world`, `P.tiles`, `P.flow`

The 1.x module also ships an interpreted arcade world (`P.world` +
`P.tiles` tilemaps) and a BFS flow field (`P.flow`). They have no consumers
in the store today and new games should not adopt them: the interpreted
world is exactly the GC trap rung 0 avoids (its users migrated away —
Supernova to pools, Detna to `celeros.grid`), and pursuit fields live in
`celeros.grid`. They leave the module in 2.0.

### Recipes

- **Platformer:** rung 0 — hero AABB vs. the tilemap, move X then Y, land
  and jump when the Y pass finds floor; `grounded` is "the Y pass collided
  this frame". Camera `follow`s the hero.
- **Breakout:** rung 0 — the Quica pattern with bricks as a grid of alive
  flags; the ball's crossing test per brick edge is the same as the paddle.
- **Top-down shooter:** rung 0 — `vx/vy` from input with a drag factor;
  bullets/enemies in pools, distance checks for hits.
- **Bomberman:** grid state, not bodies — `celeros.grid` for solid/oneway
  queries and chaser paths; explosions walk the grid outward from the blast
  center, one soft block deep (this is Detna today).

## 16. Native canvas (fullscreen, API 28)

`E.init({ native: true })` switches drawing/touch to **physical glass pixels** (e.g. 480×480 on the SmartDisplay instead of the scaled 240×320) — crisper and faster for fullscreen games, but shapes are not uniform-scaled anymore. Requires `"topbar": false` in `app.json` (the request fails gracefully otherwise and `E.caps.native` stays false — always branch on it). `E.W/E.H` reflect the active mode.

## 16b. Rendering without flicker (engine 1.2)

The frame is **persistent** and the firmware pushes to the glass only the boxes you drew (up to 8 dirty boxes per frame since API 32 — before, the union of everything). So the cheapest frame is the one that touches the fewest pixels:

- **Menus:** `static: true` scenes (§4).
- **Action on a plain background:** `E.dirty.enable(bgColor)` in `enter`. Every engine draw (`E.gfx.*`, `E.spr.blit`, `E.fx`, text) records its screen box; next frame the engine repaints just those boxes with the background before calling your `draw`. No `fillScreen`. Direct `System.*` draws register with `E.dirty.add(x, y, w, h)`. `E.fx.stars` erases its own old pixels and only touches stars that moved (a star covered by something drawn over it heals on its next stride tick).
- **HUD:** `E.dirty.clip(x, y, w, h)` keeps erase and world drawing inside the arena; call `E.dirty.unclip()` and redraw the HUD only when its values change (keep a key string).
- **Tile worlds:** `E.tilemap({ cols, rows, cell, ox, oy, paint(c, r, x, y, w, h) })` keeps the scenery in the frame; `E.dirty.enable(function (x, y, w, h) { map.markRect(x, y, w, h); })` marks the cells under whatever moved, `map.mark(c, r)` the ones that changed (block broken), and `map.flush()` at the top of `draw` repaints only those. What `paint` draws is background — it never enters the dirty layer.
- `E.dirty.full()` repaints the whole background next frame (scene change, fullscreen flash do it for you); the layer turns itself off on every scene switch.

```js
jogo: {
  enter: function () { E.dirty.enable(0x0000); E.dirty.clip(0, HUD_H, W, H - HUD_H); },
  draw: function () {
    drawWorld();                 // E.gfx / E.spr — boxes recorded automatically
    E.fx.draw();
    E.dirty.unclip();
    if (hudKey() !== lastKey) drawHud();
  }
}
```

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

- Engine+game go over 48 KB (deps count toward the app ceiling!) → keep `"requires": ["psram"]`. The hub measures deps **as published**: `celerhub publish-dep` uploads them through `tools/sdk/lib/jsstrip.js` (1:1 port of the firmware's JsStripper — comments/indentation out, line breaks kept, so error lines still match the source), which is exactly what the device compiles. Today: `celeros.engine` 59 KB source → 33 KB; `celeros.physics` 30 → 17 KB — and rung-0 games (§15) skip it entirely.
- **No allocations per frame**: use `E.pool`, `swap-pop` removal, and reuse objects. A `new`/`[...]` per frame per entity is what triggers GC pauses. Since engine 1.2.3 the wrappers themselves don't allocate: opts-less `E.gfx.*` calls share one read-only object, `E.font` lookups are memoized and each string's `textWidth` is measured once per style — repeated HUD/score/button text costs no extra firmware calls.
- If you call `System.setTextDatum` directly, restore `0` when done: engine 1.2.3 only touches the datum when a text asks for a non-default origin, so `E.gfx.text` relies on apps leaving it at `0` (every app in this repo already does).
- Avoid full-screen `fillScreen` + redraw everything per frame: on the SmartDisplay's RGB panel (framebuffer scanned from PSRAM) it starves the LCD DMA and the image jitters. Use `static` scenes, `E.dirty` and `E.tilemap` (§16b).
- `world.step` is O(n²) on body count in the worst case, but a **sweep-and-prune** (bodies sorted by x each substep, early break on x distance) keeps it near-linear for spread-out scenes — shooters with ~40 bodies run comfortably.
- `E.audio.sfx` is non-blocking on API 32 (`System.sfx`); on older firmware it is **blocking** (`playTone`): keep melodies under ~300 ms.
- Pure-JS bursts longer than ~1 s trip the firmware exec-timeout — the `E.run` loop yields every frame, so stay inside it.

## 19. API cheat sheet

| Call | Does |
|---|---|
| `E.init(opts)` / `E.run(scenes, first)` | detect caps / own the loop |
| `E.goto(name)` / `E.quit()` | switch scene / leave the loop |
| `E.input`, `E.hit(r)`, `E.press(r)` | touch state, tap in rect, held in rect |
| `E.gfx.*`, `E.cam` | camera-aware drawing, scroll/shake/follow |
| `E.u(v)`, `E.U`, `E.font(px)`, `E.ts` | project-unit scale, pixel-height typography |
| `scene.static`, `E.redraw()` | menus that draw only when something changes |
| `E.dirty.enable/clip/unclip/add/full`, `E.tilemap` | erase-and-redraw only what changed |
| `E.group()`, `E.pool(n, f)` | entity lists, pre-allocated recycling |
| `E.spr.load/blit`, `E.anim(frames, fps)` | PNG sprites with painter fallback, flipbooks |
| `E.fx.burst/popText/flash/stars/draw` | juice |
| `E.tween/after/every/cancel/clear*` | engine-ticked time |
| `E.audio.music/beat/sfx/stop/mute/volume` | chiptune + polite sfx |
| `E.save.get/set/num/best` | NVS persistence |
| `E.m.*`, `E.rng(seed)` | math, deterministic RNG |
| `P.verletFast`, `P.rigid`, `P.hit` | the physics ladder: native Verlet, native rigid bodies, static overlap (optional module — §15) |

Editor autocomplete ships in the scaffold: `engine.d.ts` (engine + physics) alongside `celer.d.ts` (firmware API).
