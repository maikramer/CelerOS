// Physics Drop — sandbox de fisica (integracao de Verlet). ES5 puro
// (Duktape), toolkit UI (API 22): a barra de ferramentas virou botoes
// nativos embaixo e a gravidade cicla num botao proprio. O mundo e
// desenhado direto no frame automatico (dirty box, zero flicker, sem
// sprite por fatias); vinculos com drawWideLine (API 22) e nos
// suavizados. Pinte com o dedo, solte cordas e panos, brinque com a
// gravidade (ate invertida).

var T = System.theme();
var SW = 240, SH = 320;
var BAR_Y = 252;                      // topo da barra de ferramentas

// gravidade em presets (cicla no botao): normal, leve, lua, forte, invertida
var GRAVS = [[0.4, "normal"], [0.2, "leve"], [0.07, "lua"], [0.9, "forte"], [-0.3, "invertida"]];
var gravIdx = 0;

var points = [];
var sticks = [];
var MAX_POINTS = 120;

var friction = 0.999;
var bounce = 0.8;

var isDrawing = false;
var lastPointIdx = -1;

function addPoint(x, y) {
    if (points.length >= MAX_POINTS) return -1;
    points.push({ x: x, y: y, oldx: x, oldy: y, pinned: false });
    return points.length - 1;
}

function spawnRope() {
    var n = 14, step = 13;
    var x0 = 36 + Math.random() * (SW - 72);
    var prev = -1;
    for (var i = 0; i < n; i++) {
        var idx = addPoint(x0, 20 + i * step);
        if (idx < 0) break;
        if (i === 0) points[idx].pinned = true;
        if (prev >= 0) addStick(prev, idx, step);
        prev = idx;
    }
}

function spawnCloth() {
    var cols = 8, rows = 6, step = 15;
    var x0 = (SW - cols * step) / 2;
    for (var r = 0; r < rows; r++) {
        for (var c = 0; c < cols; c++) {
            var idx = addPoint(x0 + c * step, 24 + r * step);
            if (idx < 0) return;
            if (r === 0 && (c === 0 || c === Math.floor(cols / 2) || c === cols - 1)) {
                points[idx].pinned = true;
            }
            if (c > 0) addStick(idx - 1, idx, step);
            if (r > 0) addStick(idx - cols, idx, step);
        }
    }
}

function addStick(p0, p1, length) {
    sticks.push({ p0: p0, p1: p1, length: length });
}

function getDistance(p0, p1) {
    var dx = p1.x - p0.x, dy = p1.y - p0.y;
    return Math.sqrt(dx * dx + dy * dy);
}

function updatePoints() {
    for (var i = 0; i < points.length; i++) {
        var p = points[i];
        if (p.pinned) continue;
        var vx = (p.x - p.oldx) * friction;
        var vy = (p.y - p.oldy) * friction;
        p.oldx = p.x;
        p.oldy = p.y;
        p.x += vx;
        p.y += vy;
        p.y += GRAVS[gravIdx][0];
    }
}

function updateSticks() {
    for (var i = 0; i < sticks.length; i++) {
        var s = sticks[i];
        var p0 = points[s.p0], p1 = points[s.p1];
        var dx = p1.x - p0.x, dy = p1.y - p0.y;
        var dist = Math.sqrt(dx * dx + dy * dy);
        if (dist === 0) continue;
        var percent = ((s.length - dist) / dist) / 2;
        var ox = dx * percent, oy = dy * percent;
        if (!p0.pinned) { p0.x -= ox; p0.y -= oy; }
        if (!p1.pinned) { p1.x += ox; p1.y += oy; }
    }
}

function constrainPoints() {
    for (var i = 0; i < points.length; i++) {
        var p = points[i];
        if (p.pinned) continue;
        var vx = (p.x - p.oldx) * friction;
        var vy = (p.y - p.oldy) * friction;
        if (p.x < 0) { p.x = 0; p.oldx = p.x + vx * bounce; }
        else if (p.x > SW) { p.x = SW; p.oldx = p.x + vx * bounce; }
        if (p.y < 0) { p.y = 0; p.oldy = p.y + vy * bounce; }
        else if (p.y > BAR_Y - 4) { p.y = BAR_Y - 4; p.oldy = p.y + vy * bounce; }
    }
}

// ------------------------------------------------------------ desenho -----
var hasWide = (typeof System.drawWideLine === "function");
var hasSmooth = (typeof System.fillSmoothCircle === "function");

function drawWorld() {
    System.fillRect(0, 0, SW, BAR_Y, T.bg);
    System.drawFastHLine(0, BAR_Y - 1, SW, T.stroke);

    if (!points.length) {
        // drawString direto: o fundo do mundo e repintado a cada frame e um
        // UI.text em slot so redesenharia quando a assinatura mudasse
        System.setTextDatum(4);           // MC
        System.setTextColor(T.textDim, T.bg);
        System.drawString("pinte com o dedo:", SW / 2, 118, 2);
        System.drawString("as linhas caem sozinhas", SW / 2, 138, 1);
        System.setTextDatum(0);
    }

    for (var i = 0; i < sticks.length; i++) {
        var s = sticks[i];
        var p0 = points[s.p0], p1 = points[s.p1];
        if (hasWide) {
            System.drawWideLine(Math.floor(p0.x), Math.floor(p0.y), Math.floor(p1.x), Math.floor(p1.y), 2, T.text);
        } else {
            System.drawLine(Math.floor(p0.x), Math.floor(p0.y), Math.floor(p1.x), Math.floor(p1.y), T.text);
        }
    }
    for (var j = 0; j < points.length; j++) {
        var p = points[j];
        var col = p.pinned ? T.warn : T.accent;
        if (hasSmooth) System.fillSmoothCircle(Math.floor(p.x), Math.floor(p.y), 2, col);
        else System.fillCircle(Math.floor(p.x), Math.floor(p.y), 2, col);
    }
}

// ------------------------------------------------------------------ main ---
while (true) {
    UI.begin(T.bg);
    var tc = UI.touch();

    // pintura: o dedo cria a cadeia de nos/vinculos (area acima da barra)
    if (tc.down && tc.y < BAR_Y - 8) {
        if (!isDrawing) {
            isDrawing = true;
            lastPointIdx = addPoint(tc.x, tc.y);
            if (lastPointIdx !== -1) points[lastPointIdx].pinned = true;
        } else if (lastPointIdx !== -1) {
            var lp = points[lastPointIdx];
            var dist = getDistance({ x: tc.x, y: tc.y }, lp);
            if (dist > 15) {
                var nIdx = addPoint(tc.x, tc.y);
                if (nIdx !== -1) {
                    addStick(lastPointIdx, nIdx, dist);
                    points[nIdx].pinned = true;
                    lp.pinned = false;
                    lastPointIdx = nIdx;
                }
            } else {
                lp.x = tc.x;
                lp.y = tc.y;
            }
        }
    } else if (isDrawing) {
        isDrawing = false;
        if (lastPointIdx !== -1) {
            points[lastPointIdx].pinned = false;
            lastPointIdx = -1;
        }
    }

    // fisica: 2 passos por frame (o loop do toolkit corre a ~50 fps)
    for (var st = 0; st < 2; st++) {
        updatePoints();
        for (var iter = 0; iter < 4; iter++) {
            updateSticks();
            constrainPoints();
        }
    }

    drawWorld();

    // barra de ferramentas nativa: contador + gravidade num botao so
    if (UI.button("g " + GRAVS[gravIdx][1] + "  ·  nos " + points.length + "/" + MAX_POINTS,
                  8, BAR_Y + 4, 224, 24, { style: "ghost" })) {
        gravIdx = (gravIdx + 1) % GRAVS.length;
    }
    if (UI.button("Limpar", 8, BAR_Y + 32, 70, 32, { style: "danger" })) {
        points = [];
        sticks = [];
        isDrawing = false;
        lastPointIdx = -1;
    }
    if (UI.button("Corda", 84, BAR_Y + 32, 70, 32)) spawnRope();
    if (UI.button("Pano", 160, BAR_Y + 32, 70, 32)) spawnCloth();
    UI.end(50);
}
