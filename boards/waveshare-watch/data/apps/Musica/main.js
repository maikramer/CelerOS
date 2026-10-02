// Musica — controle do player do celular (API 15, Phone Link/Gadgetbridge)
// ES5 puro (Duktape). Phone.musicInfo() traz faixa/estado; Phone.music(cmd)
// manda play/pause/next/previous/volume.

var T = System.theme();
var INFO = {};
try { INFO = System.getInfo() || {}; } catch (e) { INFO = {}; }
var M = 12 + Math.round((INFO.inset || 0) / 2);
var HAS = typeof Phone !== "undefined" && Phone && typeof Phone.music === "function";

function ctext(s, y, f, col) {
    if (System.textWidth(s, f) > 240 - 2 * M) {
        while (s.length > 3 && System.textWidth(s + "...", f) > 240 - 2 * M) s = s.substring(0, s.length - 1);
        s += "...";
    }
    System.setTextColor(col);
    System.drawString(s, Math.round((240 - System.textWidth(s, f)) / 2), y, f);
}
function hit(t, r) { return t.x >= r[0] && t.x < r[0] + r[2] && t.y >= r[1] && t.y < r[1] + r[3]; }
function btn(r, label, bg, fg) {
    System.fillRoundRect(r[0], r[1], r[2], r[3], Math.round(r[3] / 2), bg);
    System.setTextColor(fg);
    System.drawString(label, r[0] + Math.round((r[2] - System.textWidth(label, 2)) / 2), r[1] + Math.round(r[3] / 2) - 7, 2);
}

var PREV = [M, 150, 60, 56], PLAY = [90, 144, 60, 68], NEXT = [240 - M - 60, 150, 60, 56];
var VDN = [M + 10, 236, 80, 40], VUP = [240 - M - 90, 236, 80, 40];

function draw(st, mi) {
    System.fillRect(0, 0, 240, 320, T.bg);
    if (!HAS) { ctext("Sem Phone Link nesta placa", 150, 2, T.warn); return; }
    if (!st.connected) {
        ctext("Celular desconectado", 120, 2, T.textDim);
        ctext("Pareie no Gadgetbridge", 146, 1, T.textDim);
        ctext("(adicionar como Bangle.js)", 162, 1, T.textDim);
        return;
    }
    ctext(mi && mi.track ? mi.track : "Nada tocando", 46, 2, T.text);
    ctext(mi && mi.artist ? mi.artist : "", 74, 1, T.textDim);
    ctext(mi && mi.album ? mi.album : "", 92, 1, T.stroke);
    var playing = mi && mi.state === "play";
    btn(PREV, "<<", T.card, T.text);
    btn(PLAY, playing ? "||" : ">", T.accent, T.onAccent);
    btn(NEXT, ">>", T.card, T.text);
    btn(VDN, "Vol -", T.card, T.text);
    btn(VUP, "Vol +", T.card, T.text);
}

var last = "", wasDown = false;
while (true) {
    var st = HAS ? Phone.status() : { connected: false };
    var mi = HAS ? Phone.musicInfo() : null;
    var sig = JSON.stringify(st) + JSON.stringify(mi);
    if (sig !== last) { draw(st, mi); last = sig; }
    var t = System.getTouch();
    if (t.touched && !wasDown && HAS && st.connected) {
        var cmd = hit(t, PREV) ? "previous" : hit(t, PLAY) ? "playpause" : hit(t, NEXT) ? "next" :
            hit(t, VDN) ? "volumedown" : hit(t, VUP) ? "volumeup" : "";
        if (cmd) {
            Phone.music(cmd);
            if (System.beep) System.beep(1500, 20);
        }
    }
    wasDown = t.touched;
    System.delay(80);
}
