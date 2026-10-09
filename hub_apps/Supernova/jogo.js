// jogo.js — simulacao do combate do Supernova sobre a game engine do SDK:
// corpos SENSOR na fisica (celeros.physics) fazem a deteccao de acertos
// (tiro x inimigo, ram x nave, orbe x nave) e o movimento e roteado — cenas
// de acao nao querem empurra-empurra. A trilha continua sendo o metronomo:
// cada cruzamento de batida pode soltar um comboio. Novidade da 2.0:
// SENTINELA-MOR (chefe) a cada 5 ondas, com rajadas radiais na batida e
// barra de vida; combo x2..x5 por abates em serie; drones splitter que se
// dividem; tiro triplo de premiacao. Coordenadas em PIXELS FISICOS (canvas
// nativo).

var E = require("celeros.engine");
var P = require("celeros.physics");

// paleta do jogo (preto puro e a cor-chave dos sprites)
var CIANO = 0x07FF, CIANOD = 0x03EF, MAGENTA = 0xF81F, LARANJA = 0xFD20,
    OURO = 0xFFE0, BRANCO = 0xFFFF, VERDE = 0x07E0;

// grupos/mascaras de colisao (bitmask): tiro<->inimigo, inimigo/tiro<->nave,
// orbe<->nave; pares fora das mascaras nem entram no narrow phase
var G_PLAYER = 1, G_PBULLET = 2, G_ENEMY = 4, G_EBULLET = 8, G_ORB = 16;

var BPM = 132;

// Lá menor, 2 compassos de loop (bumbo/caixa/chimbal GM, baixo triangular,
// melodia quadrada e arpejo sq25 no contratempo).
var SONG = {
    bpm: BPM,
    loops: 8,
    tracks: [
        { drum: true, vol: 100, notes: [
            [36, 2], [42, 1], [42, 1], [38, 2], [42, 1], [42, 1],
            [36, 1], [36, 1], [42, 1], [42, 1], [38, 2], [42, 2],
            [36, 2], [42, 1], [42, 1], [38, 2], [36, 1], [36, 1],
            [42, 1], [42, 1], [38, 1], [38, 1], [42, 1], [42, 1], [42, 2]
        ] },
        { wave: "tri", vol: 92, notes: [
            [45, 2], [45, 2], [57, 2], [45, 2], [45, 2], [57, 2], [45, 2], [45, 2],
            [41, 2], [41, 2], [53, 2], [41, 2], [41, 2], [53, 2], [41, 2], [41, 2],
            [43, 2], [43, 2], [55, 2], [43, 2], [43, 2], [55, 2], [43, 2], [43, 2],
            [45, 2], [45, 2], [57, 2], [45, 2], [45, 2], [57, 2], [45, 2], [45, 2]
        ] },
        { wave: "sq", vol: 68, notes: [
            [69, 2], [0, 2], [72, 2], [0, 2], [76, 4], [74, 4],
            [72, 2], [0, 2], [69, 2], [0, 2], [71, 4], [69, 4],
            [72, 2], [0, 2], [76, 2], [0, 2], [79, 4], [77, 4],
            [76, 2], [74, 2], [72, 2], [71, 2], [69, 6], [0, 2]
        ] },
        { wave: "sq25", vol: 42, notes: [
            [57, 1], [60, 1], [64, 1], [60, 1], [57, 1], [64, 1], [60, 1], [64, 1],
            [53, 1], [57, 1], [60, 1], [57, 1], [53, 1], [60, 1], [57, 1], [60, 1],
            [55, 1], [59, 1], [62, 1], [59, 1], [55, 1], [62, 1], [59, 1], [62, 1],
            [57, 1], [60, 1], [64, 1], [60, 1], [57, 1], [64, 1], [60, 1], [64, 1]
        ] }
    ]
};

var W = 0, H = 0;
var world = null;
var S = null;
var lastBeatInt = -1;

function clampX(x) { return x < 30 ? 30 : (x > W - 30 ? W - 30 : x); }

function reset() {
    W = E.W;
    H = E.H;
    world = P.world({ gravity: { x: 0, y: 0 }, maxSub: 6 });
    S = {
        t: 0, world: world, player: null,
        score: 0, kills: 0, lives: 3, charge: 0,
        streak: 0, streakT: 0, mult: 1, multPulse: 0,
        wave: 0, spawns: 0, intensity: 0,
        boss: null, bossNext: 5, triple: 0,
        over: false, overT: 0, resumeAt: 0
    };
    var p = world.add({
        cat: 'player', group: G_PLAYER, mask: G_EBULLET | G_ENEMY,
        x: W / 2, y: H * 0.78, r: 24, sensor: true
    });
    p.cool = 0;
    p.invuln = 1.2;
    S.player = p;
    lastBeatInt = -1;
}

function state() { return S; }

function count(cat) {
    var n = 0, all = world.all;
    for (var i = 0; i < all.length; i++) if (all[i].cat === cat) n++;
    return n;
}

function movePlayer(dx) {
    S.player.x = clampX(S.player.x + dx);
}

// ------------------------------------------------------------- spawns ---

function spawnConvoy() {
    var s = S;
    if (s.boss) return;
    var n = 2 + Math.min(2, Math.floor(s.intensity / 2));
    var cx = 60 + Math.random() * (W - 120);
    for (var i = 0; i < n; i++) {
        var e = world.add({
            cat: 'enemy', kind: 0, group: G_ENEMY, mask: G_PLAYER | G_PBULLET,
            x: clampX(cx + (i - (n - 1) / 2) * 78), y: -40 - i * 34, r: 20,
            hp: 1, sensor: true, flashT: 0,
            ph: Math.random() * Math.PI * 2, cool: 1.4 + Math.random() * 2.2,
            vx: 26 + s.intensity * 5
        });
        e.upd = updDrone;
        e.onCollide = enemyCollide;
    }
    s.spawns++;
    s.wave = 1 + Math.floor(s.spawns / 4);
    if (s.wave >= s.bossNext) spawnBoss();
}

function spawnSentinel() {
    var e = world.add({
        cat: 'enemy', kind: 1, group: G_ENEMY, mask: G_PLAYER | G_PBULLET,
        x: clampX(70 + Math.random() * (W - 140)), y: -50, r: 20,
        hp: 3 + Math.floor(S.intensity / 3), sensor: true, flashT: 0,
        ph: Math.random() * Math.PI * 2, cool: 1.1 + Math.random()
    });
    e.upd = updSentinel;
    e.onCollide = enemyCollide;
    S.spawns++;
    S.wave = 1 + Math.floor(S.spawns / 4);
    if (S.wave >= S.bossNext) spawnBoss();
}

// splitter: desce rapido em senoide apertada e se divide em 2 minis
function spawnSplitter() {
    var e = world.add({
        cat: 'enemy', kind: 3, group: G_ENEMY, mask: G_PLAYER | G_PBULLET,
        x: clampX(60 + Math.random() * (W - 120)), y: -40, r: 20,
        hp: 2, sensor: true, flashT: 0,
        ph: Math.random() * Math.PI * 2
    });
    e.upd = updSplitter;
    e.onCollide = enemyCollide;
}

function spawnMini(x, ph) {
    var e = world.add({
        cat: 'enemy', kind: 4, group: G_ENEMY, mask: G_PLAYER | G_PBULLET,
        x: clampX(x), y: -20, r: 12, hp: 1, sensor: true, flashT: 0,
        ph: ph
    });
    e.upd = updMini;
    e.onCollide = enemyCollide;
}

function spawnMinis(e) {
    spawnMini(e.x - 22, e.ph);
    spawnMini(e.x + 22, e.ph + 1.5);
}

function spawnOrb(x) {
    var o = world.add({
        cat: 'orb', group: G_ORB, mask: G_PLAYER,
        x: x === undefined ? 50 + Math.random() * (W - 100) : clampX(x),
        y: -30, r: 16, sensor: true, ph: 0
    });
    o.upd = updOrb;
    o.onCollide = function (me, other) {
        if (me.dead || other.cat !== 'player') return;
        me.dead = true;
        S.charge = Math.min(100, S.charge + 25);
        S.score += 15;
        E.fx.ring(me.x, me.y, { color: OURO, speed: 300, life: 0.5 });
        E.fx.popText(me.x, me.y - 14, "+carga", { color: OURO });
    };
}

function spawnBoss() {
    var hp = 26 + S.wave * 3;
    var b = world.add({
        cat: 'enemy', kind: 2, group: G_ENEMY, mask: G_PLAYER | G_PBULLET,
        x: W / 2, y: -90, r: 46, hp: hp, hpMax: hp, sensor: true, flashT: 0
    });
    b.upd = updBoss;
    b.onCollide = enemyCollide;
    S.boss = b;
    E.fx.popText(W / 2, H * 0.3, "!! SENTINELA-MOR !!", { color: MAGENTA, life: 1.6 });
}

// ------------------------------------------------------ roteiro/movimento ---

function updDrone(e, dt) {
    e.y += (46 + S.intensity * 7) * dt;
    e.x += Math.sin(S.t * 2.2 + e.ph) * e.vx * dt * 2.4;
    e.cool -= dt;
    if (e.cool <= 0 && e.y > 30 && e.y < H * 0.7) {
        e.cool = 2.6 + Math.random() * 2.5 - S.intensity * 0.12;
        ebullet(e.x, e.y + 22, (S.player.x - e.x) * 0.22, 150 + S.intensity * 8);
    }
}

function updSentinel(e, dt) {
    if (e.y < H * 0.24) e.y += 34 * dt;
    else e.x += Math.sin(S.t * 0.9 + e.ph) * 40 * dt;
    e.cool -= dt;
    if (e.cool <= 0 && e.y > 20) {
        e.cool = 1.7 + Math.random() * 1.2 - S.intensity * 0.08;
        var dx = S.player.x - e.x, dy = S.player.y - e.y;
        var d = Math.sqrt(dx * dx + dy * dy) || 1;
        var sp = 205 + S.intensity * 10;
        ebullet(e.x, e.y + 16, dx / d * sp, dy / d * sp);
    }
}

function updSplitter(e, dt) {
    e.y += (70 + S.intensity * 8) * dt;
    e.x += Math.sin(S.t * 4 + e.ph) * 90 * dt;
}

function updMini(e, dt) {
    e.y += (150 + S.intensity * 10) * dt;
}

function updBoss(e, dt) {
    if (e.y < 110) {
        e.y += 40 * dt;
        return;
    }
    var fast = e.hp < e.hpMax / 2;
    e.x = W / 2 + Math.sin(S.t * (fast ? 0.9 : 0.55)) * (W * 0.3);
    if (fast) e.y = 110 + Math.sin(S.t * 1.3) * 14;
}

function updOrb(o, dt) {
    o.y += 64 * dt;
    o.ph += dt * 5;
}

// ------------------------------------------------------------- tiros ---

function pbullet(x, y, vx, vy) {
    var b = world.add({
        cat: 'pbullet', group: G_PBULLET, mask: G_ENEMY,
        x: x, y: y, r: 5, vx: vx, vy: vy, sensor: true, dead: false
    });
    b.onCollide = function (me, other) {
        if (me.dead || other.dead || other.killed) return;
        if (other.cat !== 'enemy') return;
        me.dead = true;
        other.hp -= 1;
        other.flashT = 0.09;
        E.fx.burst(me.x, me.y + 6, { n: 3, color: BRANCO, speed: 80, life: 0.3, size: 1 });
        if (other.hp <= 0) other.killed = true;
    };
    return b;
}

function ebullet(x, y, vx, vy) {
    var b = world.add({
        cat: 'ebullet', group: G_EBULLET, mask: G_PLAYER,
        x: x, y: y, r: 5, vx: vx, vy: vy, sensor: true, dead: false
    });
    b.onCollide = function (me, other) {
        if (me.dead) return;
        if (other.cat === 'player' && !S.over && S.player.invuln <= 0) {
            me.dead = true;
            hitPlayer();
        }
    };
    return b;
}

// inimigo que ramming a nave: explode junto
function enemyCollide(me, other) {
    if (me.dead || me.killed) return;
    if (other.cat === 'player' && !S.over && S.player.invuln <= 0) {
        me.dead = true;
        E.fx.burst(me.x, me.y, { n: 12, color: LARANJA, speed: 120, life: 0.5 });
        hitPlayer();
    }
}

// rajada radial do chefe, sincronizada na batida
function bossBurst() {
    var b = S.boss;
    if (!b || b.y < 60) return;
    var fast = b.hp < b.hpMax / 2;
    var n = fast ? 14 : 9;
    var sp = 130 + S.intensity * 8 + (fast ? 40 : 0);
    var a0 = Math.random() * Math.PI;
    for (var i = 0; i < n; i++) {
        var a = a0 + i * Math.PI * 2 / n;
        ebullet(b.x + Math.cos(a) * 44, b.y + Math.sin(a) * 44,
                Math.cos(a) * sp, Math.sin(a) * sp);
    }
    E.fx.ring(b.x, b.y, { color: MAGENTA, speed: 500, life: 0.4 });
}

// ----------------------------------------------------------- simulacao ---

function update(dt) {
    var s = S;
    if (s.over) {
        s.overT += dt;
        return;
    }
    s.t += dt;
    s.intensity = Math.min(10, s.t / 14);
    if (s.triple > 0) s.triple -= dt;
    if (s.multPulse > 0) s.multPulse -= dt;

    // diretor: comboio no cruzamento de batida; chefe comanda a tela
    var beatF = E.audio.beat();
    if (beatF >= 0) {
        var bi = Math.floor(beatF);
        if (bi !== lastBeatInt) {
            lastBeatInt = bi;
            if (s.boss) bossBurst();
            else {
                var chance = 0.3 + s.intensity * 0.05;
                if (Math.random() < chance && count('enemy') < 14) spawnConvoy();
            }
        }
    } else if (!s.boss && count('enemy') < 3 && Math.random() < dt * 0.5) {
        spawnConvoy();   // sem musica (fallback): relogio proprio
    }
    if (!s.boss) {
        if (Math.random() < dt / 9) spawnSentinel();
        if (Math.random() < dt / 11) spawnSplitter();
        if (Math.random() < dt / 12) spawnOrb();
    } else if (Math.random() < dt / 14) {
        spawnOrb();      // orbe de alivio durante o chefe
    }

    // nave: tiro duplo (triplo com o poder do chefe) + rastro no motor
    var p = s.player;
    p.cool -= dt;
    if (p.invuln > 0) p.invuln -= dt;
    if (p.cool <= 0) {
        p.cool = 0.16;
        pbullet(p.x - 14, p.y - 26, 0, -620);
        pbullet(p.x + 14, p.y - 26, 0, -620);
        if (s.triple > 0) {
            pbullet(p.x, p.y - 30, -130, -600);
            pbullet(p.x, p.y - 30, 130, -600);
        }
    }
    if (Math.random() < 0.7) {
        E.fx.burst(p.x + (Math.random() * 10 - 5), p.y + 34,
                   { n: 1, color: CIANO, speed: 50, speed2: 100,
                     angle: Math.PI / 2, spread: 0.5, life: 0.28, size: 1 });
    }

    // roteia os corpos e roda a fisica (so deteccao: todos sao sensor)
    var all = world.all;
    for (var i = 0; i < all.length; i++) {
        var b = all[i];
        if (b.flashT > 0) b.flashT -= dt;
        if (b.upd && !b.dead && !b.killed) b.upd(b, dt);
        if (b.cat === 'enemy' && b.kind !== 2) b.x = clampX(b.x);
    }
    world.step(dt);
    sweep();

    if (s.streakT > 0) {
        s.streakT -= dt;
        if (s.streakT <= 0 && s.streak > 0) {
            s.streak = 0;
            s.mult = 1;
        }
    }
    // retoma a trilha depois do boom da supernova
    if (s.resumeAt && s.t > s.resumeAt) {
        s.resumeAt = 0;
        E.audio.music(SONG);
    }
}

// varre os corpos: mortos, marcados como abate e fora da tela
function sweep() {
    var all = world.all;
    for (var i = all.length - 1; i >= 0; i--) {
        var b = all[i];
        if (b.cat === 'pbullet') {
            if (b.dead || b.y < -24) world.remove(b);
        } else if (b.cat === 'ebullet') {
            if (b.dead || b.y > H + 24 || b.x < -24 || b.x > W + 24) world.remove(b);
        } else if (b.cat === 'orb') {
            if (b.dead || b.y > H + 30) world.remove(b);
        } else if (b.cat === 'enemy') {
            if (b.killed) killEnemy(b);
            else if (b.dead) {
                explosionFx(b);
                world.remove(b);
            } else if (b.y > H + 60) world.remove(b);
        }
    }
}

function explosionFx(e) {
    var big = e.kind === 1 || e.kind === 2 || e.kind === 3;
    var col = (e.kind === 1 || e.kind === 2) ? MAGENTA : (e.kind === 3 ? OURO : LARANJA);
    E.fx.burst(e.x, e.y, { n: big ? 26 : 14, color: col, speed: big ? 170 : 120, life: 0.7 });
    E.fx.burst(e.x, e.y, { n: big ? 10 : 5, color: BRANCO, speed: 90, life: 0.4, size: 1 });
    E.fx.ring(e.x, e.y, { color: col, speed: big ? 420 : 260, life: 0.55 });
    // tremor so em evento grande: drone comum abatido em serie sacudia a
    // tela o tempo todo (a "vibracao" que o player sentia)
    if (big) E.cam.shake(5, 0.3);
}

function killEnemy(e) {
    var s = S;
    var pts = e.kind === 2 ? 300 : (e.kind === 1 ? 30 : (e.kind === 3 ? 20 : (e.kind === 4 ? 5 : 10)));
    s.streak++;
    s.streakT = 2.2;
    s.kills++;
    var m = 1 + Math.min(4, Math.floor(s.streak / 8));
    if (m > s.mult) {
        s.mult = m;
        s.multPulse = 1.2;
        E.fx.popText(s.player.x, s.player.y - 50, "COMBO x" + m, { color: OURO, life: 1.1 });
    }
    var tot = pts * s.mult;
    s.score += tot;
    if (e.kind !== 4) {
        s.charge = Math.min(100, s.charge + (e.kind === 2 ? 40 : (e.kind === 1 ? 7 : 3)));
    }
    explosionFx(e);
    E.fx.popText(e.x, e.y - 10, "+" + tot, { color: e.kind === 1 ? MAGENTA : OURO });
    if (e.kind === 3) spawnMinis(e);
    if (e.kind === 2) bossDown(e);
    world.remove(e);
}

function bossDown(e) {
    var s = S;
    s.boss = null;
    s.bossNext += 5;
    s.triple = 10;
    E.fx.flash(BRANCO, 350);
    E.fx.ring(e.x, e.y, { color: MAGENTA, speed: 700, life: 0.7, r0: 20 });
    E.cam.shake(8, 0.4);
    spawnOrb(e.x - 50);
    spawnOrb(e.x + 50);
    E.fx.popText(W / 2, H * 0.4, "CHEFE AO CHAO! TIRO TRIPLO", { color: MAGENTA, life: 1.4 });
}

function hitPlayer() {
    var s = S, p = s.player;
    s.lives--;
    s.streak = 0;
    s.mult = 1;
    E.audio.stop();
    E.fx.burst(p.x, p.y, { n: 26, color: CIANO, speed: 170, life: 0.8 });
    E.fx.ring(p.x, p.y, { color: CIANO, speed: 420, life: 0.55 });
    E.fx.flash(BRANCO, 220);
    E.cam.shake(6, 0.35);
    if (s.lives <= 0) {
        s.over = true;
        s.overT = 0;
        E.audio.sfx("over");
    } else {
        p.invuln = 2.2;
        E.audio.sfx("hit");
        E.audio.music(SONG);
    }
}

// detonacao da SUPERNOVA: limpa a tela (o chefe apanha 15 em vez de morrer),
// pontos por cabeca, duas ondas de choque e flash; a trilha volta depois
function detonate() {
    var s = S;
    if (s.over || s.charge < 100) return false;
    var gained = 0;
    var all = world.all;
    var i, b;
    for (i = 0; i < all.length; i++) {
        b = all[i];
        if (b.cat !== 'enemy' || b.dead || b.killed) continue;
        if (b.kind === 2) {
            b.hp -= 15;
            b.flashT = 0.3;
            gained += 60;
            if (b.hp <= 0) b.killed = true;
        } else {
            gained += b.kind === 1 ? 45 : 15;
            b.killed = true;
            E.fx.burst(b.x, b.y, { n: 20, color: OURO, speed: 150, life: 0.7 });
        }
    }
    for (i = all.length - 1; i >= 0; i--) {
        b = all[i];
        if (b.cat === 'enemy' && b.killed) {
            if (b.kind === 2) bossDown(b);
            world.remove(b);
        } else if (b.cat === 'ebullet') {
            world.remove(b);
        }
    }
    s.score += gained + 60;
    s.charge = 0;
    E.fx.burst(s.player.x, s.player.y - 30, { n: 46, color: OURO, speed: 300, life: 0.9 });
    E.fx.burst(s.player.x, s.player.y - 30, { n: 30, color: BRANCO, speed: 180, life: 0.6 });
    E.fx.ring(s.player.x, s.player.y - 30, { color: OURO, speed: 700, life: 0.7, r0: 10 });
    E.fx.ring(s.player.x, s.player.y - 30, { color: CIANO, speed: 520, life: 0.7, r0: 10 });
    E.fx.flash(BRANCO, 500);
    E.cam.shake(8, 0.45);
    E.fx.popText(W / 2, H / 2 - 30, "SUPERNOVA! +" + gained, { color: OURO, life: 1.4 });
    E.audio.stop();
    E.audio.sfx("boom");
    s.resumeAt = s.t + 1.6;
    return true;
}

function startMusic() {
    E.audio.music(SONG);
}

// teste deterministico (harness): tiro inimigo em cima da nave
function debugHit() {
    var p = S.player;
    ebullet(p.x, p.y, 0, 120);
}

module.exports = {
    reset: reset, update: update, detonate: detonate,
    state: state, movePlayer: movePlayer,
    SONG: SONG, startMusic: startMusic, debugHit: debugHit, BPM: BPM
};
