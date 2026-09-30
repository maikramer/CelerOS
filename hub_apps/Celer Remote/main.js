// Celer Remote (API 10): controle remoto via Celer Link (Bluetooth).
// Escaneia CelerOS proximos, conecta e pilota com um D-pad na tela.
// Mensagens JSON: {type:"move",dir} / {type:"stop"}; o lado do robo
// responde com {type:"tel",...}.
//
// Tipografia: fontes numericas 1/2/4 passadas como 4o arg do drawString
// (setTextSize nao troca a fonte do drawString) e fontHeight()/textWidth()
// com o MESMO indice — o idiom da App Store.

var W = 240, H = 320;
var TH = System.theme();
var ONACC = 0x081020;  // texto sobre o acento
function fh(f) { return System.fontHeight ? System.fontHeight(f) : (f >= 2 ? 16 : 10); }
function txt(s, x, y, f, c) { System.setTextColor(c); System.drawString(s, x, y, f); }
function center(s, y, f, c) { txt(s, (W - System.textWidth(s, f)) / 2, y, f, c); }

var MODE_SCAN = 0, MODE_CTRL = 1;
var mode = MODE_SCAN;
var peers = [];
var scanning = false;
var selPeer = -1;
var tel = null;
var lastTelMs = 0;
var dropped = false;

// ---- tela de scan --------------------------------------------------------
var ITEM_H = 34, ITEM_GAP = 8, LIST_Y = 66;

function drawScan(msg) {
    System.fillScreen(TH.bg);
    center("Celer Remote", 10, 2, TH.text);
    center("controle por Celer Link", 12 + fh(2) + 4, 1, TH.textDim);
    var y = LIST_Y;
    if (scanning) {
        center("escaneando...", y, 2, TH.accent);
        return;
    }
    if (msg) { center(msg, y, 1, TH.err); y += fh(1) + 10; }
    if (peers.length === 0 && !msg) {
        center("nenhum CelerOS por perto", y, 1, TH.textDim);
        y += fh(1) + 10;
    }
    for (var i = 0; i < peers.length && i < 6; i++) {
        var sel = selPeer === i;
        System.fillRect(10, y, W - 20, ITEM_H, sel ? TH.accentD : TH.card);
        System.drawRect(10, y, W - 20, ITEM_H, TH.stroke);
        txt(peers[i].name || "(sem nome)", 18, y + 5, 1, TH.text);
        txt(peers[i].rssi + " dBm  " + peers[i].id, 18, y + 5 + fh(1) + 3, 1, TH.textDim);
        y += ITEM_H + ITEM_GAP;
    }
    var by = H - 46;
    System.fillRect(10, by, W - 20, 36, TH.accent);
    center("Scanear de novo (" + peers.length + ")", by + (36 - fh(1)) / 2, 1, ONACC);
}

function doScan() {
    scanning = true;
    drawScan(null);
    peers = CelerLink.scan(3000);
    scanning = false;
    selPeer = -1;
    drawScan(null);
}

// ---- tela de controle (D-pad) -------------------------------------------
// [x, y, w, h, rotulo, dir]
var PAD = [
    [90, 96, 60, 46, "^", "up"],
    [90, 198, 60, 46, "v", "down"],
    [24, 147, 60, 46, "<", "left"],
    [156, 147, 60, 46, ">", "right"],
    [90, 147, 60, 46, "o", "stop"]
];
var held = -1;
var lastSend = 0;

function drawCtrl() {
    System.fillScreen(TH.bg);
    var st = CelerLink.status();
    var head = st.connected ? ("conectado: " + (st.peer || "?")) : "desconectado";
    txt(head.substring(0, 32), 8, 6, 1, st.connected ? TH.ok : TH.err);
    if (tel) txt("batt " + tel.batt + "  " + (tel.state || ""), 8, 6 + fh(1) + 3, 1, TH.textDim);
    for (var i = 0; i < PAD.length; i++) {
        var p = PAD[i];
        var on = held === i;
        System.fillRect(p[0], p[1], p[2], p[3], on ? TH.accent : TH.raised);
        System.drawRect(p[0], p[1], p[2], p[3], TH.stroke);
        center(p[4], p[1] + (p[3] - fh(2)) / 2, 2, on ? ONACC : TH.text);
    }
    center("soltar tudo = parar", H - 8 - fh(1), 1, TH.textDim);
}

function hitPad(t) {
    for (var i = 0; i < PAD.length; i++) {
        var p = PAD[i];
        if (t.x >= p[0] && t.x < p[0] + p[2] && t.y >= p[1] && t.y < p[1] + p[3]) return i;
    }
    return -1;
}

// ---- sem Celer Link (placa sem BT) ---------------------------------------
if (typeof CelerLink === "undefined") {
    System.fillScreen(TH.bg);
    center("Celer Remote", 60, 2, TH.text);
    center("esta placa nao tem Celer Link", 60 + fh(2) + 16, 1, TH.err);
    center("(Bluetooth desativado no build)", 60 + fh(2) + 16 + fh(1) + 8, 1, TH.textDim);
    System.delay(3000);
    System.exitApp();
}

// ---- laco principal -------------------------------------------------------
doScan();  // primeiro scan automatico
while (true) {
    var t = System.getTouch();

    if (mode === MODE_SCAN) {
        if (t.touched) {
            if (t.y > H - 50) {
                doScan();
            } else {
                // itens da lista: mesmos retangulos do drawScan (sem msg)
                var y = LIST_Y;
                for (var i = 0; i < peers.length && i < 6; i++) {
                    if (t.y >= y && t.y < y + ITEM_H + ITEM_GAP) {
                        selPeer = i;
                        drawScan(null);
                        System.delay(150);
                        if (CelerLink.connect(peers[i].id, 5000)) {
                            mode = MODE_CTRL;
                            tel = null;
                            dropped = false;
                            drawCtrl();
                        } else {
                            drawScan("conectou nao: " + peers[i].name);
                        }
                        break;
                    }
                    y += ITEM_H + ITEM_GAP;
                }
            }
        }
    } else {
        var st = CelerLink.status();
        if (!st.connected && !dropped) {
            dropped = true;
            held = -1;
            drawCtrl();
        }
        var h2 = t.touched ? hitPad(t) : -1;
        if (h2 !== held) {
            held = h2;
            drawCtrl();
            if (held >= 0) {
                var dir = PAD[held][5];
                CelerLink.send(dir === "stop" ? {type: "stop"} : {type: "move", dir: dir});
                lastSend = System.millis();
            }
        } else if (held >= 0 && held !== 4 && System.millis() - lastSend > 250) {
            // segurar a seta: repete o comando (anda enquanto segura)
            CelerLink.send({type: "move", dir: PAD[held][5]});
            lastSend = System.millis();
        }
        var m = CelerLink.poll();
        if (m !== null) {
            var v = null;
            try { v = JSON.parse(m); } catch (e) {}
            if (v && v.type === "tel") {
                tel = v;
                lastTelMs = System.millis();
                drawCtrl();
            }
        }
        if (dropped && System.millis() - lastTelMs > 400 && t.touched && t.y < 60) {
            // toque no topo apos queda: volta pro scan
            mode = MODE_SCAN;
            drawScan(null);
        }
    }
    System.delay(30);
}
