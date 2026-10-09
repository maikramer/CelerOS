// engine.js — game engine para apps CelerOS (ES5, sobre a API JS; requer
// api >= 23). A engine POSSUI o loop: o app define cenas e chama E.run().
//
//   var E = require("engine");
//   E.init({ dir: "MeuJogo", fps: 30 });   // dir = pasta do app p/ assets
//   E.run({ titulo: { enter, update(dt), draw, exit }, jogo: { ... } },
//         "titulo");
//
// Sub-sistemas: E.input (gestos), E.gfx (desenho com camera), E.cam,
// E.spr/E.anim (sprites PNG + painters), E.fx (particulas/flash/estrelas),
// E.tween/E.after/E.every, E.audio (musica com beat + sfx), E.save (NVS),
// E.group/E.pool (entidades), E.m/E.rng (matematica). Fisica opcional e o
// modulo "physics". Recursos novos sao detectados em E.caps (sprites 12,
// keepAwake 13, smooth 22, playMusic 25, canvas nativo 28) — roda em toda
// placa e no harness/emu sem mudanca. Veja Documentation/Game_Engine_Guide.

var E = { version: '1.0.0' };
var S = System;

// ------------------------------------------------------- caps / init ------

E.caps = (function () {
    var info = null;
    try { info = S.getInfo(); } catch (e) {}
    return {
        psram: !!(info && info.totalPSRAM > 0),
        speaker: !!(info && info.hasSpeaker),
        music: typeof S.playMusic === 'function',
        png: typeof S.drawPNG === 'function',
        sprites: typeof S.createSprite === 'function',
        smooth: typeof S.fillSmoothCircle === 'function',
        round: typeof S.fillSmoothRoundRect === 'function',
        gradient: typeof S.fillGradient === 'function',
        arc: typeof S.fillArc === 'function',
        wide: typeof S.drawWideLine === 'function',
        native: false, w: 240, h: 320
    };
})();

E.theme = null;
E.W = 240;
E.H = 320;
E.dt = 0;
E.fps = 0;
E.data = {};            // bolsa compartilhada entre cenas
E.sceneName = '';

E.init = function (opts) {
    opts = opts || {};
    E.fpsTarget = opts.fps === undefined ? 30 : opts.fps;
    if (opts.native && typeof S.setNativeCanvas === 'function') {
        E.caps.native = !!S.setNativeCanvas(true);
    } else if (opts.native === false && E.caps.native) {
        S.setNativeCanvas(false);
        E.caps.native = false;
    }
    E.W = E.caps.w = S.screenWidth();
    E.H = E.caps.h = S.screenHeight();
    try { E.theme = S.theme(); } catch (e) { E.theme = null; }
    if (opts.keepAwake !== false && typeof S.keepAwake === 'function') {
        S.keepAwake(true);
    }
    E.save.prefix = opts.save || '';
    E.spr.bases = opts.bases ||
                  (opts.dir ? ['/local/apps/' + opts.dir + '/assets/',
                               '/sd/apps/' + opts.dir + '/assets/'] : []);
    E.fx._reset(opts.particles || 96);
    return E.caps;
};

// ------------------------------------------------------------- mat -------

E.m = {
    clamp: function (v, a, b) { return v < a ? a : (v > b ? b : v); },
    lerp: function (a, b, t) { return a + (b - a) * t; },
    map: function (v, a, b, c, d) { return c + (v - a) * (d - c) / (b - a); },
    rand: function (a, b) {
        if (a === undefined) { a = 0; b = 1; }
        else if (b === undefined) { b = a; a = 0; }
        return a + Math.random() * (b - a);
    },
    randInt: function (a, b) { return Math.floor(E.m.rand(a, b + 1)); },
    pick: function (arr) { return arr[Math.floor(Math.random() * arr.length)]; },
    dist: function (x0, y0, x1, y1) {
        var dx = x1 - x0, dy = y1 - y0;
        return Math.sqrt(dx * dx + dy * dy);
    },
    dist2: function (x0, y0, x1, y1) {
        var dx = x1 - x0, dy = y1 - y0;
        return dx * dx + dy * dy;
    },
    ang: function (x0, y0, x1, y1) { return Math.atan2(y1 - y0, x1 - x0); },
    approach: function (v, target, delta) {
        if (v < target) return v + delta > target ? target : v + delta;
        return v - delta < target ? target : v - delta;
    },
    wrap: function (v, a, b) {
        if (v < a) return b - (a - v) % (b - a);
        if (v >= b) return a + (v - a) % (b - a);
        return v;
    },
    sign: function (v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); },
    linear: function (t) { return t; },
    inQuad: function (t) { return t * t; },
    outQuad: function (t) { return t * (2 - t); },
    inOutQuad: function (t) { return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t; },
    outBack: function (t) {
        var s = 1.70158;
        t = t - 1;
        return t * t * ((s + 1) * t + s) + 1;
    }
};

// PRNG deterministico (xorshift32) — testes e mundo procedural.
E.rng = function (seed) {
    var a = (seed >>> 0) || 0x9E3779B9;
    return function () {
        a ^= a << 13;
        a >>>= 0;
        a ^= a >>> 17;
        a ^= a << 5;
        a >>>= 0;
        return a / 4294967296;
    };
};

// ----------------------------------------------------------- input -------

E.input = {
    x: 0, y: 0,            // toque atual (ou ultimo antes de soltar)
    down: false,
    justDown: false,
    justUp: false,
    moved: false,          // passou do limiar de arrasto desde o press
    dx: 0, dy: 0,          // delta desde o frame anterior
    holdDx: 0, holdDy: 0,  // delta desde o press
    tap: null,             // {x, y} — toque seco (< tapMs, sem arrastar)
    swipe: null,           // {dir, dx, dy, dist}
    longpress: false,      // >= longMs sem arrastar (1 tiro por toque)
    btn: 0,                // botao fisico (devkit): 1 curto, 2 longo
    dragThresh: 12,
    tapMs: 350,
    swipeMin: 30,
    longMs: 600,
    _raw: { down: false, sx: 0, sy: 0, t0: 0, moved: false, longDone: false }
};

E._pollInput = function (now) {
    var i = E.input, r = i._raw;
    i.justDown = false;
    i.justUp = false;
    i.dx = 0;
    i.dy = 0;
    i.tap = null;
    i.swipe = null;
    i.longpress = false;
    var t = S.getTouch();
    if (t.touched) {
        if (!r.down) {
            r.down = true;
            r.sx = t.x;
            r.sy = t.y;
            r.t0 = now;
            r.moved = false;
            r.longDone = false;
            i.justDown = true;
            i.holdDx = 0;
            i.holdDy = 0;
        } else {
            i.dx = t.x - i.x;
            i.dy = t.y - i.y;
        }
        i.x = t.x;
        i.y = t.y;
        i.down = true;
        i.holdDx = t.x - r.sx;
        i.holdDy = t.y - r.sy;
        if (!r.moved && (Math.abs(i.holdDx) > i.dragThresh ||
                         Math.abs(i.holdDy) > i.dragThresh)) r.moved = true;
        i.moved = r.moved;
        if (!r.moved && !r.longDone && now - r.t0 >= i.longMs) {
            r.longDone = true;
            i.longpress = true;
        }
    } else if (r.down) {
        r.down = false;
        i.down = false;
        i.justUp = true;
        if (!r.moved && now - r.t0 < i.tapMs) i.tap = { x: r.sx, y: r.sy };
        else if (r.moved) {
            var dx = i.x - r.sx, dy = i.y - r.sy;
            if (dx * dx + dy * dy >= i.swipeMin * i.swipeMin) {
                i.swipe = {
                    dx: dx, dy: dy, dist: Math.sqrt(dx * dx + dy * dy),
                    dir: Math.abs(dx) >= Math.abs(dy) ?
                         (dx > 0 ? 'right' : 'left') : (dy > 0 ? 'down' : 'up')
                };
            }
        }
    }
    i.btn = typeof S.button === 'function' ? S.button() : 0;
    return t;
};

// toque seco dentro do rect {x,y,w,h}
E.hit = function (b) {
    var t = E.input.tap;
    return !!t && t.x >= b.x && t.x <= b.x + b.w && t.y >= b.y && t.y <= b.y + b.h;
};

// dedo pressionado dentro do rect (feedback de botao apertado)
E.press = function (b) {
    var i = E.input;
    return i.down && i.x >= b.x && i.x <= b.x + b.w && i.y >= b.y && i.y <= b.y + b.h;
};

// ------------------------------------------------- cenas e loop ----------

E._scenes = null;
E._scene = null;
E._next = null;
E._quit = false;
E._last = 0;
E._fpsT = 0;
E._fpsN = 0;

E.goto = function (name) { E._next = name; };
E.quit = function () { E._quit = true; };

// roda cenas {enter, update(dt), draw, exit} ate E.quit(); ticka input,
// audio, camera, fx, timers e tweens antes do update da cena
E.run = function (scenes, first) {
    E._scenes = scenes;
    E._quit = false;
    E._next = null;
    var cur = null;

    function enter(name) {
        cur = scenes[name];
        E._scene = cur;
        E.sceneName = name;
        if (cur && cur.enter) cur.enter();
    }
    enter(first);
    E._last = S.millis();
    E._fpsT = E._last;
    E._fpsN = 0;

    while (!E._quit) {
        if (E._next !== null) {
            var n = E._next;
            E._next = null;
            if (cur && cur.exit) cur.exit();
            enter(n);
            E._last = S.millis();
        }
        var now = S.millis();
        var dt = (now - E._last) / 1000;
        E._last = now;
        if (dt > 0.1) dt = 0.1;
        if (dt < 0) dt = 0;
        E.dt = dt;
        E._fpsN++;
        if (now - E._fpsT >= 1000) {
            E.fps = E._fpsN;
            E._fpsN = 0;
            E._fpsT = now;
        }
        E._pollInput(now);
        E.audio._tick();
        E.cam._tick(dt);
        E.fx._tick(dt, now);
        E._tickTimers(now);
        E._tickTweens(now);
        if (cur && cur.update) cur.update(dt);
        if (E._quit) break;
        if (cur && cur.draw) cur.draw();
        // pacing: fps global, ou o da cena (cur.fps; 0 = sem teto)
        var fps = cur && cur.fps !== undefined ? cur.fps : E.fpsTarget;
        var frameMin = fps > 0 ? Math.floor(1000 / fps) : 0;
        var spent = S.millis() - now;
        S.delay(spent < frameMin ? frameMin - spent : 1);
    }
    if (cur && cur.exit) cur.exit();
    E._scene = null;
};

// -------------------------------------------------- timers e tweens ------

E._timers = [];
E.after = function (ms, fn) {
    var t = { at: S.millis() + ms, fn: fn, dead: false };
    E._timers.push(t);
    return t;
};
E.every = function (ms, fn) {
    var t = { at: S.millis() + ms, every: ms, fn: fn, dead: false };
    E._timers.push(t);
    return t;
};
E.cancel = function (t) { if (t) t.dead = true; };
// limpa tudo de uma vez (chame no exit da cena que criou timers/tweens)
E.clearTimers = function () { E._timers.length = 0; };
E.clearTweens = function () { E._tweens.length = 0; };
E._tickTimers = function (now) {
    for (var k = E._timers.length - 1; k >= 0; k--) {
        var t = E._timers[k];
        if (t.dead) { E._timers.splice(k, 1); continue; }
        if (now < t.at) continue;
        if (t.every) {
            t.at += t.every;
            if (t.at < now) t.at = now + t.every;
        } else {
            t.dead = true;
        }
        t.fn();
    }
};

// tween de props numericas: E.tween(obj, {x: 100}, 500, {ease, delay, onDone})
E._tweens = [];
E.tween = function (obj, to, ms, opts) {
    opts = opts || {};
    var from = {};
    for (var k in to) if (k in obj) from[k] = obj[k];
    var tw = {
        obj: obj, from: from, to: to, ms: ms < 1 ? 1 : ms,
        t0: S.millis() + (opts.delay || 0),
        ease: opts.ease || E.m.inOutQuad,
        onDone: opts.onDone || null,
        dead: false
    };
    E._tweens.push(tw);
    return tw;
};
E._tickTweens = function (now) {
    for (var k = E._tweens.length - 1; k >= 0; k--) {
        var tw = E._tweens[k];
        if (tw.dead) { E._tweens.splice(k, 1); continue; }
        if (now < tw.t0) continue;
        var p = (now - tw.t0) / tw.ms;
        var done = p >= 1;
        if (done) p = 1;
        var e = tw.ease(p);
        for (var kk in tw.to) {
            tw.obj[kk] = tw.from[kk] + (tw.to[kk] - tw.from[kk]) * e;
        }
        if (done) {
            tw.dead = true;
            if (tw.onDone) tw.onDone();
        }
    }
};

// ----------------------------------------------- grupos e pools ----------

// lista de entidades com update/draw em lote; dead = swap-pop
E.group = function () {
    var g = { items: [], count: 0 };
    g.add = function (o) { g.items.push(o); g.count = g.items.length; return o; };
    g.update = function (dt) {
        var a = g.items;
        for (var i = a.length - 1; i >= 0; i--) {
            var o = a[i];
            if (!o.dead && o.update) o.update(dt);
            if (o.dead) {
                a[i] = a[a.length - 1];
                a.pop();
            }
        }
        g.count = a.length;
    };
    g.draw = function () {
        var a = g.items;
        for (var i = 0; i < a.length; i++) {
            if (!a[i].dead && a[i].draw) a[i].draw();
        }
    };
    g.each = function (fn) {
        var a = g.items;
        for (var i = 0; i < a.length; i++) fn(a[i]);
    };
    g.clear = function () { g.items.length = 0; g.count = 0; };
    return g;
};

// pool pre-alocado: spawn() recicla objetos mortos (zero alloc por frame)
E.pool = function (n, factory) {
    var items = [];
    for (var i = 0; i < n; i++) {
        var o = factory(i);
        o.dead = true;
        items.push(o);
    }
    var p = {
        items: items, count: n, alive: 0,
        spawn: function () {
            for (var j = 0; j < items.length; j++) {
                if (items[j].dead) {
                    items[j].dead = false;
                    p.alive++;
                    return items[j];
                }
            }
            return null;
        },
        update: function (dt) {
            var n2 = 0;
            for (var j = 0; j < items.length; j++) {
                var o = items[j];
                if (o.dead) continue;
                if (o.update) o.update(dt);
                if (!o.dead) n2++;
            }
            p.alive = n2;
        },
        draw: function () {
            for (var j = 0; j < items.length; j++) {
                if (!items[j].dead && items[j].draw) items[j].draw();
            }
        },
        each: function (fn) {
            for (var j = 0; j < items.length; j++) {
                if (!items[j].dead) fn(items[j]);
            }
        },
        clear: function () {
            for (var j = 0; j < items.length; j++) items[j].dead = true;
            p.alive = 0;
        }
    };
    return p;
};

// --------------------------------------------------------- sprites -------

// pool do firmware tem 4 slots; o que nao couber vira painter procedural
E.spr = {
    bases: [],
    _slots: {},
    _used: 0,

    // defs: [{ name, file, w, h, paint }] — tenta PNG (em bases, prefixo por
    // app via init opts.dir) e cai no painter. paint(w, h, x, y) desenha em
    // coords absolutas.
    load: function (defs, opts) {
        var bases = (opts && opts.bases) || E.spr.bases;
        var out = {};
        for (var i = 0; i < defs.length; i++) {
            var d = defs[i];
            var slot = { id: 0, w: d.w, h: d.h, paint: d.paint || null };
            if (E.caps.sprites && E.spr._used < 4) {
                var id = S.createSprite(d.w, d.h);
                if (id) {
                    var ok = false;
                    S.useSprite(id);
                    S.fillScreen(0x0000);
                    if (E.caps.png && d.file) {
                        for (var b = 0; b < bases.length && !ok; b++) {
                            try { ok = !!S.drawPNG(bases[b] + d.file + '.png', 0, 0); }
                            catch (e) { ok = false; }
                        }
                    }
                    if (!ok && d.paint) {
                        d.paint(d.w, d.h, 0, 0);
                        ok = true;
                    }
                    S.useSprite(0);
                    if (ok) {
                        slot.id = id;
                        E.spr._used++;
                    } else {
                        S.deleteSprite(id);
                    }
                }
            }
            E.spr._slots[d.name] = slot;
            out[d.name] = slot;
        }
        return out;
    },

    has: function (name) { return !!E.spr._slots[name]; },

    // true se o sprite esta num slot real (blit com cor-chave rapido);
    // false = o blit vai cair no painter procedural
    backed: function (name) {
        var s = E.spr._slots[name];
        return !!(s && s.id);
    },

    // opts: { key: cor-chave (default preto), cx/cy: centralizar }
    blit: function (name, x, y, opts) {
        var s = E.spr._slots[name];
        if (!s) return;
        var dx = Math.round(opts && opts.cx ? x - s.w / 2 : x);
        var dy = Math.round(opts && opts.cy ? y - s.h / 2 : y);
        if (s.id) {
            S.useSprite(s.id);
            S.pushSprite(dx, dy, opts && opts.key !== undefined ? opts.key : 0x0000);
            S.useSprite(0);
        } else if (s.paint) {
            s.paint(s.w, s.h, dx, dy);
        }
    },

    free: function (name) {
        var s = E.spr._slots[name];
        if (!s) return;
        if (s.id) {
            S.deleteSprite(s.id);
            E.spr._used--;
        }
        delete E.spr._slots[name];
    },
    freeAll: function () {
        for (var n in E.spr._slots) E.spr.free(n);
    }
};

// flipbook: frames = painters function(x,y) ou nomes de sprite carregado
E.anim = function (frames, fps, loop) {
    return {
        frames: frames, fps: fps || 6, loop: loop !== false,
        t: 0, i: 0, done: false,
        tick: function (dt) {
            this.t += dt;
            var fi = Math.floor(this.t * this.fps);
            if (fi >= this.frames.length) {
                if (this.loop) {
                    fi = fi % this.frames.length;
                    this.t = fi / this.fps;
                } else {
                    fi = this.frames.length - 1;
                    this.done = true;
                }
            }
            this.i = fi;
        },
        draw: function (x, y, opts) {
            var f = this.frames[this.i];
            if (typeof f === 'function') f(x, y);
            else E.spr.blit(f, x, y, opts);
        }
    };
};

// ---------------------------------------------------------- camera -------

E.cam = {
    x: 0, y: 0,          // canto sup. esq. da camera no mundo
    ox: 0, oy: 0,        // offset do shake
    bounds: null,        // {x,y,w,h} do mundo
    _target: null,
    _lerp: 1,
    _shT: 0, _shDur: 0, _shPow: 0,

    follow: function (t, lerp) {
        this._target = t;
        this._lerp = lerp === undefined ? 1 : lerp;
    },
    shake: function (pow, dur) {
        this._shPow = pow;
        this._shDur = dur;
        this._shT = dur;
    },
    center: function (x, y) {
        this.x = x - E.W / 2;
        this.y = y - E.H / 2;
        this._clamp();
    },
    _clamp: function () {
        var b = this.bounds;
        if (!b) return;
        if (b.w <= E.W) this.x = (b.x + b.w / 2) - E.W / 2;
        else this.x = E.m.clamp(this.x, b.x, b.x + b.w - E.W);
        if (b.h <= E.H) this.y = (b.y + b.h / 2) - E.H / 2;
        else this.y = E.m.clamp(this.y, b.y, b.y + b.h - E.H);
    },
    _tick: function (dt) {
        if (this._target) {
            var k = E.m.clamp(this._lerp, 0, 1);
            this.x += (this._target.x - E.W / 2 - this.x) * k;
            this.y += (this._target.y - E.H / 2 - this.y) * k;
            this._clamp();
        }
        if (this._shT > 0) {
            this._shT -= dt;
            var p = this._shPow * (this._shT / this._shDur);
            this.ox = (Math.random() * 2 - 1) * p;
            this.oy = (Math.random() * 2 - 1) * p;
        } else {
            this.ox = 0;
            this.oy = 0;
        }
    },
    reset: function () {
        this.x = 0;
        this.y = 0;
        this.ox = 0;
        this.oy = 0;
        this._target = null;
        this._shT = 0;
    },
    wx: function (x) { return x - this.x + this.ox; },
    wy: function (y) { return y - this.y + this.oy; }
};

// ------------------------------------------------------------ gfx --------

// wrappers com camera: opts.screen = true desenha fora da camera (HUD)
E.gfx = (function () {
    function xy(x, y, screen) {
        if (screen) return [Math.round(x), Math.round(y)];
        return [Math.round(E.cam.wx(x)), Math.round(E.cam.wy(y))];
    }
    return {
        // rect(x,y,w,h,cor,{fill=true, r (cantos), screen})
        rect: function (x, y, w, h, color, o) {
            o = o || {};
            var p = xy(x, y, o.screen);
            if (o.r && E.caps.round) {
                S.fillSmoothRoundRect(p[0], p[1], Math.round(w), Math.round(h),
                                      Math.round(o.r), color);
            } else if (o.fill === false) {
                S.drawRect(p[0], p[1], Math.round(w), Math.round(h), color);
            } else {
                S.fillRect(p[0], p[1], Math.round(w), Math.round(h), color);
            }
        },
        // circle(x,y,r,cor,{fill=true, smooth, screen})
        circle: function (x, y, r, color, o) {
            o = o || {};
            var p = xy(x, y, o.screen);
            if (o.fill === false) S.drawCircle(p[0], p[1], Math.round(r), color);
            else if (o.smooth && E.caps.smooth) S.fillSmoothCircle(p[0], p[1], Math.round(r), color);
            else S.fillCircle(p[0], p[1], Math.round(r), color);
        },
        // line(x0,y0,x1,y1,cor,{w (espessura), screen})
        line: function (x0, y0, x1, y1, color, o) {
            o = o || {};
            var a = xy(x0, y0, o.screen), b = xy(x1, y1, o.screen);
            if (o.w > 1 && E.caps.wide) {
                S.drawWideLine(a[0], a[1], b[0], b[1], Math.round(o.w), color);
            } else {
                S.drawLine(a[0], a[1], b[0], b[1], color);
            }
        },
        tri: function (x0, y0, x1, y1, x2, y2, color, o) {
            o = o || {};
            var a = xy(x0, y0, o.screen), b = xy(x1, y1, o.screen), c = xy(x2, y2, o.screen);
            if (o.fill === false) {
                S.drawTriangle(a[0], a[1], b[0], b[1], c[0], c[1], color);
            } else {
                S.fillTriangle(a[0], a[1], b[0], b[1], c[0], c[1], color);
            }
        },
        gradient: function (x, y, w, h, c1, c2, o) {
            if (!E.caps.gradient) {
                E.gfx.rect(x, y, w, h, c1, o);
                return;
            }
            o = o || {};
            var p = xy(x, y, o.screen);
            S.fillGradient(p[0], p[1], Math.round(w), Math.round(h), c1, c2,
                           o.dir === 'x' ? 1 : 0);
        },
        arc: function (x, y, r0, r1, a0, a1, color, o) {
            if (!E.caps.arc) {
                E.gfx.circle(x, y, r1, color, o);
                return;
            }
            o = o || {};
            var p = xy(x, y, o.screen);
            S.fillArc(p[0], p[1], Math.round(r0), Math.round(r1),
                      Math.round(a0), Math.round(a1), color);
        },
        // texto: opts {color, bg, size (textSize), font (1..8), align
        // left|center|right, valign top|middle|bottom, screen}
        text: function (str, x, y, o) {
            o = o || {};
            var p = xy(x, y, o.screen);
            // bg ausente = chamada de 1 arg = fundo transparente (firmware)
            if (o.bg === undefined) {
                S.setTextColor(o.color === undefined ? 0xFFFF : o.color);
            } else {
                S.setTextColor(o.color === undefined ? 0xFFFF : o.color, o.bg);
            }
            var col = o.align === 'center' ? 1 : (o.align === 'right' ? 2 : 0);
            var row = o.valign === 'middle' ? 3 : (o.valign === 'bottom' ? 6 : 0);
            S.setTextDatum(row + col);
            if (o.size) S.setTextSize(o.size);
            S.drawString(String(str), p[0], p[1], o.font || 2);
            if (o.size) S.setTextSize(1);
            S.setTextDatum(0);
        },
        // botao immediate-mode: desenha e devolve o rect p/ E.hit()
        button: function (label, x, y, w, h, o) {
            o = o || {};
            var T = E.theme || {};
            var accent = o.color || T.accent || 0x07FF;
            var on = E.press({ x: x, y: y, w: w, h: h });
            var fill = o.primary === false ?
                (o.bg !== undefined ? o.bg : (T.card || 0x1082)) :
                (on ? accent : (E.caps.wide ? S.mixColor(accent, 0x0000, 25) : accent));
            E.gfx.rect(x, y, w, h, fill, { r: o.r === undefined ? 8 : o.r, screen: o.screen });
            E.gfx.text(label, x + w / 2, y + h / 2, {
                color: o.primary === false ? (T.text || 0xFFFF) : (T.onAccent || 0x0000),
                bg: fill, align: 'center', valign: 'middle',
                font: o.font || 2, screen: o.screen
            });
            return { x: x, y: y, w: w, h: h };
        },
        // medidor de fracao: opts {fg, bg, r, screen}
        bar: function (x, y, w, h, frac, o) {
            o = o || {};
            var T = E.theme || {};
            var f = E.m.clamp(frac, 0, 1);
            E.gfx.rect(x, y, w, h, o.bg || (T.stroke || 0x3186), { r: o.r, screen: o.screen });
            if (f > 0.01) {
                E.gfx.rect(x + 1, y + 1, Math.max(1, (w - 2) * f), h - 2,
                           o.fg || (T.accent || 0x07FF), { r: o.r, screen: o.screen });
            }
        },
        // painel/card com borda
        panel: function (x, y, w, h, o) {
            o = o || {};
            var T = E.theme || {};
            E.gfx.rect(x, y, w, h, o.bg || (T.card || 0x1082),
                       { r: o.r === undefined ? 10 : o.r, screen: o.screen });
            E.gfx.rect(x, y, w, h, o.stroke || (T.stroke || 0x3186),
                       { r: o.r === undefined ? 10 : o.r, fill: false, screen: o.screen });
        }
    };
})();

// ------------------------------------------------------------- fx --------

// particulas/floaters/flash tickados pelo loop; draw() por cima da cena
E.fx = {
    _parts: [],
    _cursor: 0,
    _floaters: [],
    _flashColor: 0,
    _flashT: 0,
    _flashDur: 1,

    _reset: function (max) {
        this._parts = [];
        for (var i = 0; i < max; i++) {
            this._parts.push({ dead: true, x: 0, y: 0, vx: 0, vy: 0, t: 0,
                               life: 1, size: 2, color: 0xFFFF, grav: 0,
                               drag: 0, shape: 'dot', grow: 30 });
        }
        this._floaters = [];
        this._flashT = 0;
    },

    // burst(x, y, {n, color|colors[], speed, speed2, angle, spread, life,
    //               size, grav, drag, shape dot|spark|ring})
    burst: function (x, y, o) {
        o = o || {};
        var n = o.n || 12;
        var sp = o.speed === undefined ? 60 : o.speed;
        var sp2 = o.speed2 === undefined ? sp : o.speed2;
        var a0 = o.angle === undefined ? 0 : o.angle;
        var spread = o.spread === undefined ? Math.PI * 2 : o.spread;
        for (var i = 0; i < n; i++) {
            var p = this._spawn();
            if (!p) break;
            var a = a0 + (spread >= Math.PI * 2 ?
                          Math.random() * spread : (Math.random() - 0.5) * spread);
            var v = E.m.lerp(sp, sp2, Math.random());
            p.x = x;
            p.y = y;
            p.vx = Math.cos(a) * v;
            p.vy = Math.sin(a) * v;
            p.t = 0;
            p.life = (o.life || 0.6) * (0.6 + Math.random() * 0.4);
            p.size = o.size || 2;
            p.color = o.colors ? E.m.pick(o.colors) :
                      (o.color === undefined ? 0xFFFF : o.color);
            p.grav = o.grav || 0;
            p.drag = o.drag || 0;
            p.shape = o.shape || 'dot';
        }
    },

    // onda de choque: anel que expande (speed px/s) e some
    ring: function (x, y, o) {
        o = o || {};
        var p = this._spawn();
        if (!p) return;
        p.x = x;
        p.y = y;
        p.vx = 0;
        p.vy = 0;
        p.t = 0;
        p.life = o.life || 0.55;
        p.size = o.r0 === undefined ? 6 : o.r0;
        p.grav = 0;
        p.drag = 0;
        p.color = o.color === undefined ? 0xFFFF : o.color;
        p.shape = 'ring';
        p.grow = o.speed === undefined ? 300 : o.speed;
    },

    // texto flutuante (score, dano) que sobe e some
    popText: function (x, y, str, o) {
        o = o || {};
        this._floaters.push({
            x: x, y: y, str: String(str),
            color: o.color === undefined ? 0xFFE0 : o.color,
            t: 0, life: o.life || 0.9, font: o.font || 1, screen: !!o.screen
        });
        if (this._floaters.length > 12) this._floaters.shift();
    },

    flash: function (color, ms) {
        this._flashColor = color;
        this._flashDur = (ms || 180) / 1000;
        this._flashT = this._flashDur;
    },

    // campo de estrelas parallax (2 camadas): guardado pelo chamador;
    // opts {w, h, vy, color} ou {colors: [perto, longe]}
    stars: function (n, o) {
        o = o || {};
        var w = o.w || E.W, h = o.h || E.H;
        var pts = [];
        for (var i = 0; i < n; i++) {
            pts.push({
                x: Math.random() * w, y: Math.random() * h,
                layer: i % 2 ? 1 : 0.45,
                color: o.colors ? o.colors[i % 2] :
                       (o.color === undefined ? 0x7BEF : o.color)
            });
        }
        return {
            update: function (dt) {
                for (var i = 0; i < pts.length; i++) {
                    var p = pts[i];
                    p.y += (o.vy === undefined ? 14 : o.vy) * p.layer * dt;
                    if (p.y >= h) {
                        p.y -= h;
                        p.x = Math.random() * w;
                    }
                }
            },
            draw: function () {
                for (var i = 0; i < pts.length; i++) {
                    S.drawPixel(Math.round(pts[i].x), Math.round(pts[i].y), pts[i].color);
                }
            }
        };
    },

    _spawn: function () {
        for (var i = 0; i < this._parts.length; i++) {
            var k = (this._cursor + i) % this._parts.length;
            if (this._parts[k].dead) {
                this._cursor = (k + 1) % this._parts.length;
                this._parts[k].dead = false;
                return this._parts[k];
            }
        }
        return null;
    },

    _tick: function (dt) {
        var ps = this._parts;
        for (var i = 0; i < ps.length; i++) {
            var p = ps[i];
            if (p.dead) continue;
            p.t += dt;
            if (p.t >= p.life) { p.dead = true; continue; }
            p.vy += p.grav * dt;
            if (p.drag > 0) {
                var d = 1 - p.drag * dt;
                if (d < 0) d = 0;
                p.vx *= d;
                p.vy *= d;
            }
            p.x += p.vx * dt;
            p.y += p.vy * dt;
        }
        var fs = this._floaters;
        for (var j = fs.length - 1; j >= 0; j--) {
            fs[j].t += dt;
            fs[j].y -= 18 * dt;
            if (fs[j].t >= fs[j].life) fs.splice(j, 1);
        }
        if (this._flashT > 0) this._flashT -= dt;
    },

    draw: function () {
        var ps = this._parts;
        for (var i = 0; i < ps.length; i++) {
            var p = ps[i];
            if (p.dead) continue;
            var k = 1 - p.t / p.life;
            var col = p.color;
            if (k < 0.5 && E.caps.wide) col = S.mixColor(p.color, 0x0000, Math.round(100 - k * 200));
            var x = Math.round(E.cam.wx(p.x)), y = Math.round(E.cam.wy(p.y));
            if (p.shape === 'spark') {
                E.gfx.line(p.x, p.y, p.x - p.vx * 0.05, p.y - p.vy * 0.05, col);
            } else if (p.shape === 'ring') {
                S.drawCircle(x, y, Math.round(p.size + p.t * p.grow), col);
            } else {
                S.fillCircle(x, y, Math.max(1, Math.round(p.size * k)), col);
            }
        }
        var fs = this._floaters;
        for (var j = 0; j < fs.length; j++) {
            var f = fs[j];
            var al = 1 - f.t / f.life;
            var fc = f.color;
            if (al < 0.4 && E.caps.wide) fc = S.mixColor(f.color, 0x0000, Math.round(100 - al * 250));
            E.gfx.text(f.str, f.x, f.y, { color: fc, align: 'center', font: f.font, screen: f.screen });
        }
        if (this._flashT > 0) {
            var q = Math.round(100 * (1 - this._flashT / this._flashDur));
            if (q < 70) {   // some antes de virar veu cinza: flash e curto
                var c = E.caps.wide ? S.mixColor(this._flashColor, 0x0000, q) : this._flashColor;
                S.fillRect(0, 0, E.W, E.H, c);
            }
        }
    }
};

// ----------------------------------------------------------- audio -------

E.audio = {
    muted: false,
    sfxOverMusic: false,   // true: toca sfx mesmo com musica (rouba o canal)
    sfxTable: {
        ui: [880, 40],
        ok: [[660, 60], [880, 70]],
        back: [330, 60],
        bad: [160, 120],
        hit: [220, 50],
        coin: [[988, 50], [1319, 80]],
        boom: [[120, 80], [70, 160]]
    },
    _song: null,
    _bpm: 120,

    // song = {bpm, loops, tracks:[...]} (API 25); opts {startMs} retoma
    music: function (song, opts) {
        if (!E.caps.music || E.audio.muted || !song) return false;
        E.audio._song = song;
        E.audio._bpm = song.bpm || 120;
        try {
            return !!S.playMusic(song, opts && opts.startMs !== undefined ?
                                 { startMs: opts.startMs } : undefined);
        } catch (e) {
            return false;
        }
    },
    stop: function () {
        E.audio._song = null;
        if (typeof S.musicStop === 'function') {
            try { S.musicStop(); } catch (e) {}
        }
    },
    playing: function () {
        if (!E.audio._song || typeof S.musicPlaying !== 'function') return false;
        try { return !!S.musicPlaying(); } catch (e) { return false; }
    },
    // fracao de batida (x.0 = tempo); -1 sem musica
    beat: function () {
        if (!E.audio._song || typeof S.musicPos !== 'function') return -1;
        var pos;
        try { pos = S.musicPos(); } catch (e) { return -1; }
        if (pos < 0) return -1;
        return pos / (60000 / E.audio._bpm);
    },
    // o que: nome da tabela, [f, ms] ou melodia [[f,ms],...] (bloqueante!)
    sfx: function (what) {
        if (E.audio.muted || !E.caps.speaker) return;
        if (E.audio.playing() && !E.audio.sfxOverMusic) return;
        var mel = typeof what === 'string' ? E.audio.sfxTable[what] : what;
        if (!mel) return;
        if (typeof mel[0] === 'number') mel = [mel];
        try { S.playTone(mel); } catch (e) {}
    },
    mute: function (on) {
        E.audio.muted = !!on;
        if (E.audio.muted) E.audio.stop();
    },
    volume: function (v) {
        if (typeof S.setVolume === 'function') {
            try { S.setVolume(E.m.clamp(Math.round(v), 0, 100)); } catch (e) {}
        }
    },
    _tick: function () {
        // reinicia a trilha quando os loops acabam
        if (E.audio._song && !E.audio.playing()) {
            try { S.playMusic(E.audio._song); } catch (e) {}
        }
    }
};

// ------------------------------------------------------------ save -------

E.save = {
    prefix: '',
    get: function (key, def) {
        try {
            var v = Storage.get(E.save.prefix + key, null);
            if (v === null || v === undefined || v === '') return def;
            return v;
        } catch (e) { return def; }
    },
    set: function (key, val) {
        try { Storage.set(E.save.prefix + key, String(val)); } catch (e) {}
    },
    num: function (key, def) {
        var v = parseFloat(E.save.get(key, def));
        return isNaN(v) ? def : v;
    },
    // true se virou novo recorde
    best: function (key, score) {
        var cur = E.save.num(key, 0);
        if (score > cur) {
            E.save.set(key, score);
            return true;
        }
        return false;
    }
};

module.exports = E;
