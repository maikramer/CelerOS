// Celer Remote (API 10): controle remoto via Celer Link (Bluetooth).
// Escaneia CelerOS proximos, conecta e pilota com um D-pad na tela.
// Mensagens JSON: {type:"move",dir} / {type:"stop"}; o lado do robo
// responde com {type:"tel",...}.
//
// Seguranca do robo: segurar uma seta repete o move a cada 250 ms (o robo
// para sozinho se a repeticao some) e soltar manda stop. Se o link cai, o
// app tenta reconectar no mesmo par antes de voltar para a lista.
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
function hit(t, r) { return t.x >= r[0] && t.x < r[0] + r[2] && t.y >= r[1] && t.y < r[1] + r[3]; }

var MODE_SCAN = 0, MODE_CTRL = 1;
var mode = MODE_SCAN;
var peers = [];
var scanning = false;
var selPeer = -1;
var target = null;      // par conectado (para reconectar)
var tel = null;
var wasDown = false;    // borda do toque (evita repetir acao com o dedo parado)

var REPEAT_MS = 250;    // repeticao do move (o robo tem keepalive de ~900 ms)
var RECONNECT_TRIES = 3;

// ---- tela de scan --------------------------------------------------------
var ITEM_H = 34, ITEM_GAP = 8, LIST_Y = 66;
var MAX_ITEMS = 4;      // cabe acima do botao mesmo com a linha de erro
var itemRects = [];     // retangulos desenhados (o hit-test usa os mesmos)
var RESCAN = [10, H - 46, W - 20, 36];

function drawScan(msg) {
    System.fillScreen(TH.bg);
    center("Celer Remote", 10, 2, TH.text);
    center("controle por Celer Link", 12 + fh(2) + 4, 1, TH.textDim);
    itemRects = [];
    var y = LIST_Y;
    if (scanning) {
        center("escaneando...", y, 2, TH.accent);
        return;
    }
    if (msg) { center(msg, y, 1, TH.err); y += fh(1) + 10; }
    if (peers.length === 0) {
        center("nenhum CelerOS por perto", y, 1, TH.textDim);
        y += fh(1) + 10;
    }
    for (var i = 0; i < peers.length && i < MAX_ITEMS; i++) {
        var sel = selPeer === i;
        var r = [10, y, W - 20, ITEM_H];
        itemRects.push(r);
        System.fillRect(r[0], r[1], r[2], r[3], sel ? TH.accentD : TH.card);
        System.drawRect(r[0], r[1], r[2], r[3], TH.stroke);
        txt(peers[i].name || "(sem nome)", 18, y + 5, 1, TH.text);
        txt(peers[i].rssi + " dBm  " + peers[i].id, 18, y + 5 + fh(1) + 3, 1, TH.textDim);
        y += ITEM_H + ITEM_GAP;
    }
    System.fillRect(RESCAN[0], RESCAN[1], RESCAN[2], RESCAN[3], TH.accent);
    center("Escanear de novo (" + peers.length + ")", RESCAN[1] + (RESCAN[3] - fh(1)) / 2, 1, ONACC);
}

function doScan() {
    scanning = true;
    drawScan(null);
    peers = CelerLink.scan(3000);
    // o firmware ja ordena por RSSI; ordena de novo por garantia (fw antigo)
    peers.sort(function(a, b) { return b.rssi - a.rssi; });
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
var STOP_IDX = 4;
var BACK = [0, 0, 64, 40];   // "< sair" no canto superior esquerdo
var MODE = [10, 254, W - 20, 30];  // troca a marcha do robo ({type:"mode"})
var held = -1;
var lastSend = 0;

function drawCtrl(note) {
    System.fillScreen(TH.bg);
    var st = CelerLink.status();
    txt("< sair", 8, 6, 1, TH.accent);
    var head = st.connected ? ((target && target.name) || st.peer || "?") : "desconectado";
    if (st.connected && st.rssi) head += "  " + st.rssi + " dBm";
    txt(head.substring(0, 30), 60, 6, 1, st.connected ? TH.ok : TH.err);
    var y2 = 6 + fh(1) + 4;
    if (note) txt(note, 8, y2, 1, TH.accent);
    else if (tel) txt("batt " + tel.batt + "  " + (tel.state || ""), 8, y2, 1, TH.textDim);
    for (var i = 0; i < PAD.length; i++) {
        var p = PAD[i];
        var on = held === i;
        System.fillRect(p[0], p[1], p[2], p[3], on ? TH.accent : TH.raised);
        System.drawRect(p[0], p[1], p[2], p[3], TH.stroke);
        center(p[4], p[1] + (p[3] - fh(2)) / 2, 2, on ? ONACC : TH.text);
    }
    // so aparece com robo que reporta marcha (tel.mode, ex.: Dog Face)
    if (tel && tel.mode) {
        System.fillRect(MODE[0], MODE[1], MODE[2], MODE[3], TH.card);
        System.drawRect(MODE[0], MODE[1], MODE[2], MODE[3], TH.stroke);
        center("marcha: " + tel.mode + " (trocar)", MODE[1] + (MODE[3] - fh(1)) / 2, 1, TH.text);
    }
    center("soltar = parar", H - 8 - fh(1), 1, TH.textDim);
}

function hitPad(t) {
    for (var i = 0; i < PAD.length; i++) if (hit(t, PAD[i])) return i;
    return -1;
}

function sendDir(i) {
    var dir = PAD[i][5];
    CelerLink.send(dir === "stop" ? {type: "stop"} : {type: "move", dir: dir});
    lastSend = System.millis();
}

function enterCtrl(p) {
    target = p;
    mode = MODE_CTRL;
    tel = null;
    held = -1;
    drawCtrl(null);
}

function backToScan(msg) {
    if (CelerLink.status().connected) {
        CelerLink.send({type: "stop"});
        CelerLink.disconnect();
    }
    mode = MODE_SCAN;
    target = null;
    held = -1;
    drawScan(msg || null);
}

// Link caiu: tenta o mesmo par algumas vezes (o robo segue anunciando).
function reconnect() {
    for (var k = 1; k <= RECONNECT_TRIES; k++) {
        drawCtrl("reconectando (" + k + "/" + RECONNECT_TRIES + ")...");
        if (CelerLink.connect(target.id, 4000)) {
            drawCtrl(null);
            return true;
        }
        System.delay(300);
    }
    return false;
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
    var press = t.touched && !wasDown;   // so a borda de descida
    wasDown = !!t.touched;

    if (mode === MODE_SCAN) {
        if (press) {
            if (hit(t, RESCAN)) {
                doScan();
            } else {
                for (var i = 0; i < itemRects.length; i++) {
                    if (!hit(t, [itemRects[i][0], itemRects[i][1], itemRects[i][2], ITEM_H + ITEM_GAP])) continue;
                    selPeer = i;
                    drawScan(null);
                    if (CelerLink.connect(peers[i].id, 5000)) {
                        enterCtrl(peers[i]);
                    } else {
                        drawScan("falha ao conectar: " + (peers[i].name || peers[i].id));
                    }
                    break;
                }
            }
        }
    } else {
        var st = CelerLink.status();
        if (!st.connected) {
            held = -1;
            if (!reconnect()) {
                backToScan("conexao perdida: " + (target.name || target.id));
                System.delay(30);
                continue;
            }
            st = CelerLink.status();
        }

        if (press && tel && tel.mode && hit(t, MODE)) {
            CelerLink.send({type: "mode"});  // o robo para e responde com tel nova
            System.delay(30);
            continue;
        }

        if (press && hit(t, BACK)) {
            backToScan(null);
            System.delay(30);
            continue;
        }

        var h2 = t.touched ? hitPad(t) : -1;
        if (h2 !== held) {
            var was = held;
            held = h2;
            drawCtrl(null);
            if (held >= 0) sendDir(held);
            else if (was >= 0 && was !== STOP_IDX) CelerLink.send({type: "stop"});  // soltou a seta
        } else if (held >= 0 && held !== STOP_IDX && System.millis() - lastSend > REPEAT_MS) {
            // segurar a seta: repete o comando (anda enquanto segura)
            sendDir(held);
        }

        // esvazia a fila: so a telemetria mais nova importa
        var redraw = false;
        for (var k = 0; k < 8; k++) {
            var m = CelerLink.poll();
            if (m === null) break;
            var v = null;
            try { v = JSON.parse(m); } catch (e) {}
            if (v && v.type === "tel") {
                tel = v;
                redraw = true;
            }
        }
        if (redraw) drawCtrl(null);
    }
    System.delay(30);
}
