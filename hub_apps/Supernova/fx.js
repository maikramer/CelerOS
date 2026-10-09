// fx.js — juice: campo de estrelas em parallax (3 camadas, twinkle no
// beat), particulas, ondas de choque, tremor de tela e textos flutuantes.
// Tudo em primitivas fisicas (drawPixel/fillCircle/drawCircle): o custo
// por quadro e previsivel e a caixa suja do quadro faz o push so do que
// mudou. Piscar = mixColor ate o preto do espaco.

var W = 0, H = 0, T = null;

var stars = [];          // {x, y, spd, layer}
var parts = [];          // {x, y, vx, vy, life, max, col, r}
var waves = [];          // {x, y, r, vr, life, col}
var floats = [];         // {x, y, txt, life, col}
var shake = 0;           // magnitude decrescente do tremor
var flash = 0;           // flash branco de tela (supernova)

var LAYER_SPD = [18, 42, 90];
var LAYER_COL = [0x39E7, 0x630C, 0xC5F9];   // cinza-azulado -> branco quente
var MAX_PARTS = 90;

function init(w, h, theme) {
    W = w; H = h; T = theme;
    stars = [];
    var counts = [Math.round(W / 14), Math.round(W / 20), Math.round(W / 28)];
    for (var l = 0; l < 3; l++) {
        for (var i = 0; i < counts[l]; i++) {
            stars.push({ x: Math.random() * W, y: Math.random() * H,
                         spd: LAYER_SPD[l] * (0.8 + Math.random() * 0.4), layer: l });
        }
    }
    parts = []; waves = []; floats = [];
    shake = 0; flash = 0;
}

function addShake(mag) { if (shake < mag) shake = mag; }
function doFlash(a) { if (flash < a) flash = a; }

function burst(x, y, col, n, spd) {
    for (var i = 0; i < n && parts.length < MAX_PARTS; i++) {
        var a = Math.random() * Math.PI * 2;
        var v = spd * (0.3 + Math.random() * 0.9);
        parts.push({ x: x, y: y, vx: Math.cos(a) * v, vy: Math.sin(a) * v,
                     life: 0.45 + Math.random() * 0.55, max: 1, col: col,
                     r: 1 + Math.floor(Math.random() * 2) });
    }
}

function trail(x, y, col) {
    if (parts.length >= MAX_PARTS) return;
    parts.push({ x: x + (Math.random() * 6 - 3), y: y, vx: 0, vy: 40 + Math.random() * 60,
                 life: 0.18 + Math.random() * 0.2, max: 1, col: col, r: 1 });
}

function shock(x, y, col, vr) {
    waves.push({ x: x, y: y, r: 6, vr: vr, life: 0.55, col: col });
}

function floater(x, y, txt, col) {
    if (floats.length > 8) floats.shift();
    floats.push({ x: x, y: y, txt: txt, life: 0.9, col: col });
}

function explosion(x, y, col, big) {
    burst(x, y, col, big ? 26 : 14, big ? 170 : 120);
    burst(x, y, 0xFFFF, big ? 10 : 5, big ? 90 : 60);
    shock(x, y, col, big ? 420 : 260);
    addShake(big ? 8 : 3);
}

function supernova(x, y) {
    burst(x, y, 0xFFE0, 46, 300);
    burst(x, y, 0xFFFF, 30, 180);
    shock(x, y, 0xFFE0, 700);
    shock(x, y, 0x07FF, 520);
    addShake(14);
    doFlash(0.85);
}

// beatPulse: 0..1 — energie da batida (fracao alta logo apos o beat)
function update(dt, scroll, beatPulse) {
    var i, s;
    for (i = 0; i < stars.length; i++) {
        s = stars[i];
        s.y += (s.spd + scroll * (s.layer + 1) * 0.4) * dt;
        if (s.y > H) { s.y -= H; s.x = Math.random() * W; }
    }
    for (i = parts.length - 1; i >= 0; i--) {
        var p = parts[i];
        p.x += p.vx * dt; p.y += p.vy * dt;
        p.life -= dt;
        if (p.life <= 0) { parts[i] = parts[parts.length - 1]; parts.pop(); }
    }
    for (i = waves.length - 1; i >= 0; i--) {
        var wv = waves[i];
        wv.r += wv.vr * dt; wv.life -= dt;
        if (wv.life <= 0) { waves[i] = waves[waves.length - 1]; waves.pop(); }
    }
    for (i = floats.length - 1; i >= 0; i--) {
        var f = floats[i];
        f.y -= 34 * dt; f.life -= dt;
        if (f.life <= 0) { floats[i] = floats[floats.length - 1]; floats.pop(); }
    }
    if (shake > 0) shake = Math.max(0, shake - dt * 26);
    if (flash > 0) flash = Math.max(0, flash - dt * 2.6);
    return beatPulse;
}

var shakeX = 0, shakeY = 0;
// deslocamento do tremor deste quadro (chamar 1x por frame antes de desenhar)
function roll() {
    if (shake <= 0) { shakeX = 0; shakeY = 0; return; }
    shakeX = Math.round((Math.random() * 2 - 1) * shake);
    shakeY = Math.round((Math.random() * 2 - 1) * shake);
}

function drawStars(now, beatPulse) {
    for (var i = 0; i < stars.length; i++) {
        var s = stars[i];
        if (s.layer === 2 && ((now + i * 137) % 900) < 140 + beatPulse * 500) continue;  // pisca
        var col = LAYER_COL[s.layer];
        if (s.layer === 2 && beatPulse > 0.4) col = mixWhite(col, beatPulse);
        System.drawPixel(Math.round(s.x + shakeX), Math.round(s.y + shakeY), col);
    }
}

function mixWhite(c, k) {
    var r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = Math.min(31, r + Math.round(14 * k));
    g = Math.min(63, g + Math.round(28 * k));
    b = Math.min(31, b + Math.round(14 * k));
    return (r << 11) | (g << 5) | b;
}

function draw() {
    var i;
    for (i = 0; i < parts.length; i++) {
        var p = parts[i];
        var k = Math.max(0, Math.min(1, p.life / p.max));
        var col = System.mixColor(p.col, 0x0000, Math.round((1 - k) * 82));
        if (p.r > 1) System.fillCircle(Math.round(p.x + shakeX), Math.round(p.y + shakeY), p.r, col);
        else System.drawPixel(Math.round(p.x + shakeX), Math.round(p.y + shakeY), col);
    }
    for (i = 0; i < waves.length; i++) {
        var wv = waves[i];
        System.drawCircle(Math.round(wv.x + shakeX), Math.round(wv.y + shakeY),
                          Math.round(wv.r), System.mixColor(wv.col, 0x0000, Math.round((1 - wv.life / 0.55) * 70)));
    }
    for (i = 0; i < floats.length; i++) {
        var f = floats[i];
        System.setTextColor(System.mixColor(f.col, 0x0000, Math.round((1 - f.life) * 60)), 0x0000);
        System.setTextDatum(5);   // centro (mapa do firmware: MC=5, 4=middle-left)
        System.drawString(f.txt, Math.round(f.x + shakeX), Math.round(f.y + shakeY), 1);
    }
    System.setTextDatum(0);
}

function drawFlash() {
    if (flash <= 0) return;
    var k = Math.round(flash * 100);
    var c = System.mixColor(0x0000, 0xFFFF, k);
    System.fillRect(0, 0, W, H, c);
}

module.exports = {
    init: init, update: update, roll: roll,
    drawStars: drawStars, draw: draw, drawFlash: drawFlash,
    explosion: explosion, supernova: supernova, burst: burst, trail: trail,
    shock: shock, floater: floater, addShake: addShake, doFlash: doFlash,
    shakeX: function() { return shakeX; }, shakeY: function() { return shakeY; }
};
