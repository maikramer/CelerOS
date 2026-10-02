// Celular — o link com o Gadgetbridge (API 15, Phone Link)
// ES5 puro (Duktape). Mostra o estado, o codigo de pareamento (grande) e
// as acoes: ligar/desligar, achar celular, esquecer o pareamento.

var T = System.theme();
var INFO = {};
try { INFO = System.getInfo() || {}; } catch (e) { INFO = {}; }
var M = 12 + Math.round((INFO.inset || 0) / 2);
var HAS = typeof Phone !== "undefined" && Phone && typeof Phone.status === "function";

function ctext(s, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, Math.round((240 - System.textWidth(s, f)) / 2), y, f);
}
function hit(t, r) { return t.x >= r[0] && t.x < r[0] + r[2] && t.y >= r[1] && t.y < r[1] + r[3]; }
function btn(r, label, bg, fg) {
    System.fillRoundRect(r[0], r[1], r[2], r[3], Math.round(r[3] / 2), bg);
    System.setTextColor(fg);
    System.drawString(label, r[0] + Math.round((r[2] - System.textWidth(label, 2)) / 2), r[1] + Math.round(r[3] / 2) - 7, 2);
}
function pad6(n) { var s = String(n); while (s.length < 6) s = "0" + s; return s; }

var W = 240 - 2 * M;
var B1 = [M, 168, W, 36], B2 = [M, 212, W, 36], B3 = [M, 256, W, 36];
var finding = false, confirmForget = false;

function draw(st) {
    System.fillRect(0, 0, 240, 320, T.bg);
    if (!HAS) { ctext("Sem Phone Link nesta placa", 150, 2, T.warn); return; }
    if (st.passkey) {
        ctext("Código", 70, 2, T.textDim);
        ctext(pad6(st.passkey), 110, 4, T.text);
        ctext("digite no celular", 170, 1, T.textDim);
        return;
    }
    var stTxt = !st.enabled ? "Desligado" : st.connected ? "Conectado" : "Aguardando celular";
    System.fillCircle(M + 8, 30, 5, !st.enabled ? T.stroke : st.connected ? T.ok : T.warn);
    System.setTextColor(T.text);
    System.drawString(stTxt, M + 20, 22, 2);
    if (st.enabled && !st.connected) {
        System.setTextColor(T.textDim);
        System.drawString("Gadgetbridge > + > Bangle.js", M, 58, 1);
        System.drawString("aparece como:", M, 76, 1);
        System.setTextColor(T.accent);
        System.drawString(st.name || "", M, 92, 2);
    } else if (st.connected) {
        System.setTextColor(T.textDim);
        System.drawString("Notificações, hora, música,", M, 58, 1);
        System.drawString("clima e chamadas chegam aqui.", M, 74, 1);
    }
    btn(B1, st.enabled ? "Desligar" : "Ligar", st.enabled ? T.card : T.accent, st.enabled ? T.text : T.onAccent);
    if (st.connected) btn(B2, finding ? "Parar de tocar" : "Achar celular", finding ? T.warn : T.card, T.text);
    btn(B3, confirmForget ? "Confirmar: esquecer" : "Esquecer pareamento", confirmForget ? T.err : T.card,
        confirmForget ? T.text : T.textDim);
}

function tap(t, st) {
    if (hit(t, B1)) { Phone.setEnabled(!st.enabled); confirmForget = false; return; }
    if (st.connected && hit(t, B2)) { finding = !finding; Phone.find(finding); return; }
    if (hit(t, B3)) {
        if (!confirmForget) { confirmForget = true; return; }
        confirmForget = false;
        Phone.forget();
        System.toast("Esqueça o relógio no Android também");
        return;
    }
    confirmForget = false;
}

var last = "", wasDown = false, lx = 0, ly = 0;
while (true) {
    var st = HAS ? Phone.status() : null;
    var sig = JSON.stringify(st) + finding + confirmForget;
    if (sig !== last) { draw(st || {}); last = sig; }
    var t = System.getTouch();
    if (t.touched) { lx = t.x; ly = t.y; }
    else if (wasDown && HAS && !st.passkey) { tap({ x: lx, y: ly }, st); last = ""; }
    wasDown = t.touched;
    System.delay(100);
}
