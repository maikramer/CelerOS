// Atividade — passos do dia x meta e historico de 7 dias (API 15)
// ES5 puro (Duktape). Dados do pedometro do firmware: Sensors.steps() (dia
// corrente) e Sensors.stepHistory() (dias fechados, mais recente primeiro).
// Meta em System.setting("step_goal") — editavel em Ajustes > Relogio.

var T = System.theme();
var INFO = {};
try { INFO = System.getInfo() || {}; } catch (e) { INFO = {}; }
var M = 12 + Math.round((INFO.inset || 0) / 2);
var DIA = ["D", "S", "T", "Q", "Q", "S", "S"];
var PASSO_M = 0.75;      // passada media (m)
var KCAL_PASSO = 0.04;

function ctext(s, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, Math.round((240 - System.textWidth(s, f)) / 2), y, f);
}
function goal() {
    var g = 0;
    try { g = parseInt(System.setting("step_goal"), 10); } catch (e) { g = 0; }
    return g > 0 ? g : 8000;
}
// yyyymmdd -> dia da semana (Zeller simplificado via Date UTC)
function weekday(ymd) {
    var y = Math.floor(ymd / 10000), m = Math.floor(ymd / 100) % 100, d = ymd % 100;
    return new Date(Date.UTC(y, m - 1, d)).getUTCDay();
}
function fmtKm(steps) {
    var km = steps * PASSO_M / 1000;
    return (km < 10 ? km.toFixed(2) : km.toFixed(1)).replace(".", ",") + " km";
}

function draw(steps, hist, g) {
    System.fillRect(0, 0, 240, 320, T.bg);
    if (steps < 0) {
        ctext("Sem sensor de movimento", 150, 2, T.warn);
        return;
    }
    // hoje
    ctext("Hoje", 14, 1, T.textDim);
    ctext(String(steps), 34, 4, steps >= g ? T.ok : T.text);
    ctext("de " + g + " passos", 70, 1, T.textDim);
    var bw = 240 - 2 * M, pw = Math.min(bw, Math.round(bw * steps / g));
    System.fillRoundRect(M, 88, bw, 10, 5, T.card);
    if (pw > 8) System.fillRoundRect(M, 88, pw, 10, 5, steps >= g ? T.ok : T.accent);
    System.setTextColor(T.textDim);
    System.drawString(fmtKm(steps), M, 108, 2);
    var kc = Math.round(steps * KCAL_PASSO) + " kcal";
    System.drawString(kc, 240 - M - System.textWidth(kc, 2), 108, 2);

    // 7 dias: hoje + ate 6 fechados (mais antigo a esquerda)
    var days = [];
    for (var i = Math.min(hist.length, 6) - 1; i >= 0; i--) days.push(hist[i]);
    days.push({ date: 0, steps: steps });
    var max = g;
    for (i = 0; i < days.length; i++) if (days[i].steps > max) max = days[i].steps;
    var top = 150, bot = 270, n = 7, colW = Math.floor((240 - 2 * M) / n);
    ctext("Últimos dias", 132, 1, T.textDim);
    // linha da meta
    var gy = bot - Math.round((bot - top) * g / max);
    System.drawFastHLine(M, gy, 240 - 2 * M, T.stroke);
    var off = n - days.length;
    for (i = 0; i < days.length; i++) {
        var x = M + (off + i) * colW + 4, w = colW - 8;
        var h = Math.max(2, Math.round((bot - top) * days[i].steps / max));
        var isToday = i === days.length - 1;
        System.fillRoundRect(x, bot - h, w, h, 3,
            isToday ? T.accent : (days[i].steps >= g ? T.ok : T.raised));
        var lab = isToday ? "H" : DIA[weekday(days[i].date)];
        System.setTextColor(isToday ? T.accent : T.textDim);
        System.drawString(lab, x + Math.round((w - System.textWidth(lab, 1)) / 2), bot + 6, 1);
    }
    var ok = 0;
    for (i = 0; i < hist.length; i++) if (hist[i].steps >= g) ok++;
    if (hist.length) ctext("meta batida em " + ok + " de " + hist.length + " dias", 292, 1, T.textDim);
}

var hasHist = typeof Sensors !== "undefined" && Sensors && typeof Sensors.stepHistory === "function";
var last = "";
var hist = hasHist ? Sensors.stepHistory() : [];
var g = goal();
var tick = 0;
while (true) {
    var steps = (typeof Sensors !== "undefined" && Sensors) ? Sensors.steps() : -1;
    if (++tick % 100 === 0) {  // ~20 s: virada do dia / meta nova
        hist = hasHist ? Sensors.stepHistory() : [];
        g = goal();
    }
    var sig = steps + "|" + g + "|" + hist.length;
    if (sig !== last) { draw(steps, hist, g); last = sig; }
    System.getTouch();
    System.delay(200);
}
