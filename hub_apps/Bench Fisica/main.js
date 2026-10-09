// Bench Fisica — mede o verlet da celeros.physics nos DOIS caminhos:
// interpretado (P.verlet, JS puro no Duktape) e nativo (P.verletFast ->
// System.verletStep, JsPhysics.cpp — API 31), com a MESMA cena (cordas +
// pano + pontos soltos, bounds com bounce). Mostra ms totais, ms/step e o
// ganho — no device. Em firmware sem API 31 o nativo nao existe e o app
// mostra so a linha JS (nao e erro: e a comparacao dizendo que o
// acelerador nao esta la).

var P = require("celeros.physics");
var T = System.theme();

var STEPS = 2000;                     // resolucao do relogio (ms) pede volume
var temNativo = typeof System.verletNew === "function";

// a mesma cena dos dois lados: 3 cordas (14 nos, topo pinned), 1 pano
// (8x6, 3 pinos) e pontos soltos ate ~120 nos
function montar(v) {
    var i, j;
    for (var c = 0; c < 3; c++) {
        var x0 = 36 + c * 80, prev = -1;
        for (i = 0; i < 14; i++) {
            var idx = v.add(x0, 20 + i * 13);
            if (i === 0) v.pin(idx);
            if (prev >= 0) v.stick(prev, idx, 13);
            prev = idx;
        }
    }
    var cols = 8, rows = 6, x0 = (240 - cols * 15) / 2;
    for (var r = 0; r < rows; r++) {
        for (j = 0; j < cols; j++) {
            var p = v.add(x0 + j * 15, 24 + r * 15);
            if (r === 0 && (j === 0 || j === 4 || j === cols - 1)) v.pin(p);
            if (j > 0) v.stick(p - 1, p, 15);
            if (r > 0) v.stick(p - cols, p, 15);
        }
    }
    while (v.count() < 120) v.add(10 + Math.random() * 220, 60 + Math.random() * 160);
    return v;
}

// verlet JS: pontos como objetos (a API classica)
function cenarioJS() {
    var v = P.verlet({ iterations: 4 });
    var api = {
        count: function () { return v.points.length; },
        add: function (x, y) {
            v.points.push({ x: x, y: y, px: x, py: y, pin: false });
            return v.points.length - 1;
        },
        pin: function (i) { v.pin(i); },
        stick: function (a, b, len) { v.stick(a, b, len); }
    };
    return {
        setup: function () { montar(api); return v; },
        run: function (w) {
            for (var s = 0; s < STEPS; s++) {
                w.step(1, { gravity: { x: 0, y: 0.4 }, damp: 0.999,
                            bounds: { x: 0, y: 0, w: 240, h: 248 }, bounce: 0.8 });
            }
        },
        free: function () {}
    };
}

// verlet nativo (quando existe): mesmos numeros, mundo no buffer C++
function cenarioNativo() {
    var v = null;
    return {
        setup: function () { v = P.verletFast({ iterations: 4 }); montar(v); return v; },
        run: function () {
            for (var s = 0; s < STEPS; s++) {
                v.step(1, { gravity: { x: 0, y: 0.4 }, damp: 0.999,
                            bounds: { x: 0, y: 0, w: 240, h: 248 }, bounce: 0.8 });
            }
        },
        free: function () { v.free(); }
    };
}

var res = null;    // {nos, jsMs, natMs}
function rodar() {
    res = { nos: 0, jsMs: 0, natMs: 0 };
    var cj = cenarioJS();
    var wj = cj.setup();
    res.nos = wj.points.length;
    var t0 = System.millis();
    cj.run(wj);
    res.jsMs = System.millis() - t0;
    cj.free();
    if (temNativo) {
        var cn = cenarioNativo();
        cn.setup();
        var t1 = System.millis();
        cn.run();
        res.natMs = System.millis() - t1;
        cn.free();
    }
}

function fmtMs(ms) {
    var porStep = ms / STEPS;
    return ms + " ms (" + (porStep < 1 ? porStep.toFixed(2) : porStep.toFixed(1)) + " ms/step)";
}

while (true) {
    var full = UI.begin(T.bg);
    if (UI.header("Bench Fisica", { back: true })) System.exitApp();

    UI.card(10, 56, 220, 150);
    UI.text("Verlet da celeros.physics, " + (res ? res.nos : 120) + " nos, " +
            STEPS + " steps", 22, 70, { role: "caption", color: T.textDim });
    if (!res) {
        UI.text("Toque em Rodar para medir\nos dois caminhos.", 22, 100,
                { role: "title", w: 196, lines: 2 });
    } else {
        UI.text("JS (interpretado)", 22, 100, { role: "caption", color: T.textDim });
        UI.text(fmtMs(res.jsMs), 22, 118, { id: "b1" });
        if (temNativo) {
            UI.text("Nativo (C++, API 31)", 22, 146, { role: "caption", color: T.textDim });
            UI.text(fmtMs(res.natMs), 22, 164, { id: "b2" });
            var ganho = res.natMs > 0 ? (res.jsMs / res.natMs) : 0;
            UI.text(ganho >= 1 ? "nativo " + ganho.toFixed(1) + "x mais rapido" : "nativo no par do JS",
                    22, 190, { color: ganho >= 1 ? T.ok : T.warn, id: "b3" });
        } else {
            UI.text("Sem acelerador nativo\n(firmware < API 31).", 22, 146,
                    { role: "caption", w: 196, lines: 2, color: T.warn, id: "b2" });
        }
    }
    UI.cardEnd();

    if (UI.button("Rodar (" + STEPS + " steps)", 10, 216, 220, 40)) rodar();
    if (UI.button("Sair", 10, 264, 220, 36, { style: "ghost" })) System.exitApp();
    UI.end();
}
