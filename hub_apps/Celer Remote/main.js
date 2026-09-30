// Celer Remote (API 10): controle remoto via Celer Link (Bluetooth).
// Escaneia CelerOS proximos, conecta e pilota com um D-pad na tela.
// Mensagens JSON: {type:"move",dir} / {type:"stop"}; o lado do robo
// responde com {type:"tel",...}.

var W = 240, H = 320;
var TH = System.theme();
var COL = {
    bg: TH.bg, card: TH.card, raised: TH.raised, stroke: TH.stroke,
    accent: TH.accent, text: TH.text, dim: TH.textDim, err: TH.err
};

function fill(c) { System.fillScreen(c); }
function rect(x, y, w, h, c) { System.fillRect(x, y, w, h, c); }
function frame(x, y, w, h, c) { System.drawRect(x, y, w, h, c); }
function txt(s, x, y, size, c) { System.setTextColor(c); System.setTextSize(size); System.drawString(s, x, y); }
function center(s, y, size, c) {
    System.setTextColor(c); System.setTextSize(size);
    System.drawString(s, (W - System.textWidth(s, size)) / 2, y);
}

var MODE_SCAN = 0, MODE_CTRL = 1;
var mode = MODE_SCAN;
var peers = [];
var scanning = false;
var selPeer = -1;          // indice do par tocado
var tel = null;            // ultima telemetria {batt,state}
var lastTelMs = 0;
var dropped = false;       // conexao caiu

// ---- tela de scan --------------------------------------------------------
function drawScan(msg) {
    fill(COL.bg);
    center("Celer Remote", 12, 2, COL.text);
    center("controle por Celer Link", 34, 1, COL.dim);
    var y = 60;
    if (scanning) {
        center("escaneando...", y, 2, COL.accent);
        return;
    }
    if (msg) { center(msg, y, 1, COL.err); y += 26; }
    if (peers.length === 0 && !msg) { center("nenhum CelerOS por perto", y, 1, COL.dim); y += 26; }
    for (var i = 0; i < peers.length && i < 6; i++) {
        var h = 34;
        rect(10, y, W - 20, h, selPeer === i ? COL.accentD || COL.raised : COL.card);
        frame(10, y, W - 20, h, COL.stroke);
        txt(peers[i].name || "(sem nome)", 18, y + 6, 1, COL.text);
        txt(peers[i].rssi + " dBm  " + peers[i].id, 18, y + 19, 1, COL.dim);
        y += h + 6;
    }
    var by = H - 44;
    rect(10, by, W - 20, 34, COL.accent);
    center("Scanear de novo (" + (peers.length | 0) + ")", by + 10, 1, 0x081020);
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
// botes: [x, y, w, h, rotulo, dir]
var PAD = [
    [90, 90, 60, 50, "^", "up"],
    [90, 200, 60, 50, "v", "down"],
    [25, 145, 60, 50, "<", "left"],
    [155, 145, 60, 50, ">", "right"],
    [90, 145, 60, 50, "o", "stop"]
];
var held = -1;             // indice pressionado
var lastSend = 0;

function drawCtrl() {
    fill(COL.bg);
    var st = CelerLink.status();
    var head = st.connected ? ("conectado: " + (st.peer || "?")) : "DESCONEECTADO";
    txt(head.substring(0, 30), 8, 6, 1, st.connected ? COL.ok || 0x20C864 : COL.err);
    if (tel) txt("batt " + tel.batt + "  " + (tel.state || ""), 8, 22, 1, COL.dim);
    for (var i = 0; i < PAD.length; i++) {
        var p = PAD[i];
        var on = held === i;
        rect(p[0], p[1], p[2], p[3], on ? COL.accent : COL.raised);
        frame(p[0], p[1], p[2], p[3], COL.stroke);
        center(p[4], p[1] + (p[3] - 14) / 2, 2, on ? 0x081020 : COL.text);
    }
    center("soltar tudo = parar", H - 18, 1, COL.dim);
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
    fill(COL.bg);
    center("Celer Remote", 60, 2, COL.text);
    center("esta placa nao tem Celer Link", 100, 1, COL.err);
    center("(Bluetooth desativado no build)", 120, 1, COL.dim);
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
                var y = 60;
                for (var i = 0; i < peers.length && i < 6; i++) {
                    if (t.y >= y && t.y < y + 40) {
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
                    y += 40;
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
