// Alarmes — lista e edicao dos alarmes do sistema (API 15)
// ES5 puro (Duktape). Os alarmes vivem no firmware (System.alarms/addAlarm/
// updateAlarm/removeAlarm): tocam com o app fechado, a tela apagada ou o
// relogio em deep sleep. Edicao sem teclado: +/- na hora e no minuto e os
// dias da semana em bolinhas.

var T = System.theme();
var INFO = {};
try { INFO = System.getInfo() || {}; } catch (e) { INFO = {}; }
var M = 10 + Math.round((INFO.inset || 0) / 2);
var DLET = ["D", "S", "T", "Q", "Q", "S", "S"];
var ROW_H = 46, TOP = 44, LIST_BOT = 262;

var mode = "list";      // list | edit
var list = [];
var scroll = 0;
var ed = null;          // {id, hour, minute, days, label, enabled}
var dirty = true;

function pad2(n) { return (n < 10 ? "0" : "") + n; }
function hit(t, x, y, w, h) { return t.x >= x && t.x < x + w && t.y >= y && t.y < y + h; }
function ctext(s, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, Math.round((240 - System.textWidth(s, f)) / 2), y, f);
}
function button(x, y, w, h, label, bg, fg) {
    System.fillRoundRect(x, y, w, h, Math.round(h / 2), bg);
    System.setTextColor(fg);
    System.drawString(label, x + Math.round((w - System.textWidth(label, 2)) / 2), y + Math.round(h / 2) - 7, 2);
}
function daysText(d) {
    if (!d) return "uma vez";
    if (d === 0x7F) return "todo dia";
    if (d === 0x3E) return "seg a sex";
    if (d === 0x41) return "fim de semana";
    var s = "";
    for (var i = 0; i < 7; i++) if (d & (1 << i)) s += DLET[i];
    return s;
}

function reload() { list = System.alarms(); }

// ---- lista ------------------------------------------------------------------

var ADD = [M + 30, 272, 240 - 2 * M - 60, 36];

function drawList() {
    System.fillRect(0, 0, 240, 320, T.bg);
    ctext(list.length + " de 8 alarmes", 18, 1, T.textDim);
    System.setClip(0, TOP, 240, LIST_BOT - TOP);
    for (var i = 0; i < list.length; i++) {
        var y = TOP + i * ROW_H - scroll;
        if (y + ROW_H < TOP || y > LIST_BOT) continue;
        var a = list[i];
        System.fillRoundRect(M, y + 3, 240 - 2 * M, ROW_H - 6, 10, T.card);
        System.setTextColor(a.enabled ? T.text : T.textDim);
        System.drawString(pad2(a.hour) + ":" + pad2(a.minute), M + 10, y + 9, 4);
        System.setTextColor(T.textDim);
        var sub = daysText(a.days) + (a.label ? "  " + a.label : "");
        System.drawString(sub.length > 22 ? sub.substring(0, 21) + "." : sub, M + 86, y + 18, 1);
        // chave on/off
        var sx = 240 - M - 40;
        System.fillRoundRect(sx, y + 14, 32, 16, 8, a.enabled ? T.accent : T.stroke);
        System.fillCircle(a.enabled ? sx + 24 : sx + 8, y + 22, 6, T.text);
    }
    System.clearClip();
    if (list.length === 0) ctext("Nenhum alarme", 140, 2, T.textDim);
    if (list.length < 8) button(ADD[0], ADD[1], ADD[2], ADD[3], "+ Novo", T.accent, T.onAccent);
}

function rowAt(t) {
    if (t.y < TOP || t.y >= LIST_BOT) return -1;
    var i = Math.floor((t.y - TOP + scroll) / ROW_H);
    return i >= 0 && i < list.length ? i : -1;
}

// ---- editor -----------------------------------------------------------------

var HM = [M, 70, 44, 40], HP = [M + 70, 70, 44, 40];       // hora - +
var MM = [240 - M - 114, 70, 44, 40], MP = [240 - M - 44, 70, 44, 40];  // minuto - +
var DAY_Y = 150;
var SAVE = [128, 262, 240 - M - 128, 40], DEL = [M, 262, 112 - M, 40];

function dayCx(i) { return Math.round(M + 14 + i * (240 - 2 * M - 28) / 6); }

function drawEdit() {
    System.fillRect(0, 0, 240, 320, T.bg);
    ctext(ed.id < 0 ? "Novo alarme" : "Editar alarme", 18, 2, T.textDim);
    button(HM[0], HM[1], HM[2], HM[3], "-", T.card, T.text);
    button(HP[0], HP[1], HP[2], HP[3], "+", T.card, T.text);
    button(MM[0], MM[1], MM[2], MM[3], "-", T.card, T.text);
    button(MP[0], MP[1], MP[2], MP[3], "+", T.card, T.text);
    ctext(pad2(ed.hour) + ":" + pad2(ed.minute), 116, 4, T.text);
    for (var i = 0; i < 7; i++) {
        var on = (ed.days & (1 << i)) !== 0;
        System.fillCircle(dayCx(i), DAY_Y + 30, 13, on ? T.accent : T.card);
        System.setTextColor(on ? T.onAccent : T.textDim);
        System.drawString(DLET[i], dayCx(i) - (System.textWidth(DLET[i], 2) >> 1), DAY_Y + 23, 2);
    }
    ctext(daysText(ed.days), DAY_Y + 56, 1, T.textDim);
    ctext(ed.label ? ed.label : "(toque aqui p/ rotulo)", 228, 1, T.textDim);
    if (ed.id >= 0) button(DEL[0], DEL[1], DEL[2], DEL[3], "Apagar", T.card, T.err);
    button(ed.id >= 0 ? SAVE[0] : M + 30, SAVE[1], ed.id >= 0 ? SAVE[2] : 240 - 2 * M - 60, SAVE[3],
        "Salvar", T.accent, T.onAccent);
}

function editTap(t) {
    if (hit(t, HM[0], HM[1], HM[2], HM[3])) ed.hour = (ed.hour + 23) % 24;
    else if (hit(t, HP[0], HP[1], HP[2], HP[3])) ed.hour = (ed.hour + 1) % 24;
    else if (hit(t, MM[0], MM[1], MM[2], MM[3])) ed.minute = ed.minute % 5 ? ed.minute - ed.minute % 5 : (ed.minute + 55) % 60;
    else if (hit(t, MP[0], MP[1], MP[2], MP[3])) ed.minute = (ed.minute - ed.minute % 5 + 5) % 60;
    else if (t.y >= DAY_Y + 14 && t.y < DAY_Y + 48) {
        for (var i = 0; i < 7; i++) if (Math.abs(t.x - dayCx(i)) <= 15) ed.days ^= (1 << i);
    } else if (t.y >= 218 && t.y < 248) {
        var l = System.prompt("Rótulo do alarme", ed.label || "");
        if (l !== null && l !== undefined) ed.label = String(l).substring(0, 40);
    } else if (ed.id >= 0 && hit(t, DEL[0], DEL[1], DEL[2], DEL[3])) {
        System.removeAlarm(ed.id);
        mode = "list";
        reload();
    } else if (hit(t, ed.id >= 0 ? SAVE[0] : M + 30, SAVE[1], ed.id >= 0 ? SAVE[2] : 240 - 2 * M - 60, SAVE[3])) {
        var spec = { hour: ed.hour, minute: ed.minute, days: ed.days, label: ed.label, enabled: true };
        var ok = ed.id >= 0 ? System.updateAlarm(ed.id, spec) : System.addAlarm(spec) >= 0;
        System.toast(ok ? "Alarme " + pad2(ed.hour) + ":" + pad2(ed.minute) : "Falha ao salvar");
        mode = "list";
        reload();
    }
}

function listTap(t) {
    if (list.length < 8 && hit(t, ADD[0], ADD[1], ADD[2], ADD[3])) {
        ed = { id: -1, hour: 7, minute: 0, days: 0x3E, label: "" };
        mode = "edit";
        return;
    }
    var i = rowAt(t);
    if (i < 0) return;
    var a = list[i];
    if (t.x >= 240 - M - 48) {  // chave on/off
        System.updateAlarm(a.id, { hour: a.hour, minute: a.minute, days: a.days, label: a.label, enabled: !a.enabled });
        reload();
        return;
    }
    ed = { id: a.id, hour: a.hour, minute: a.minute, days: a.days, label: a.label };
    mode = "edit";
}

if (typeof System.alarms !== "function") {
    System.fillRect(0, 0, 240, 320, T.bg);
    ctext("Requer firmware API 15", 150, 2, T.warn);
    while (true) { System.getTouch(); System.delay(200); }
}

reload();
// toque: o release vem sem coordenadas — o tap usa a ultima posicao vista
var down = false, sy0 = 0, lastX = 0, lastY = 0, moved = false, scroll0 = 0;
while (true) {
    if (dirty) {
        if (mode === "list") drawList(); else drawEdit();
        dirty = false;
    }
    var t = System.getTouch();
    if (t.touched) {
        if (!down) { down = true; sy0 = t.y; lastY = t.y; moved = false; scroll0 = scroll; }
        if (Math.abs(t.y - sy0) > 10) moved = true;
        if (mode === "list" && moved) {
            var maxS = Math.max(0, list.length * ROW_H - (LIST_BOT - TOP));
            scroll = Math.max(0, Math.min(maxS, scroll0 - (t.y - sy0)));
            dirty = true;
        }
        lastX = t.x;
        lastY = t.y;
    } else if (down) {
        down = false;
        if (!moved) {
            if (mode === "list") listTap({ x: lastX, y: lastY }); else editTap({ x: lastX, y: lastY });
            dirty = true;
        }
    }
    System.delay(40);
}
