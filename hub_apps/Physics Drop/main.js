// Physics Drop 4.0 — sandbox de fisica VERLET NATIVO (celeros.physics,
// API 31) com ferramentas de verdade no toque:
//   Pintar — arraste cria a cadeia de nos/vinculos (como sempre)
//   Pegar  — toque perto de um no o pega; arraste puxa (pano/corda
//            seguem); soltar arremessa com a velocidade do dedo
//   Pino   — toque seco alterna prender/soltar o no no ar
//   Cortar — arraste corta os vinculos que cruzarem o traco (tesoura)
//   Apagar — arraste apaga os nos que tocar (vinculos ligados saem)
// Os nos tem CORPO (colisao ponto-ponto nativa): corda cai sobre pano em
// vez de atravessar. Gravidade cicla no botao de status (ate invertida).

var P = require("celeros.physics");
var T = System.theme();
var SW = 240, SH = 320;
var BAR_Y = 236;                      // topo da barra de ferramentas

// gravidade em presets (cicla no botao): normal, leve, lua, forte, invertida
var GRAVS = [[0.4, "normal"], [0.2, "leve"], [0.07, "lua"], [0.9, "forte"], [-0.3, "invertida"]];
var gravIdx = 0;

var TOOLS = [
    { label: "Pinta", w: 42 },
    { label: "Pega", w: 42 },
    { label: "Pino", w: 42 },
    { label: "Corta", w: 42 },
    { label: "Apaga", w: 42 }
];
var T_DRAW = 0, T_GRAB = 1, T_PIN = 2, T_CUT = 3, T_ERASE = 4;
var tool = T_DRAW;

var MAX_POINTS = 120;
var GRAB_R = 22;                      // raio de pescar um no com o dedo
var ERASE_R = 16;

var world = P.verletFast({ iterations: 4, radius: 2.5 });   // nos com corpo

// pega de teste (so existe no harness): deixa o test.js dirigir as
// ferramentas e conferir o mundo
if (typeof __harness !== "undefined") {
    __harness.drop = {
        world: function () { return world; },
        tool: function () { return tool; },
        TOOLS: TOOLS,
        BAR_Y: BAR_Y
    };
}

// estados das ferramentas
var isDrawing = false, lastPointIdx = -1;   // Pintar
var grabbed = -1;                            // Pegar
var cutPrev = null;                          // Cortar (traco anterior)
var touchDown = false;

function novoMundo() {
    world.free();
    world = P.verletFast({ iterations: 4, radius: 2.5 });
    isDrawing = false;
    lastPointIdx = -1;
    grabbed = -1;
    cutPrev = null;
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

// no mais proximo de (x, y) dentro do raio (ou -1)
function nearest(x, y, r) {
    var xy = world.xy(), best = -1, bd = r * r;
    for (var i = 0; i < xy.length; i += 2) {
        var dx = x - xy[i], dy = y - xy[i + 1];
        var d2 = dx * dx + dy * dy;
        if (d2 <= bd) { bd = d2; best = i / 2; }
    }
    return best;
}

// intersecao de segmentos (traco do dedo x vinculo)
function segCross(ax, ay, bx, by, cx, cy, dx, dy) {
    var d1x = bx - ax, d1y = by - ay, d2x = dx - cx, d2y = dy - cy;
    var den = d1x * d2y - d1y * d2x;
    if (den === 0) return false;
    var t = ((cx - ax) * d2y - (cy - ay) * d2x) / den;
    var u = ((cx - ax) * d1y - (cy - ay) * d1x) / den;
    return t >= 0 && t <= 1 && u >= 0 && u <= 1;
}

// ------------------------------------------------------------ desenho -----
var hasWide = (typeof System.drawWideLine === "function");
var hasSmooth = (typeof System.fillSmoothCircle === "function");

function drawWorld(tc) {
    System.fillRect(0, 0, SW, BAR_Y, T.bg);
    System.drawFastHLine(0, BAR_Y - 1, SW, T.stroke);

    var xy = world.xy();
    if (!xy.length) {
        // drawString direto: o fundo do mundo e repintado a cada frame
        System.setTextDatum(4);           // MC
        System.setTextColor(T.textDim, T.bg);
        System.drawString("Pinte, ou solte Corda/Pano", SW / 2, 108, 2);
        System.drawString("Pegar arrasta e arremessa;", SW / 2, 132, 1);
        System.drawString("Cortar fatia; Pino fixa no ar", SW / 2, 146, 1);
        System.setTextDatum(0);
    }

    var pins = world.pins();
    var st = world.sticks();
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
    for (var j = 0; j < xy.length; j += 2) {
        var idx = j / 2;
        var col = pins[idx] ? T.warn : T.accent;
        var r = idx === grabbed ? 4 : 2;
        if (hasSmooth) System.fillSmoothCircle(Math.floor(xy[j]), Math.floor(xy[j + 1]), r, col);
        else System.fillCircle(Math.floor(xy[j]), Math.floor(xy[j + 1]), r, col);
    }
    // traco da tesoura em vermelho enquanto corta
    if (tool === T_CUT && cutPrev && tc && tc.down) {
        System.drawWideLine(Math.floor(cutPrev.x), Math.floor(cutPrev.y),
                            Math.floor(tc.x), Math.floor(tc.y), 2, T.err);
    }
}

// ------------------------------------------------------------------ main ---
while (true) {
    UI.begin(T.bg);
    var tc = UI.touch();
    var inWorld = tc.down && tc.y < BAR_Y - 6;

    if (tc.down && !touchDown) {
        // press: inicio de gesto por ferramenta
        touchDown = true;
        if (inWorld) {
            if (tool === T_DRAW) {
                isDrawing = true;
                if (world.count() < MAX_POINTS) {
                    lastPointIdx = world.add(tc.x, tc.y);
                    if (lastPointIdx !== -1) world.pin(lastPointIdx);
                } else lastPointIdx = -1;
            } else if (tool === T_GRAB) {
                grabbed = nearest(tc.x, tc.y, GRAB_R);
                if (typeof __harness !== "undefined") {
                    __harness.log.push('[grab] press em ' + tc.x.toFixed(0) + ',' + tc.y.toFixed(0) +
                                       ' -> ' + grabbed + ' (nos ' + world.count() + ')');
                }
            } else if (tool === T_PIN) {
                var pi = nearest(tc.x, tc.y, GRAB_R);
                if (pi >= 0) {
                    var pinsNow = world.pins();
                    world.pin(pi, !pinsNow[pi]);
                }
            } else if (tool === T_CUT) {
                cutPrev = { x: tc.x, y: tc.y };
            }
        }
    } else if (!tc.down && touchDown) {
        // release
        touchDown = false;
        if (tool === T_DRAW) {
            isDrawing = false;
            if (lastPointIdx !== -1) {
                world.pin(lastPointIdx, false);
                lastPointIdx = -1;
            }
        } else if (tool === T_GRAB) {
            grabbed = -1;   // soltou: a velocidade do dedo vira arremesso
        } else if (tool === T_CUT) {
            cutPrev = null;
        }
    }

    // arrasto por ferramenta
    if (inWorld) {
        if (tool === T_DRAW && isDrawing && lastPointIdx !== -1) {
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
        } else if (tool === T_GRAB && grabbed >= 0) {
            world.set(grabbed, tc.x, tc.y);
            if (typeof __harness !== "undefined" && __harness.log.length < 400) {
                var dxy = world.xy();
                __harness.log.push('[grab] drag dedo ' + tc.x.toFixed(0) + ',' + tc.y.toFixed(0) +
                                   ' | no1 ' + dxy[2].toFixed(0) + ',' + dxy[3].toFixed(0));
            }
        } else if (tool === T_CUT && cutPrev) {
            var st = world.sticks();
            var pts = world.xy();
            for (var ci = st.length / 2 - 1; ci >= 0; ci--) {   // do fim pro comeco (delStick faz swap)
                var a2 = st[ci * 2] * 2, b2 = st[ci * 2 + 1] * 2;
                if (segCross(cutPrev.x, cutPrev.y, tc.x, tc.y,
                             pts[a2], pts[a2 + 1], pts[b2], pts[b2 + 1])) {
                    world.delStick(ci);
                }
            }
            cutPrev = { x: tc.x, y: tc.y };
        } else if (tool === T_ERASE) {
            var exy = world.xy();          // uma leitura por frame
            for (var ei = world.count() - 1; ei >= 0; ei--) {   // do fim pro comeco (delPoint faz swap)
                var edx = tc.x - exy[ei * 2], edy = tc.y - exy[ei * 2 + 1];
                if (edx * edx + edy * edy <= ERASE_R * ERASE_R) world.delPoint(ei);
            }
            grabbed = -1;
        }
    }

    // fisica: 2 steps por frame, dt=1 mantem os presets em px por step
    for (var stp = 0; stp < 2; stp++) {
        world.step(1, { gravity: { x: 0, y: GRAVS[gravIdx][0] },
                        damp: 0.999,
                        bounds: { x: 0, y: 0, w: SW, h: BAR_Y - 4 },
                        bounce: 0.8 });
    }

    drawWorld(tc);

    // barra: status / ferramentas / acoes
    if (UI.button("g: " + GRAVS[gravIdx][1] + "  ·  " + world.count() + "/" + MAX_POINTS + " nós",
                  10, BAR_Y + 2, 220, 20, { style: "ghost" })) {
        gravIdx = (gravIdx + 1) % GRAVS.length;
    }
    var tx = 10;
    for (var ti = 0; ti < TOOLS.length; ti++) {
        var act = ti === tool;
        if (UI.button(TOOLS[ti].label, tx, BAR_Y + 26, TOOLS[ti].w, 26,
                      { style: act ? "primary" : "ghost" })) {
            tool = ti;
            grabbed = -1;
            cutPrev = null;
            isDrawing = false;
        }
        tx += TOOLS[ti].w + 2;
    }
    if (UI.button("Corda", 10, BAR_Y + 56, 70, 28)) spawnRope();
    if (UI.button("Pano", 84, BAR_Y + 56, 70, 28)) spawnCloth();
    if (UI.button("Limpar", 158, BAR_Y + 56, 72, 28, { style: "danger" })) novoMundo();
    UI.end(50);
}
