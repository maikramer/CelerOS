// Physics Drop — sandbox de fisica sobre a dep celeros.physics (P.verlet,
// API 30): a integracao de Verlet, vinculos e bounds com bounce sao da
// fisica compartilhada do hub; o app ficou com a pintura, os presets e a
// UI (toolkit API 22, botoes nativos embaixo). Pinte com o dedo, solte
// cordas e panos, brinque com a gravidade (ate invertida).

var P = require("celeros.physics");
var T = System.theme();
var SW = 240, SH = 320;
var BAR_Y = 252;                      // topo da barra de ferramentas

// gravidade em presets (cicla no botao): normal, leve, lua, forte, invertida
// — px por step (dt=1), como no verlet da fisica
var GRAVS = [[0.4, "normal"], [0.2, "leve"], [0.07, "lua"], [0.9, "forte"], [-0.3, "invertida"]];
var gravIdx = 0;

var MAX_POINTS = 120;
var world = P.verlet({ points: [], sticks: [], iterations: 4 });

var isDrawing = false;
var lastPointIdx = -1;

function addPoint(x, y) {
    if (world.points.length >= MAX_POINTS) return -1;
    world.points.push({ x: x, y: y, pin: false });
    return world.points.length - 1;
}

function spawnRope() {
    var n = 14, step = 13;
    var x0 = 36 + Math.random() * (SW - 72);
    var prev = -1;
    for (var i = 0; i < n; i++) {
        var idx = addPoint(x0, 20 + i * step);
        if (idx < 0) break;
        if (i === 0) world.pin(idx);
        if (prev >= 0) world.stick(prev, idx, step);
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
                world.pin(idx);
            }
            if (c > 0) world.stick(idx - 1, idx, step);
            if (r > 0) world.stick(idx - cols, idx, step);
        }
    }
}

// ------------------------------------------------------------ desenho -----
var hasWide = (typeof System.drawWideLine === "function");
var hasSmooth = (typeof System.fillSmoothCircle === "function");

function drawWorld() {
    System.fillRect(0, 0, SW, BAR_Y, T.bg);
    System.drawFastHLine(0, BAR_Y - 1, SW, T.stroke);

    if (!world.points.length) {
        // drawString direto: o fundo do mundo e repintado a cada frame e um
        // UI.text em slot so redesenharia quando a assinatura mudasse
        System.setTextDatum(4);           // MC
        System.setTextColor(T.textDim, T.bg);
        System.drawString("pinte com o dedo:", SW / 2, 118, 2);
        System.drawString("as linhas caem sozinhas", SW / 2, 138, 1);
        System.setTextDatum(0);
    }

    for (var i = 0; i < world.sticks.length; i++) {
        var s = world.sticks[i];
        var p0 = world.points[s.a], p1 = world.points[s.b];
        if (hasWide) {
            System.drawWideLine(Math.floor(p0.x), Math.floor(p0.y), Math.floor(p1.x), Math.floor(p1.y), 2, T.text);
        } else {
            System.drawLine(Math.floor(p0.x), Math.floor(p0.y), Math.floor(p1.x), Math.floor(p1.y), T.text);
        }
    }
    for (var j = 0; j < world.points.length; j++) {
        var p = world.points[j];
        var col = p.pin ? T.warn : T.accent;
        if (hasSmooth) System.fillSmoothCircle(Math.floor(p.x), Math.floor(p.y), 2, col);
        else System.fillCircle(Math.floor(p.x), Math.floor(p.y), 2, col);
    }
}

// ------------------------------------------------------------------ main ---
while (true) {
    UI.begin(T.bg);
    var tc = UI.touch();

    // pintura: o dedo cria a cadeia de nos/vinculos (area acima da barra);
    // o ultimo no segue o dedo preso (pin) e solta no fim do traco
    if (tc.down && tc.y < BAR_Y - 8) {
        if (!isDrawing) {
            isDrawing = true;
            lastPointIdx = addPoint(tc.x, tc.y);
            if (lastPointIdx !== -1) world.pin(lastPointIdx);
        } else if (lastPointIdx !== -1) {
            var lp = world.points[lastPointIdx];
            var dx = tc.x - lp.x, dy = tc.y - lp.y;
            var dist = Math.sqrt(dx * dx + dy * dy);
            if (dist > 15) {
                var nIdx = addPoint(tc.x, tc.y);
                if (nIdx !== -1) {
                    world.stick(lastPointIdx, nIdx, dist);
                    world.pin(nIdx);
                    lp.pin = false;
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
            world.pin(lastPointIdx, false);
            lastPointIdx = -1;
        }
    }

    // fisica: 2 steps por frame (o loop do toolkit corre a ~50 fps), com
    // dt=1 os presets de gravidade seguem em px por step; bounds com bounce
    for (var st = 0; st < 2; st++) {
        world.step(1, { gravity: { x: 0, y: GRAVS[gravIdx][0] },
                        damp: 0.999,
                        bounds: { x: 0, y: 0, w: SW, h: BAR_Y - 4 },
                        bounce: 0.8 });
    }

    drawWorld();

    // barra de ferramentas nativa: contador + gravidade num botao so
    if (UI.button("g " + GRAVS[gravIdx][1] + "  ·  nos " + world.points.length + "/" + MAX_POINTS,
                  8, BAR_Y + 4, 224, 24, { style: "ghost" })) {
        gravIdx = (gravIdx + 1) % GRAVS.length;
    }
    if (UI.button("Limpar", 8, BAR_Y + 32, 70, 32, { style: "danger" })) {
        world = P.verlet({ points: [], sticks: [], iterations: 4 });
        isDrawing = false;
        lastPointIdx = -1;
    }
    if (UI.button("Corda", 84, BAR_Y + 32, 70, 32)) spawnRope();
    if (UI.button("Pano", 160, BAR_Y + 32, 70, 32)) spawnCloth();
    UI.end(50);
}
