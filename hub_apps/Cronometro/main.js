// Cronometro — centesimos, voltas e intervalos. ES5 puro (Duktape), toolkit
// UI (API 22). Base de tempo: System.millis() acumulado; o tempo e as
// voltas sao UI.text — so o que muda vai ao vidro.

var T = System.theme();
var W = 240, H = 320;

var BTN_Y = 252, BTN_H = 48, BTN_W = 70, BTN_GAP = 6;
var BX = [8, 8 + BTN_W + BTN_GAP, 8 + 2 * (BTN_W + BTN_GAP)];  // Volta | Run | Zerar

var running = false;
var accMs = 0;          // tempo acumulado dos intervalos parados
var startMs = 0;        // System.millis() do ultimo start
var laps = [];          // totais no momento de cada volta

function elapsed() {
    return accMs + (running ? (System.millis() - startMs) : 0);
}

function fmt(ms) {
    var cs = Math.floor(ms / 10) % 100;
    var s = Math.floor(ms / 1000) % 60;
    var m = Math.floor(ms / 60000);
    function two(n) { return (n < 10 ? "0" : "") + n; }
    return two(m) + ":" + two(s) + "." + two(cs);
}

// ------------------------------------------------------------- desenho -----
function drawLaps() {
    UI.card(8, 122, W - 16, BTN_Y - 130);
    // melhor/pior split (so faz sentido com 3+ voltas)
    var bestI = -1, worstI = -1, best = 1e15, worst = -1;
    for (var k = 1; k < laps.length; k++) {
        var sp = laps[k] - laps[k - 1];
        if (sp < best) { best = sp; bestI = k; }
        if (sp > worst) { worst = sp; worstI = k; }
    }
    var start = Math.max(0, laps.length - 4);
    for (var row = 0; row < 4; row++) {
        var i = laps.length - 1 - row;
        var y = 132 + row * 28;
        if (i < start) {
            UI.text(row === 0 && !laps.length ? "as voltas aparecem aqui" : "", row === 0 && !laps.length ? 120 : 20, y + 4,
                    { role: "caption", align: row === 0 && !laps.length ? "center" : "left", color: T.textDim, id: 10 + row });
            UI.text("", W - 20, y, { align: "right", id: 20 + row });
            continue;
        }
        var total = laps[i];
        var split = total - (i > 0 ? laps[i - 1] : 0);
        var col = T.text;
        if (laps.length >= 3 && i === bestI && i > 0) col = T.ok;
        else if (laps.length >= 3 && i === worstI && i > 0) col = T.err;
        UI.text("V" + (i + 1) + "  +" + fmt(split), 20, y, { color: col, id: 10 + row });
        UI.text(fmt(total), W - 20, y, { align: "right", color: T.textDim, id: 20 + row });
    }
    UI.cardEnd();
}

// -------------------------------------------------------------- acoes ------
function toggleRun() {
    if (running) {
        accMs = elapsed();
        running = false;
    } else {
        startMs = System.millis();
        running = true;
    }
}

function lap() {
    if (!running) return;
    laps.push(elapsed());
}

function resetAll() {
    running = false;
    accMs = 0;
    laps = [];
}

// ------------------------------------------------------------------ main ---
while (true) {
    UI.begin(T.bg);
    // estado + tempo grande (centesimos ao vivo: so o texto muda)
    UI.badge(running ? "rodando" : "parado", 12, 14, { color: running ? T.ok : T.raised,
             textColor: running ? T.onAccent : T.textDim });
    UI.text("voltas: " + laps.length, W - 12, 16, { role: "caption", align: "right", color: T.textDim });
    UI.text(fmt(elapsed()), W / 2, 56, { role: "display", align: "center" });
    drawLaps();

    if (UI.button("Volta", BX[0], BTN_Y, BTN_W, BTN_H, { style: "ghost", disabled: !running })) lap();
    if (UI.button(running ? "Pausar" : "Iniciar", BX[1], BTN_Y, BTN_W, BTN_H)) toggleRun();
    if (UI.button("Zerar", BX[2], BTN_Y, BTN_W, BTN_H, { style: "ghost" })) resetAll();
    UI.end();
}
