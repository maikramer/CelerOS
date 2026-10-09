// engine.js — simulacao do combate: nave, tiros, drones (zigue-zague),
// sentinelas (miradas) e orbes de carga. Spawn sincronizado a batida da
// trilha: cada cruzamento de batida pode soltar um "comboio". Streak de
// abates + orbes carregam a SUPERNOVA (detonacao limpa a tela).
// Coordenadas em pixels FISICOS do canvas nativo; radios de colisao
// calibrados pelos tamanhos dos sprites (72/56/56/44).

var W = 0, H = 0;

var NAVE = 72, INIMIGO = 56, OLHO = 56, ORBE = 44;
var R_NAVE = 24, R_DRONE = 20, R_OLHO = 20, R_ORBE = 16;

var state = null;
var lastBeatInt = -1;

function reset() {
    state = {
        t: 0,
        player: { x: W / 2, y: H * 0.78, cool: 0, invuln: 1.2, alive: true },
        bullets: [],       // {x, y, vy, dmg}
        ebullets: [],      // {x, y, vx, vy}
        enemies: [],       // {kind:0 drone|1 sentinela, x, y, hp, ph, cool, vx}
        orbs: [],          // {x, y, ph}
        score: 0, lives: 3, charge: 0, streak: 0, streakT: 0,
        wave: 0, spawns: 0, intensity: 0,
        over: false, overT: 0, newRecord: false
    };
    lastBeatInt = -1;
}

function init(w, h) { W = w; H = h; reset(); }

// ------------------------------------------------------------- spawns ---
function spawnConvoy() {
    var n = 2 + Math.min(2, Math.floor(state.intensity / 2));
    var cx = 60 + Math.random() * (W - 120);
    for (var i = 0; i < n; i++) {
        state.enemies.push({
            kind: 0, x: cx + (i - (n - 1) / 2) * 78, y: -40 - i * 34,
            hp: 1, ph: Math.random() * Math.PI * 2, cool: 1.4 + Math.random() * 2.2,
            vx: 26 + state.intensity * 5
        });
    }
    state.spawns++;
    state.wave = 1 + Math.floor(state.spawns / 4);
}

function spawnSentinel() {
    state.enemies.push({
        kind: 1, x: 70 + Math.random() * (W - 140), y: -50,
        hp: 3 + Math.floor(state.intensity / 3), ph: Math.random() * Math.PI * 2,
        cool: 1.1 + Math.random(), vx: 0
    });
    state.spawns++;
    state.wave = 1 + Math.floor(state.spawns / 4);
}

function spawnOrb() {
    state.orbs.push({ x: 50 + Math.random() * (W - 100), y: -30, ph: 0 });
}

// ----------------------------------------------------------- simulacao ---
// dt s; beatF = audio.beat() (float, -1 sem musica); fx/audio injetados
function update(dt, beatF, fx, audio) {
    var s = state, i, j;
    if (s.over) { s.overT += dt; return; }   // tela de fim: so o relogio anda
    s.t += dt;
    s.intensity = Math.min(10, s.t / 14);

    // diretor de spawns: comboio a cada batida (chance sobe com a onda),
    // sentinela a cada ~9 s, orbe a cada ~12 s
    if (beatF >= 0) {
        var bi = Math.floor(beatF);
        if (bi !== lastBeatInt) {
            lastBeatInt = bi;
            var chance = 0.3 + s.intensity * 0.05;
            if (Math.random() < chance && s.enemies.length < 16) spawnConvoy();
        }
    } else if (s.enemies.length < 3 && Math.random() < dt * 0.5) {
        spawnConvoy();   // sem musica (fallback): relogio proprio
    }
    if (Math.random() < dt / 9) spawnSentinel();
    if (Math.random() < dt / 12) spawnOrb();

    // nave
    var p = s.player;
    if (p.alive) {
        p.cool -= dt;
        if (p.invuln > 0) p.invuln -= dt;
        if (p.cool <= 0) {
            p.cool = 0.16;
            s.bullets.push({ x: p.x - 14, y: p.y - 26, vy: -620, dmg: 1 });
            s.bullets.push({ x: p.x + 14, y: p.y - 26, vy: -620, dmg: 1 });
        }
        if (Math.random() < 0.7) fx.trail(p.x + (Math.random() * 10 - 5), p.y + 34, 0x07FF);
    }

    // tiros da nave
    for (i = s.bullets.length - 1; i >= 0; i--) {
        var b = s.bullets[i];
        b.y += b.vy * dt;
        if (b.y < -20) { s.bullets[i] = s.bullets[s.bullets.length - 1]; s.bullets.pop(); }
    }
    // tiros inimigos
    for (i = s.ebullets.length - 1; i >= 0; i--) {
        var eb = s.ebullets[i];
        eb.x += eb.vx * dt; eb.y += eb.vy * dt;
        if (eb.y > H + 20 || eb.x < -20 || eb.x > W + 20) {
            s.ebullets[i] = s.ebullets[s.ebullets.length - 1]; s.ebullets.pop();
            continue;
        }
        if (p.alive && p.invuln <= 0 &&
            Math.abs(eb.x - p.x) < R_NAVE && Math.abs(eb.y - p.y) < R_NAVE) {
            s.ebullets[i] = s.ebullets[s.ebullets.length - 1]; s.ebullets.pop();
            hitPlayer(fx, audio);
        }
    }

    // inimigos
    for (i = s.enemies.length - 1; i >= 0; i--) {
        var e = s.enemies[i];
        if (e.kind === 0) {                      // drone: desce em senoide
            e.y += (46 + s.intensity * 7) * dt;
            e.x += Math.sin(s.t * 2.2 + e.ph) * e.vx * dt * 2.4;
            e.cool -= dt;
            if (e.cool <= 0 && e.y > 30 && e.y < H * 0.7) {
                e.cool = 2.6 + Math.random() * 2.5 - s.intensity * 0.12;
                s.ebullets.push({ x: e.x, y: e.y + 22, vx: (p.x - e.x) * 0.22, vy: 150 + s.intensity * 8 });
            }
        } else {                                 // sentinela: paira e mira
            if (e.y < H * 0.24) e.y += 34 * dt;
            else e.x += Math.sin(s.t * 0.9 + e.ph) * 40 * dt;
            e.cool -= dt;
            if (e.cool <= 0 && e.y > 20) {
                e.cool = 1.7 + Math.random() * 1.2 - s.intensity * 0.08;
                var dx = p.x - e.x, dy = p.y - e.y;
                var d = Math.sqrt(dx * dx + dy * dy) || 1;
                var sp = 205 + s.intensity * 10;
                s.ebullets.push({ x: e.x, y: e.y + 16, vx: dx / d * sp, vy: dy / d * sp });
            }
        }
        if (e.x < 30) e.x = 30;
        if (e.x > W - 30) e.x = W - 30;

        // tiro da nave acerta?
        var dead = false;
        for (j = s.bullets.length - 1; j >= 0; j--) {
            var b2 = s.bullets[j];
            if (Math.abs(b2.x - e.x) < (e.kind ? R_OLHO : R_DRONE) + 4 &&
                Math.abs(b2.y - e.y) < (e.kind ? R_OLHO : R_DRONE) + 10) {
                s.bullets[j] = s.bullets[s.bullets.length - 1]; s.bullets.pop();
                e.hp -= b2.dmg;
                fx.burst(b2.x, b2.y + 6, 0xFFFF, 3, 70);
                if (e.hp <= 0) { dead = true; break; }
            }
        }
        if (dead) {
            var pts = e.kind === 1 ? 30 : 10;
            s.streak++;
            s.streakT = 2.2;
            var mult = 1 + Math.min(4, Math.floor(s.streak / 8));
            var tot = pts * mult;
            s.score += tot;
            fx.explosion(e.x, e.y, e.kind === 1 ? 0xF81F : 0xFD20, e.kind === 1);
            fx.floater(e.x, e.y - 10, "+" + tot, e.kind === 1 ? 0xF81F : 0xFFE0);
            s.charge = Math.min(100, s.charge + (e.kind === 1 ? 7 : 3));
            s.enemies[i] = s.enemies[s.enemies.length - 1]; s.enemies.pop();
            continue;
        }

        // bateu na nave?
        if (p.alive && p.invuln <= 0 &&
            Math.abs(e.x - p.x) < (e.kind ? R_OLHO : R_DRONE) + R_NAVE - 8 &&
            Math.abs(e.y - p.y) < (e.kind ? R_OLHO : R_DRONE) + R_NAVE - 8) {
            s.enemies[i] = s.enemies[s.enemies.length - 1]; s.enemies.pop();
            fx.explosion(e.x, e.y, 0xFD20, false);
            hitPlayer(fx, audio);
            continue;
        }
        if (e.y > H + 60) { s.enemies[i] = s.enemies[s.enemies.length - 1]; s.enemies.pop(); }
    }

    // orbes
    for (i = s.orbs.length - 1; i >= 0; i--) {
        var o = s.orbs[i];
        o.y += 64 * dt;
        o.ph += dt * 5;
        if (o.y > H + 30) { s.orbs[i] = s.orbs[s.orbs.length - 1]; s.orbs.pop(); continue; }
        if (p.alive && Math.abs(o.x - p.x) < R_ORBE + R_NAVE && Math.abs(o.y - p.y) < R_ORBE + R_NAVE) {
            s.orbs[i] = s.orbs[s.orbs.length - 1]; s.orbs.pop();
            s.charge = Math.min(100, s.charge + 25);
            s.score += 15;
            fx.shock(o.x, o.y, 0xFFE0, 300);
            fx.floater(o.x, o.y - 14, "+carga", 0xFFE0);
        }
    }

    if (s.streakT > 0) { s.streakT -= dt; if (s.streakT <= 0) s.streak = 0; }
}

function hitPlayer(fx, audio) {
    var s = state, p = s.player;
    s.lives--;
    s.streak = 0;
    audio.stop();
    fx.explosion(p.x, p.y, 0x07FF, true);
    fx.doFlash(0.5);
    if (s.lives <= 0) {
        p.alive = false;
        s.over = true;
        s.overT = 0;
        audio.sfx("over");
    } else {
        p.invuln = 2.2;
        audio.sfx("hit");
        audio.start(0);
    }
}

// detonacao da supernova: limpa a tela, pontos por cabeca, respingo no score
function detonate(fx, audio) {
    var s = state;
    var gained = 0;
    for (var i = 0; i < s.enemies.length; i++) {
        var e = s.enemies[i];
        gained += e.kind === 1 ? 45 : 15;
        fx.explosion(e.x, e.y, 0xFFE0, true);
    }
    s.enemies.length = 0;
    s.ebullets.length = 0;
    s.score += gained + 60;
    s.charge = 0;
    fx.supernova(s.player.x, s.player.y - 30);
    fx.floater(W / 2, H / 2 - 30, "SUPERNOVA! +" + gained, 0xFFE0);
    audio.stop();
    audio.sfx("boom");
    s.resumeAt = s.t + 1.6;   // main retoma a trilha (startMs) depois do boom
}

// chamado pelo main a cada frame: retoma a musica apos o boom
function musicPump(audio, now) {
    var s = state;
    if (s.resumeAt && s.t > s.resumeAt) {
        s.resumeAt = 0;
        audio.start(0);
    }
}

module.exports = {
    init: init, reset: reset, update: update, detonate: detonate, musicPump: musicPump,
    NAVE: NAVE, INIMIGO: INIMIGO, OLHO: OLHO, ORBE: ORBE,
    state: function() { return state; }
};
