// Cronometro — centesimos, voltas e intervalos (API 3)
// ES5 puro (Duktape). Base de tempo: System.millis() acumulado.

var T = System.theme();
var W = 240, H = 320;

var BTN_Y = 252, BTN_H = 48, BTN_W = 70, BTN_GAP = 6;
var BX = [8, 8 + BTN_W + BTN_GAP, 8 + 2 * (BTN_W + BTN_GAP)];  // Volta | Run | Zerar

var running = false;
var accMs = 0;          // tempo acumulado dos intervalos parados
var startMs = 0;        // System.millis() do ultimo start
var laps = [];          // totais no momento de cada volta
var needChrome = true;  // cabecalho/botoes mudaram

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
function ctext(s, cx, cy, f, col, bg) {
    System.setTextColor(col, bg);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), cy - 8, f);
}

function drawHeader() {
    // o nome vive na faixa do sistema (retratil); aqui e so o estado
    System.fillRoundRect(0, 0, W, 40, 0, T.card);
    System.fillCircle(20, 20, 4, running ? T.ok : T.textDim);
    var st = running ? "rodando" : "parado";
    System.setTextColor(T.text, T.card);
    System.drawString(st, 32, 12, 2);
    var right = "voltas: " + laps.length;
    System.setTextColor(T.textDim, T.card);
    System.drawString(right, W - 12 - System.textWidth(right, 1), 16, 1);
    System.fillRect(0, 40, W, 3, T.accent);
}

function drawTime() {
    System.fillRect(0, 50, W, 60, T.bg);
    var s = fmt(elapsed());
    System.setTextColor(T.text, T.bg);
    System.drawString(s, 120 - (System.textWidth(s, 4) >> 1), 66, 4);
    var info = "voltas: " + laps.length;
    System.setTextColor(T.textDim, T.bg);
    System.drawString(info, 120 - (System.textWidth(info, 1) >> 1), 100, 1);
}

function drawLaps() {
    System.fillRect(0, 118, W, BTN_Y - 126, T.bg);
    System.drawFastHLine(0, 118, W, T.stroke);
    // melhor/pior split (so faz sentido com 3+ voltas)
    var bestI = -1, worstI = -1, best = 1e15, worst = -1;
    for (var k = 1; k < laps.length; k++) {
        var sp = laps[k] - laps[k - 1];
        if (sp < best) { best = sp; bestI = k; }
        if (sp > worst) { worst = sp; worstI = k; }
    }
    var show = 4;
    var start = laps.length - show;
    if (start < 0) start = 0;
    var y = 128;
    for (var i = laps.length - 1; i >= start; i--) {
        var total = laps[i];
        var split = total - (i > 0 ? laps[i - 1] : 0);
        var line = "V" + (i + 1) + "  +" + fmt(split);
        var col = T.text;
        if (laps.length >= 3 && i === bestI && i > 0) col = T.ok;
        else if (laps.length >= 3 && i === worstI && i > 0) col = T.err;
        System.setTextColor(col, T.bg);
        System.drawString(line, 16, y, 2);
        var rt = fmt(total);
        System.setTextColor(T.textDim, T.bg);
        System.drawString(rt, W - 16 - System.textWidth(rt, 2), y, 2);
        y += 26;
    }
    if (!laps.length) {
        ctext("as voltas aparecem aqui", 120, 140, 1, T.textDim, T.bg);
    }
}

function drawButton(i, label, bg, fg, bd) {
    System.fillRoundRect(BX[i], BTN_Y, BTN_W, BTN_H, 8, bg);
    System.drawRoundRect(BX[i], BTN_Y, BTN_W, BTN_H, 8, bd);
    ctext(label, BX[i] + BTN_W / 2, BTN_Y + BTN_H / 2, 2, fg, bg);
}

function drawButtons() {
    drawButton(0, "Volta", running ? T.raised : T.card,
               running ? T.text : T.textDim, T.stroke);
    drawButton(1, running ? "Pausar" : "Iniciar", T.accent, T.onAccent, T.accent);
    drawButton(2, "Zerar", T.card, T.text, T.stroke);
}

function drawAll() {
    System.fillScreen(T.bg);
    drawHeader();
    drawTime();
    drawLaps();
    drawButtons();
    needChrome = false;
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
    needChrome = true;
}

function lap() {
    if (!running) return;
    laps.push(elapsed());
}

function resetAll() {
    running = false;
    accMs = 0;
    laps = [];
    needChrome = true;
}

// -------------------------------------------------------------- entrada ----
function hitButton(t) {
    if (t.y < BTN_Y || t.y > BTN_Y + BTN_H) return -1;
    for (var i = 0; i < 3; i++) {
        if (t.x >= BX[i] && t.x <= BX[i] + BTN_W) return i;
    }
    return -1;
}

function waitRelease() {
    var guard = System.millis();
    while (System.getTouch().touched) {
        if (System.millis() - guard > 3000) break;
        System.delay(10);
    }
}

// ------------------------------------------------------------------ main ---
drawAll();

while (true) {
    var t = System.getTouch();
    if (t.touched) {
        var b = hitButton(t);
        waitRelease();
        if (b === 0) lap();
        else if (b === 1) toggleRun();
        else if (b === 2) resetAll();
        else b = -1;
        if (b >= 0) drawAll();
    }

    if (needChrome) {
        drawAll();
    } else if (running) {
        drawTime();          // centesimos ao vivo
    }

    System.delay(30);
}
