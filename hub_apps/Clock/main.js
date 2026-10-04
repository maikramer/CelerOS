// CelerOS Clock — relogio de mesa: hora, relogio mundial, alarme, cronometro
// e timer. ES5 (Duktape), toolkit UI (API 22): abas nativas, anel de segundos
// desenhado com System.fillArc e textos que se redesenham so quando mudam.
var TAB_CLOCK = 0;
var TAB_WORLD = 1;
var TAB_ALARM = 2;
var TAB_STOPWATCH = 3;
var TAB_TIMER = 4;
var currentTab = TAB_CLOCK;

var T = System.theme();
var WEEK = ["Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"];
var LX = 8, LW = 224, TOP = 46;

if (typeof System.keepAwake === "function") {
    try { System.keepAwake(1800000); } catch (e0) {}   // relogio de mesa: tela ligada
}
var hasTone = (typeof System.playTone === "function");
function tone(notes) {
    if (hasTone) { try { System.playTone(notes); } catch (e1) {} }
}

// --- World Clock Data ---
var cities = [
    { name: "Nova York", offset: -5 },
    { name: "Londres", offset: 0 },
    { name: "Tóquio", offset: 9 },
    { name: "Dubai", offset: 4 }
];

function parseLocalTime() {
    var tStr = System.getTime(); // "14:30" or "02:30 PM"
    var isPM = tStr.indexOf("PM") !== -1;
    var isAM = tStr.indexOf("AM") !== -1;
    var parts = tStr.split(":");
    var h = parseInt(parts[0], 10);
    var mStr = parts[1].split(" ")[0]; // handle "30 PM"
    var m = parseInt(mStr, 10);
    
    if (isPM && h !== 12) h += 12;
    if (isAM && h === 12) h = 0;
    return { h: h, m: m };
}

function parseTZOffset() {
    var tz = System.getTimezone(); // e.g. "UTC-5" or "UTC+5:30"
    tz = tz.replace("UTC", "");
    if (tz === "" || tz === "0") return 0;
    var sign = 1;
    if (tz.charAt(0) === '-') sign = -1;
    if (tz.charAt(0) === '+' || tz.charAt(0) === '-') tz = tz.substring(1);
    var parts = tz.split(":");
    var h = parseInt(parts[0], 10);
    var m = parts.length > 1 ? parseInt(parts[1], 10) : 0;
    return sign * (h + m / 60.0);
}

function formatCityTime(offset) {
    var local = parseLocalTime();
    var localOffset = parseTZOffset();
    
    var localDec = local.h + (local.m / 60.0);
    var utcDec = localDec - localOffset;
    var cityDec = utcDec + offset;
    
    while (cityDec < 0) cityDec += 24;
    while (cityDec >= 24) cityDec -= 24;
    
    var ch = Math.floor(cityDec);
    var cm = Math.round((cityDec - ch) * 60);
    if (cm === 60) { cm = 0; ch = (ch + 1) % 24; }
    
    var hStr = ch < 10 ? "0" + ch : ch;
    var mStr = cm < 10 ? "0" + cm : cm;
    return hStr + ":" + mStr;
}

// --- Alarm Data ---
var alarmH = 8;
var alarmM = 0;
var alarmOn = false;
var isAlarmRinging = false;

// --- Stopwatch Data ---
var swRunning = false;
var swStart = 0;
var swAccumulated = 0;

// --- Timer Data ---
var timerRunning = false;
var timerStart = 0;
var timerTotalMs = 5 * 60 * 1000; // 5 mins
var timerRemaining = timerTotalMs;

function format2(num) {
    return num < 10 ? "0" + num : num;
}

function formatMillis(ms) {
    var totalSec = Math.floor(ms / 1000);
    var mins = Math.floor(totalSec / 60);
    var secs = totalSec % 60;
    var frac = Math.floor((ms % 1000) / 10);
    return format2(mins) + ":" + format2(secs) + "." + format2(frac);
}

var currentH = 0;
var currentM = 0;
var currentS = 0;

function updateRealtime() {
    var t = parseLocalTime();
    currentH = t.h;
    currentM = t.m;
    currentS = System.getSeconds();
}

// ==========================================
// TELAS
// ==========================================

// Anel de segundos: desenho proprio, so quando o segundo muda (ou frame total)
var ringSec = -1;
function drawRing(cx, cy, r, frac, col, full) {
    var key = Math.floor(frac * 600);
    if (!full && key === ringSec) return;
    ringSec = key;
    System.fillArc(cx, cy, r - 6, r, 0, 360, T.raised);
    if (frac > 0) System.fillArc(cx, cy, r - 6, r, 270, 270 + 360 * frac, col);
}

function renderClock(full) {
    var cx = 120, cy = TOP + 98;
    drawRing(cx, cy, 92, currentS / 60, T.accent, full);
    UI.text(format2(currentH) + ":" + format2(currentM), cx, cy - 30, { role: "display", align: "center" });
    UI.text(format2(currentS), cx, cy + 12, { role: "title", align: "center", color: T.accent });
    var wd = System.getWeekday ? WEEK[System.getWeekday()] : "";
    UI.text((wd ? wd + ", " : "") + System.getDate(), 120, TOP + 206,
            { align: "center" });
    UI.text("fuso " + System.getTimezone(), 120, TOP + 232, { role: "caption", align: "center", color: T.textDim });
}

function renderWorld() {
    var rows = [{ label: "Aqui", right: format2(currentH) + ":" + format2(currentM), rightColor: T.accent }];
    for (var i = 0; i < cities.length; i++) {
        rows.push({ label: cities[i].name, sub: "UTC" + (cities[i].offset >= 0 ? "+" : "") + cities[i].offset,
                    right: formatCityTime(cities[i].offset), rightColor: T.warn });
    }
    UI.list("world", LX, TOP, LW, 312 - TOP, rows, { rowH: 48 });
}

function renderAlarm() {
    UI.text("agora " + format2(currentH) + ":" + format2(currentM) + ":" + format2(currentS), 120, TOP + 8,
            { role: "caption", align: "center", color: T.textDim });
    UI.text(format2(alarmH) + ":" + format2(alarmM), 120, TOP + 34, { role: "display", align: "center",
            color: alarmOn ? T.accent : T.text });
    var changed = false;
    if (UI.button("H+", 16, TOP + 96, 48, 40, { style: "ghost" })) { alarmH = (alarmH + 1) % 24; changed = true; }
    if (UI.button("H-", 70, TOP + 96, 48, 40, { style: "ghost" })) { alarmH = (alarmH + 23) % 24; changed = true; }
    if (UI.button("M+", 122, TOP + 96, 48, 40, { style: "ghost" })) { alarmM = (alarmM + 1) % 60; changed = true; }
    if (UI.button("M-", 176, TOP + 96, 48, 40, { style: "ghost" })) { alarmM = (alarmM + 59) % 60; changed = true; }
    // alarme ja ligado: reprograma o do OS com a hora nova
    if (changed && alarmOn && typeof System.setAlarm === "function") {
        try { System.setAlarm(alarmH, alarmM, "Alarme"); } catch (ec) {}
    }
    UI.card(LX, TOP + 152, LW, 48);
    UI.text("Alarme ligado", LX + 12, TOP + 166);
    var on = UI.toggle(LX + LW - 56, TOP + 164, alarmOn);
    if (on !== alarmOn) {
        alarmOn = on;
        // alarme de verdade no OS (API 12+): dispara mesmo com o app fechado
        if (typeof System.setAlarm === "function") {
            if (alarmOn) {
                try { System.setAlarm(alarmH, alarmM, "Alarme"); } catch (ea) {}
            } else {
                try { System.clearAlarm(); } catch (eb) {}
            }
        }
    }
    UI.cardEnd();
    UI.text("O alarme do sistema toca mesmo com o app fechado.", 120, TOP + 214,
            { role: "caption", align: "center", color: T.textDim, w: LW, lines: 2 });
}

function renderStopwatch(full) {
    var currentMs = swAccumulated + (swRunning ? System.millis() - swStart : 0);
    drawRing(120, TOP + 92, 84, (currentMs % 60000) / 60000, swRunning ? T.ok : T.textDim, full);
    UI.text(formatMillis(currentMs), 120, TOP + 80, { role: "title", align: "center" });
    if (UI.button(swRunning ? "Parar" : "Iniciar", LX, TOP + 200, 108, 44,
                  { color: swRunning ? T.warn : T.ok, textColor: T.onAccent })) {
        if (swRunning) {
            swAccumulated += (System.millis() - swStart);
            swRunning = false;
        } else {
            swStart = System.millis();
            swRunning = true;
        }
    }
    if (UI.button("Zerar", LX + 116, TOP + 200, 108, 44, { style: "ghost" })) {
        swRunning = false;
        swAccumulated = 0;
    }
}

function renderTimer(full) {
    var currentMs = timerTotalMs;
    if (timerRunning) {
        currentMs = timerTotalMs - (System.millis() - timerStart);
        if (currentMs <= 0) {
            currentMs = 0;
            timerRunning = false;
            timerRemaining = 0;
            isAlarmRinging = true; // tela de alarme
            UI.invalidate();
            tone([[880, 150], [660, 150], [880, 200]]);
        }
    }
    var base = timerRemaining > 0 ? timerRemaining : 1;
    drawRing(120, TOP + 80, 72, timerRunning ? currentMs / base : 1, T.accent, full);
    UI.text(formatMillis(currentMs), 120, TOP + 68, { role: "title", align: "center" });
    if (!timerRunning) {
        if (UI.button("+1 min", LX, TOP + 164, 108, 32, { style: "ghost" })) { timerTotalMs += 60000; timerRemaining = timerTotalMs; }
        if (UI.button("-1 min", LX + 116, TOP + 164, 108, 32, { style: "ghost" }) && timerTotalMs >= 60000) {
            timerTotalMs -= 60000;
            timerRemaining = timerTotalMs;
        }
    }
    if (UI.button(timerRunning ? "Pausar" : "Iniciar", LX, TOP + 208, 108, 44,
                  { color: timerRunning ? T.warn : T.ok, textColor: T.onAccent })) {
        if (timerRunning) {
            timerTotalMs = timerTotalMs - (System.millis() - timerStart);
            timerRemaining = timerTotalMs;
            timerRunning = false;
        } else if (timerTotalMs > 0) {
            timerRemaining = timerTotalMs;
            timerStart = System.millis();
            timerRunning = true;
        }
        UI.invalidate();
    }
    if (UI.button("Zerar", LX + 116, TOP + 208, 108, 44, { style: "ghost" })) {
        timerRunning = false;
        timerTotalMs = 5 * 60 * 1000;
        timerRemaining = timerTotalMs;
        UI.invalidate();
    }
}

function renderRinging(full) {
    if (full) System.fillGradient(0, 0, 240, 320, T.err, System.mixColor(T.err, 0, 60));
    UI.text("ACORDAR!", 120, 96, { role: "display", align: "center", color: 0xFFFF });
    if (UI.button("Parar", 40, 200, 160, 60, { color: 0xFFFF, textColor: T.err, role: "title" })) {
        isAlarmRinging = false;
        UI.invalidate();
    }
}

// ==========================================
// LACO
// ==========================================
var lastTab = currentTab;
while (true) {
    updateRealtime();
    // Alarme interno (o do OS tambem toca com o app fechado)
    if (alarmOn && !isAlarmRinging && currentH === alarmH && currentM === alarmM) {
        isAlarmRinging = true;
        alarmOn = false; // dispara uma vez
        UI.invalidate();
    }

    var full = UI.begin(isAlarmRinging ? T.err : T.bg);
    if (full) ringSec = -1;
    if (isAlarmRinging) {
        renderRinging(full);
    } else {
        currentTab = UI.tabs(LX, 6, LW, 32, ["Hora", "Mundo", "Alarme", "Cron.", "Timer"], currentTab);
        if (currentTab !== lastTab) {
            lastTab = currentTab;
            UI.invalidate();
        } else if (currentTab === TAB_CLOCK) renderClock(full);
        else if (currentTab === TAB_WORLD) renderWorld();
        else if (currentTab === TAB_ALARM) renderAlarm();
        else if (currentTab === TAB_STOPWATCH) renderStopwatch(full);
        else if (currentTab === TAB_TIMER) renderTimer(full);
    }
    UI.end();
}
