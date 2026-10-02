// Clima — tempo atual vindo do celular (API 15, Phone Link/Gadgetbridge)
// ES5 puro (Duktape). O Gadgetbridge repassa o clima de um provedor (ex.:
// Breezy Weather); o firmware guarda a ultima leitura entre reboots.

var T = System.theme();
var HAS = typeof Phone !== "undefined" && Phone && typeof Phone.weather === "function";

function ctext(s, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, Math.round((240 - System.textWidth(s, f)) / 2), y, f);
}
function ago(s) {
    if (s < 90) return "agora";
    if (s < 3600) return "há " + Math.round(s / 60) + " min";
    if (s < 172800) return "há " + Math.round(s / 3600) + " h";
    return "há " + Math.round(s / 86400) + " dias";
}

function draw(w) {
    System.fillRect(0, 0, 240, 320, T.bg);
    if (!HAS) { ctext("Sem Phone Link nesta placa", 150, 2, T.warn); return; }
    if (!w) {
        ctext("Sem dados ainda", 130, 2, T.textDim);
        ctext("Gadgetbridge > clima", 156, 1, T.textDim);
        return;
    }
    ctext(w.loc || "", 40, 2, T.textDim);
    ctext(Math.round(w.temp) + " C", 90, 4, w.temp >= 30 ? T.warn : T.text);
    ctext(w.txt || "", 150, 2, T.accent);
    if (w.hum >= 0) ctext("umidade " + w.hum + "%", 184, 1, T.textDim);
    ctext("atualizado " + ago(w.age), 270, 1, w.age > 10800 ? T.warn : T.stroke);
}

var last = "";
while (true) {
    var w = HAS ? Phone.weather() : null;
    var sig = w ? [Math.round(w.temp), w.txt, w.hum, w.loc, Math.floor(w.age / 60)].join("|") : "-";
    if (sig !== last) { draw(w); last = sig; }
    System.getTouch();
    System.delay(500);
}
