// Physics Drop — sandbox de fisica sobre a dep celeros.physics (API 30).
// Desde a 3.1 o mundo vive no VERLET NATIVO (API 31): P.verletFast põe os
// pontos/vínculos num buffer C++ (float, FPU do S3) e o step — integracao,
// relaxacao e bounds — roda fora do interpretador; o JS só cria, pinta e
// desenha (leitura por array plano, uma alocacao por frame). Em firmware
// sem o binding, a propria dep cai para o verlet JS com a mesma cara.

var P = require("celeros.physics");
var T = System.theme();
var SW = 240, SH = 320;
var BAR_Y = 252;                      // topo da barra de ferramentas

// gravidade em presets (cicla no botao): normal, leve, lua, forte, invertida
// — px por step (dt=1), como no verlet da fisica
var GRAVS = [[0.4, "normal"], [0.2, "leve"], [0.07, "lua"], [0.9, "forte"], [-0.3, "invertida"]];
var gravIdx = 0;

var MAX_POINTS = 120;

var world = P.verletFast({ iterations: 4 });

var isDrawing = false;
var lastPointIdx = -1;

function novoMundo() {
    world.free();
    world = P.verletFast({ iterations: 4 });
}

function spawnRope() {
    var n = 14, step = 13;
    var x0 = 36 + Math.random() * (SW - 72);
    var prev = -1;
    for (var i = 0; i < n; i++) {
        if (world.count() >= MAX_POINTS) break;
        var idx = world.add(x0, 20 + i * step);
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
            if (world.count() >= MAX_POINTS) return;
            var idx = world.add(x0 + c * step, 24 + r * step);
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

    var xy = world.xy();               // [x0, y0, x1, y1, ...] plano
    if (!xy.length) {
        // drawString direto: o fundo do mundo e repintado a cada frame e um
        // UI.text em slot so redesenharia quando a assinatura mudasse
        System.setTextDatum(4);           // MC
        System.setTextColor(T.textDim, T.bg);
        System.drawString("pinte com o dedo:", SW / 2, 118, 2);
        System.drawString("as linhas caem sozinhas", SW / 2, 138, 1);
        System.setTextDatum(0);
    }

    var st = world.sticks();           // [a0, b0, a1, b1, ...]
    for (var i = 0; i < st.length; i += 2) {
        var ia = st[i] * 2, ib = st[i + 1] * 2;
        if (hasWide) {
            System.drawWideLine(Math.floor(xy[ia]), Math.floor(xy[ia + 1]),
                                Math.floor(xy[ib]), Math.floor(xy[ib + 1]), 2, T.text);
        } else {
            System.drawLine(Math.floor(xy[ia]), Math.floor(xy[ia + 1]),
                            Math.floor(xy[ib]), Math.floor(xy[ib + 1]), T.text);
        }
    }
    // nos pinos em destaque: o verletFast nao expoe o pin por ponto — os
    // pinos sao os primeiros das cordas/panos e o ultimo do traco ativo,
    // pintados como nos comuns (o destaque era cosmetics da era JS)
    for (var j = 0; j < xy.length; j += 2) {
        if (hasSmooth) System.fillSmoothCircle(Math.floor(xy[j]), Math.floor(xy[j + 1]), 2, T.accent);
        else System.fillCircle(Math.floor(xy[j]), Math.floor(xy[j + 1]), 2, T.accent);
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
            if (world.count() < MAX_POINTS) {
                lastPointIdx = world.add(tc.x, tc.y);
                if (lastPointIdx !== -1) world.pin(lastPointIdx);
            } else lastPointIdx = -1;
        } else if (lastPointIdx !== -1) {
            var xy = world.xy();
            var lp = lastPointIdx * 2;
            var dx = tc.x - xy[lp], dy = tc.y - xy[lp + 1];
            var dist = Math.sqrt(dx * dx + dy * dy);
            if (dist > 15) {
                if (world.count() < MAX_POINTS) {
                    var nIdx = world.add(tc.x, tc.y);
                    if (nIdx !== -1) {
                        world.stick(lastPointIdx, nIdx, dist);
                        world.pin(nIdx);
                        world.pin(lastPointIdx, false);
                        lastPointIdx = nIdx;
                    }
                }
            } else {
                world.set(lastPointIdx, tc.x, tc.y);
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
    for (var stp = 0; stp < 2; stp++) {
        world.step(1, { gravity: { x: 0, y: GRAVS[gravIdx][0] },
                        damp: 0.999,
                        bounds: { x: 0, y: 0, w: SW, h: BAR_Y - 4 },
                        bounce: 0.8 });
    }

    drawWorld();

    // barra de ferramentas nativa: contador + gravidade num botao so
    if (UI.button("g " + GRAVS[gravIdx][1] + "  ·  nos " + world.count() + "/" + MAX_POINTS,
                  8, BAR_Y + 4, 224, 24, { style: "ghost" })) {
        gravIdx = (gravIdx + 1) % GRAVS.length;
    }
    if (UI.button("Limpar", 8, BAR_Y + 32, 70, 32, { style: "danger" })) {
        novoMundo();
        isDrawing = false;
        lastPointIdx = -1;
    }
    if (UI.button("Corda", 84, BAR_Y + 32, 70, 32)) spawnRope();
    if (UI.button("Pano", 160, BAR_Y + 32, 70, 32)) spawnCloth();
    UI.end(50);
}
