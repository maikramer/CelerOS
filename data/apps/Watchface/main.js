// CelerOS Watchface — relogio de pulso / relogio de mesa (API 15)
// ES5 puro (Duktape). Tela cheia (topbar:false): o canvas virtual 240x320
// escala para o vidro de cada placa (412x502 do watch, 480x480 da 4", ou a
// faixa da CYD).
//
// Tres estilos (toque longo troca; guardado no Storage do app):
//   0 digital  — hora grande, data, bateria, passos com meta, complicacoes;
//   1 analogico — mostrador com ponteiros (compensa a escala nao uniforme
//                 com getInfo().screenW/H);
//   2 minimo   — so hora e data, quase nada aceso (AMOLED agradece).
//
// Complicacoes (feature-detect, firmware antigo segue funcionando): bateria
// em % e carregando (batteryInfo), passos x meta (System.setting
// "step_goal"), proximo alarme / timer (System.alarms/getTimer), nao lidas
// (unreadNotifications) e clima do celular (Phone.weather, Gadgetbridge).
//
// Wallpaper: com /sd/wallpaper.png o estilo digital desenha o PNG UMA vez e
// redesenha so os elementos em "pills" opacas por cima.
//
// Gestos: arrastar para cima no meio da tela abre o launcher. No relogio as
// bordas sao do sistema (cima: ajustes rapidos; baixo: notificacoes).

var T = System.theme();
var INFO = {};
try { INFO = System.getInfo() || {}; } catch (e) { INFO = {}; }
var INS = INFO.inset || 0;
var TOPY = 10 + Math.round(INS / 2);
// vidro grande (watch/4"): digitos de 75 px (fonte 8); senao a 24 px (4)
var BIGF = (INFO.screenW || 240) >= 400 ? 8 : 4;
var BOTY = 310 - Math.round(INS / 2);

var HAS_BATI = typeof System.batteryInfo === "function";
var HAS_ALARMS = typeof System.alarms === "function";
var HAS_TIMER = typeof System.getTimer === "function";
var HAS_UNREAD = typeof System.unreadNotifications === "function";
var HAS_PHONE = typeof Phone !== "undefined" && Phone && typeof Phone.weather === "function";
var HAS_STORE = typeof Storage !== "undefined" && Storage && typeof Storage.get === "function";
var hasImu = false;
try { hasImu = (typeof Sensors !== "undefined") && Sensors && Sensors.steps() >= 0; } catch (e) { hasImu = false; }

var DIA = ["DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"];
var WALL = "/sd/wallpaper.png";
var hasWall = false;

var style = 0;
if (HAS_STORE) {
    style = parseInt(Storage.get("style", "0"), 10);
    if (!(style >= 0 && style <= 2)) style = 0;
}

function stepGoal() {
    var g = 0;
    try { g = parseInt(System.setting("step_goal"), 10); } catch (e) { g = 0; }
    return g > 0 ? g : 8000;
}
var goal = stepGoal();

function pad2(n) { return (n < 10 ? "0" : "") + n; }

function ctext(s, y, font, color) {
    System.setTextColor(color);
    System.drawString(s, Math.round((240 - System.textWidth(s, font)) / 2), y, font);
}

// ---- leitura do estado (uma "assinatura" decide o redesenho) ---------------

var S = {};
function readState() {
    var n = {};
    n.time = System.getTime();
    n.sec = System.getSeconds();
    n.date = System.getDate();
    n.wd = System.getWeekday ? System.getWeekday() : -1;
    n.pct = -1;
    n.chg = false;
    if (HAS_BATI) {
        var b = System.batteryInfo();
        if (b) { n.pct = b.pct; n.chg = b.charging; }
    } else {
        var mv = System.battery ? System.battery() : -1;
        if (mv > 3000) n.pct = Math.max(0, Math.min(100, Math.round((mv - 3300) / 9)));
    }
    n.steps = hasImu ? Sensors.steps() : -1;
    n.unread = HAS_UNREAD ? System.unreadNotifications() : 0;
    n.alarm = "";
    if (HAS_ALARMS) {
        var l = System.alarms(), best = 0;
        for (var i = 0; i < l.length; i++) {
            if (l[i].next > 0 && (best === 0 || l[i].next < best)) {
                best = l[i].next;
                n.alarm = pad2(l[i].hour) + ":" + pad2(l[i].minute);
            }
        }
    }
    n.timer = "";
    if (HAS_TIMER) {
        var t = System.getTimer();
        if (t && t.remaining > 0) n.timer = Math.floor(t.remaining / 60) + ":" + pad2(t.remaining % 60);
    }
    n.wx = "";
    if (HAS_PHONE) {
        try {
            var w = Phone.weather();
            if (w && typeof w.temp === "number") n.wx = Math.round(w.temp) + "C " + (w.txt || "");
        } catch (e) { n.wx = ""; }
    }
    return n;
}

// Hora em numeros (getTime pode vir "07:05" ou "7:05 PM")
function hm(s) {
    var p = s.split(":");
    var h = parseInt(p[0], 10), m = parseInt(p[1], 10);
    if (s.indexOf("PM") >= 0 && h < 12) h += 12;
    if (s.indexOf("AM") >= 0 && h === 12) h = 0;
    return [h, m];
}

// ---- pecas comuns -----------------------------------------------------------

function batteryText(n) {
    if (n.pct < 0) return "";
    return (n.chg ? "+" : "") + n.pct + "%";
}
function batteryColor(n) {
    if (n.chg) return T.ok;
    return n.pct <= 15 ? T.err : T.textDim;
}

// Linha de status no topo: bateria, nao lidas, alarme
function drawStatus(n, bg) {
    var bt = batteryText(n);
    if (bg !== null) System.fillRoundRect(40, TOPY, 160, 20, 8, bg);
    var x = 120;
    var items = [];
    if (bt) items.push([bt, batteryColor(n)]);
    if (n.unread > 0) items.push([n.unread + (n.unread === 1 ? " nova" : " novas"), T.accent]);
    if (n.alarm) items.push(["AL " + n.alarm, T.textDim]);
    var total = 0, i;
    for (i = 0; i < items.length; i++) total += System.textWidth(items[i][0], 1) + (i ? 10 : 0);
    x = Math.round(120 - total / 2);
    for (i = 0; i < items.length; i++) {
        System.setTextColor(items[i][1]);
        System.drawString(items[i][0], x, TOPY + 6, 1);
        x += System.textWidth(items[i][0], 1) + 10;
    }
}

// Barra de passos x meta
function drawSteps(n, y, bg) {
    if (n.steps < 0) return;
    if (bg !== null) System.fillRoundRect(36, y - 4, 168, 34, 10, bg);
    ctext(n.steps + " / " + goal + " passos", y, 1, T.textDim);
    var w = Math.min(150, Math.round(150 * n.steps / goal));
    System.fillRoundRect(45, y + 14, 150, 8, 4, T.card);
    if (w > 6) System.fillRoundRect(45, y + 14, w, 8, 4, n.steps >= goal ? T.ok : T.accent);
}

// Linha de baixo: timer > clima
function drawFooter(n, bg) {
    var s = n.timer ? "Timer " + n.timer : n.wx;
    if (bg !== null) System.fillRoundRect(40, BOTY - 26, 160, 22, 8, bg);
    if (s) ctext(s, BOTY - 22, 2, n.timer ? T.warn : T.textDim);
}

// ---- estilo 0: digital ------------------------------------------------------

function digitalAll(n) {
    if (!hasWall) System.fillRect(0, 0, 240, 320, T.bg);
    var bg = hasWall ? T.bg : null;
    drawStatus(n, bg);
    digitalClock(n, bg);
    drawSteps(n, 206, bg);
    digitalBar(n);
    drawFooter(n, bg);
}
function digitalClock(n, bg) {
    if (bg !== null) System.fillRoundRect(26, 70, 188, 110, 12, bg);
    var t = n.time, suf = "";
    var sp = t.indexOf(" ");
    if (sp > 0) { suf = t.substring(sp + 1); t = t.substring(0, sp); }
    ctext(t, BIGF === 8 ? 84 : 96, BIGF, T.text);
    if (suf) ctext(suf, 134, 1, T.textDim);
    var d = (n.wd >= 0 ? DIA[n.wd] + "  " : "") + n.date.substring(0, 5);
    ctext(d, 150, 2, T.accent);
}
function digitalBar(n) {
    System.fillRoundRect(70, 248, 100, 10, 5, T.bg);
    var prog = Math.round(96 * n.sec / 60);
    if (prog > 0) System.fillRect(72, 250, prog, 6, T.accent);
}

// ---- estilo 1: analogico ----------------------------------------------------

var KX = (INFO.screenW || 240) / 240, KY = (INFO.screenH || 320) / 320;
var KM = Math.min(KX, KY);
var FX = KM / KX, FY = KM / KY;   // fatores para ponto no circulo "redondo"
var CX = 120, CY = 160, R = 92;

function polar(a, r) {
    // a em graus a partir das 12h, sentido horario
    var rad = a * Math.PI / 180;
    return [CX + Math.sin(rad) * r * FX, CY - Math.cos(rad) * r * FY];
}
function hand(a, len, tail, wid, col) {
    var tip = polar(a, len), back = polar(a + 180, tail);
    var l = polar(a - 90, wid), r = polar(a + 90, wid);
    var dl = [l[0] - CX, l[1] - CY], dr = [r[0] - CX, r[1] - CY];
    System.fillTriangle(Math.round(back[0] + dl[0]), Math.round(back[1] + dl[1]),
        Math.round(back[0] + dr[0]), Math.round(back[1] + dr[1]),
        Math.round(tip[0]), Math.round(tip[1]), col);
}
function analogAll(n) {
    System.fillRect(0, 0, 240, 320, T.bg);
    drawStatus(n, null);
    System.fillCircle(CX, CY, R + 6, T.card);
    for (var i = 0; i < 12; i++) {
        var o = polar(i * 30, R), p = polar(i * 30, i % 3 === 0 ? R - 14 : R - 7);
        System.drawLine(Math.round(o[0]), Math.round(o[1]), Math.round(p[0]), Math.round(p[1]),
            i % 3 === 0 ? T.text : T.textDim);
    }
    var d = (n.wd >= 0 ? DIA[n.wd] + " " : "") + n.date.substring(0, 2);
    System.setTextColor(T.accent);
    System.drawString(d, CX + 28, CY - 6, 1);
    if (n.steps >= 0) ctext(n.steps + " passos", CY + 40, 1, T.textDim);
    var x = hm(n.time);
    hand((x[0] % 12) * 30 + x[1] * 0.5, R * 0.55, 10, 5, T.text);
    hand(x[1] * 6 + n.sec * 0.1, R * 0.82, 12, 3, T.text);
    hand(n.sec * 6, R * 0.88, 16, 1, T.accent);
    System.fillCircle(CX, CY, 5, T.accent);
    drawFooter(n, null);
}

// ---- estilo 2: minimo -------------------------------------------------------

function minimalAll(n) {
    System.fillRect(0, 0, 240, 320, 0);
    var t = n.time, sp = t.indexOf(" ");
    if (sp > 0) t = t.substring(0, sp);
    ctext(t, 120, 7, T.textDim);
    var d = (n.wd >= 0 ? DIA[n.wd] + "  " : "") + n.date.substring(0, 5);
    ctext(d, 180, 2, T.stroke);
    if (n.unread > 0) System.fillCircle(120, 210, 4, T.accent);
    var bt = batteryText(n);
    if (bt && (n.pct <= 20 || n.chg)) ctext(bt, BOTY - 18, 1, batteryColor(n));
}

// ---- laco ------------------------------------------------------------------

function sigOf(n) {
    // o que muda o desenho; analogico redesenha a cada segundo
    return [n.time, style === 2 ? "" : n.sec, n.date, n.pct, n.chg, n.steps, n.unread,
        n.alarm, n.timer, n.wx].join("|");
}

function drawAll(n) {
    if (style === 1) analogAll(n);
    else if (style === 2) minimalAll(n);
    else digitalAll(n);
}

function setStyle(s) {
    style = s;
    if (HAS_STORE) Storage.set("style", String(s));
    hasWall = false;
    if (style === 0) {
        try { hasWall = FS.exists(WALL) && System.drawPNG(WALL, 0, 0); } catch (e) { hasWall = false; }
    }
    S = readState();
    drawAll(S);
}

setStyle(style);
var lastSig = sigOf(S);
var goalAt = System.millis();

var touchStartX = -1, touchStartY = -1, touchLastY = -1, touchAt = 0, moved = false, longDone = false;

while (true) {
    var n = readState();
    if (System.millis() - goalAt > 30000) {  // meta mudada no Settings
        goalAt = System.millis();
        goal = stepGoal();
    }
    var sig = sigOf(n);
    if (sig !== lastSig) {
        if (style === 0 && hasWall) {
            // com wallpaper: so o que mudou, em pills opacas
            if (n.time !== S.time || n.date !== S.date) digitalClock(n, T.bg);
            if (n.sec !== S.sec) digitalBar(n);
            if (n.pct !== S.pct || n.chg !== S.chg || n.unread !== S.unread || n.alarm !== S.alarm) drawStatus(n, T.bg);
            if (n.steps !== S.steps) drawSteps(n, 206, T.bg);
            if (n.timer !== S.timer || n.wx !== S.wx) drawFooter(n, T.bg);
        } else {
            drawAll(n);
        }
        S = n;
        lastSig = sig;
    }

    var tp = System.getTouch();
    if (tp.touched) {
        if (touchStartY < 0) {
            touchStartX = tp.x; touchStartY = tp.y; touchAt = System.millis();
            moved = false; longDone = false;
        }
        if (Math.abs(tp.x - touchStartX) > 12 || Math.abs(tp.y - touchStartY) > 12) moved = true;
        touchLastY = tp.y;
        // toque longo parado: proximo estilo
        if (!moved && !longDone && System.millis() - touchAt > 700) {
            longDone = true;
            setStyle((style + 1) % 3);
            lastSig = sigOf(S);
            if (System.beep) System.beep(1600, 30);
        }
    } else if (touchStartY >= 0) {
        if (!longDone && touchStartY - touchLastY > 25) System.exitApp();
        touchStartY = -1;
        touchLastY = -1;
    }

    System.delay(150);
}
