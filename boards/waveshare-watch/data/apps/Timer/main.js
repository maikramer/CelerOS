// Timer — contagem regressiva em segundo plano (API 15)
// ES5 puro (Duktape). O tempo corre no firmware (System.setTimer): sair do
// app nao para nada; ao terminar, a tela de alarme do sistema toca.

var T = System.theme();
var INFO = {};
try { INFO = System.getInfo() || {}; } catch (e) { INFO = {}; }
var M = 12 + Math.round((INFO.inset || 0) / 2);   // margem lateral (cantos do vidro)

var PRESETS = [1, 3, 5, 10, 15, 30];   // minutos
var pick = 5 * 60;                      // segundos escolhidos (parado)
var last = "";

function pad2(n) { return (n < 10 ? "0" : "") + n; }
function fmt(s) {
    var h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, ss = s % 60;
    return (h > 0 ? h + ":" + pad2(m) : String(m)) + ":" + pad2(ss);
}
function ctext(s, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, Math.round((240 - System.textWidth(s, f)) / 2), y, f);
}
function hit(t, x, y, w, h) { return t.x >= x && t.x < x + w && t.y >= y && t.y < y + h; }
function button(x, y, w, h, label, bg, fg) {
    System.fillRoundRect(x, y, w, h, Math.round(h / 2), bg);
    System.setTextColor(fg);
    System.drawString(label, x + Math.round((w - System.textWidth(label, 2)) / 2), y + Math.round(h / 2) - 7, 2);
}

// geometria
var GW = Math.floor((240 - 2 * M - 12) / 3);
function presetRect(i) { return [M + (i % 3) * (GW + 6), 150 + Math.floor(i / 3) * 44, GW, 36]; }
var MINUS = [M, 82, 44, 44], PLUS = [240 - M - 44, 82, 44, 44];
var GO = [M + 20, 248, 240 - 2 * M - 40, 44];

function draw(st) {
    System.fillRect(0, 0, 240, 320, T.bg);
    if (st) {
        ctext(st.label ? st.label : "Timer", 50, 2, T.textDim);
        ctext(fmt(st.remaining), 100, 4, T.warn);
        ctext("roda em segundo plano", 150, 1, T.textDim);
        button(GO[0], GO[1], GO[2], GO[3], "Cancelar", T.card, T.text);
        return;
    }
    ctext("Novo timer", 50, 2, T.textDim);
    button(MINUS[0], MINUS[1], MINUS[2], MINUS[3], "-", T.card, T.text);
    button(PLUS[0], PLUS[1], PLUS[2], PLUS[3], "+", T.card, T.text);
    ctext(fmt(pick), 96, 4, T.text);
    for (var i = 0; i < PRESETS.length; i++) {
        var r = presetRect(i);
        var on = pick === PRESETS[i] * 60;
        button(r[0], r[1], r[2], r[3], PRESETS[i] + " min", on ? T.accentD : T.card, T.text);
    }
    button(GO[0], GO[1], GO[2], GO[3], "Iniciar", T.accent, T.onAccent);
}

function tap(t, st) {
    if (st) {
        if (hit(t, GO[0], GO[1], GO[2], GO[3])) System.cancelTimer();
        return;
    }
    if (hit(t, MINUS[0], MINUS[1], MINUS[2], MINUS[3])) pick = Math.max(30, pick - (pick > 300 ? 60 : 30));
    else if (hit(t, PLUS[0], PLUS[1], PLUS[2], PLUS[3])) pick = Math.min(86400, pick + (pick >= 300 ? 60 : 30));
    else if (hit(t, GO[0], GO[1], GO[2], GO[3])) {
        if (System.setTimer(pick, "Timer " + fmt(pick))) System.toast("Timer: " + fmt(pick));
    } else {
        for (var i = 0; i < PRESETS.length; i++) {
            var r = presetRect(i);
            if (hit(t, r[0], r[1], r[2], r[3])) pick = PRESETS[i] * 60;
        }
    }
}

if (typeof System.setTimer !== "function") {
    System.fillRect(0, 0, 240, 320, T.bg);
    ctext("Requer firmware API 15", 150, 2, T.warn);
    while (true) { System.getTouch(); System.delay(200); }
}

var wasDown = false;
while (true) {
    var st = System.getTimer();
    var sig = st ? "r" + st.remaining : "p" + pick;
    if (sig !== last) { draw(st); last = sig; }
    var t = System.getTouch();
    if (t.touched && !wasDown) { tap(t, st); last = ""; }
    wasDown = t.touched;
    System.delay(100);
}
