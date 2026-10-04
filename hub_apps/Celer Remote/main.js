// Celer Remote (API 11; WiFi do robo com API 21; truques + respostas com o
// Dog Face 1.7+): controle remoto via Celer Link (Bluetooth).
// Escaneia CelerOS proximos, conecta e pilota com um D-pad na tela.
// Mensagens JSON: {type:"move",dir} / {type:"stop"}; o lado do robo
// responde com {type:"tel",...}.
//
// Pareamento (API 11): robo com {pairing:true} mostra um codigo de 6
// digitos na tela dele — status().pairing acende aqui e o usuario digita
// (verify no firmware). Robo memoriza controles pareados: reconexao cai
// direto no D-pad sem pedir nada.
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
var WIFI_BTN = [W - 64, 0, 64, 40];  // "WiFi" no canto superior direito (API 21)
// Faixa inferior: marcha na metade esquerda ({type:"mode"}) e truques na
// direita ({type:"trick"} — grade com o que o robo anunciou em tel.tricks)
var MODE = [10, 254, 105, 30];
var TRICKS = [125, 254, 105, 30];
var held = -1;
var lastSend = 0;

// Nota que sobrevive aos redesenhos da telemetria (chega a cada 1,5 s e
// apagava na hora o "robo online: IP" do WiFi)
var stickyNote = null, stickyUntil = 0;
function stickNote(n, ms) { stickyNote = n; stickyUntil = System.millis() + (ms || 8000); }

function drawCtrl(note) {
    if (!note && stickyNote && System.millis() < stickyUntil) note = stickyNote;
    System.fillScreen(TH.bg);
    var st = CelerLink.status();
    txt("< sair", 8, 6, 1, TH.accent);
    var head = st.connected ? ((target && target.name) || st.peer || "?") : "desconectado";
    if (st.connected && st.rssi) head += "  " + st.rssi + " dBm";
    var wifi = wifiAvailable();
    txt(head.substring(0, wifi ? 16 : 30), 60, 6, 1, st.connected ? TH.ok : TH.err);
    if (wifi) txt("WiFi", W - 44, 6, 1, tel.net ? TH.ok : TH.accent);  // verde = robo online
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
    // Troca de marcha: so com robo que lista os modos dele (tel.modes) E ha
    // mais de um. O botao das 1.2/1.3 cicla as cegas {"type":"mode"}: um
    // toque sem querer salvava no robo uma marcha que nao anda (o esphi do
    // Dog Face) e o robo "nao saia do lugar" em todos os boots seguintes.
    // Agora o destino vem por nome, escolhido da lista do proprio robo.
    if (tel && tel.mode && tel.modes && tel.modes.length > 1) {
        System.fillRect(MODE[0], MODE[1], MODE[2], MODE[3], TH.card);
        System.drawRect(MODE[0], MODE[1], MODE[2], MODE[3], TH.stroke);
        center("marcha: " + tel.mode, MODE[1] + (MODE[3] - fh(1)) / 2, 1, TH.text);
    }
    // Truques que o ROBO anunciou (tel.tricks): robo velho/sem truques nao mostra
    if (tel && tel.tricks && tel.tricks.length) {
        System.fillRect(TRICKS[0], TRICKS[1], TRICKS[2], TRICKS[3], TH.raised);
        System.drawRect(TRICKS[0], TRICKS[1], TRICKS[2], TRICKS[3], TH.stroke);
        center("truques (" + tel.tricks.length + ")", TRICKS[1] + (TRICKS[3] - fh(1)) / 2, 1, TH.text);
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

function enterCtrl(p, note) {
    target = p;
    mode = MODE_CTRL;
    tel = null;
    held = -1;
    drawCtrl(note || null);
}

function backToScan(msg) {
    var st = CelerLink.status();
    if (st.connected || st.pairing) {
        // pendente (pairing) nao tem canal de dados: so desconecta
        if (st.connected) CelerLink.send({type: "stop"});
        CelerLink.disconnect();
    }
    mode = MODE_SCAN;
    target = null;
    held = -1;
    drawScan(msg || null);
}

// Robo com pareamento (status().pairing): pede o codigo ao usuario e
// verifica no firmware. "ok"/"cancel"/"fail" — em cancel/fail a sessao ja
// voltou pro scan (o robo derruba sozinho em 3 erros ou 60 s sem digitar).
function ensurePairing(name) {
    var note = null;
    for (var wrong = 0; wrong < 3; ) {
        System.fillScreen(TH.bg);
        center("pareamento", 54, 2, TH.text);
        center(name || "robo", 54 + fh(2) + 6, 1, TH.textDim);
        center("codigo na tela do robo", 54 + fh(2) + 6 + fh(1) + 10, 1, TH.accent);
        if (note) center(note, 200, 1, TH.err);
        var c = System.prompt("codigo do robo (6 digitos)", "", {hint: "num"});
        if (!c) {
            backToScan("pareamento cancelado");
            return "cancel";
        }
        if (!/^[0-9]{6}$/.test(c)) { note = "so numeros, 6 digitos"; continue; }
        if (CelerLink.verify(c)) return "ok";
        wrong++;
        note = "codigo errado (" + wrong + "/3)";
    }
    backToScan("codigo errado 3x");
    return "fail";
}

// Pos-conexao (novo ou reconexao): peer com pareamento pede codigo antes
// do D-pad. true = link pronto para controlar.
function afterConnect(p) {
    if (!CelerLink.status().pairing) return true;
    return ensurePairing(p && (p.name || p.id)) === "ok";
}

// Link caiu: tenta o mesmo par algumas vezes (o robo segue anunciando).
function reconnect() {
    for (var k = 1; k <= RECONNECT_TRIES; k++) {
        drawCtrl("reconectando (" + k + "/" + RECONNECT_TRIES + ")...");
        if (CelerLink.connect(target.id, 4000) && afterConnect(target)) {
            drawCtrl(null);
            return true;
        }
        if (mode !== MODE_CTRL) return false;  // pareamento cancelado: ja foi pro scan
        System.delay(300);
    }
    return false;
}

// ---- WiFi do robo (API 21) -----------------------------------------------
// O robo nao tem teclado: ele escaneia as redes que ELE ve e devolve a lista
// ({type:"wifi_scan"} -> {type:"wifi_list"}); a senha vai SELADA
// (CelerLink.sendSealed: AES-GCM com a chave do pareamento — no ar, quem nao
// gravou o proprio pareamento nao le) e ele responde {type:"wifi_res"}.
function wifiAvailable() {
    return !!(tel && tel.wifi && typeof CelerLink.sendSealed === "function");
}

// Espera uma mensagem do tipo pedido (a telemetria segue atualizando).
function waitMsg(type, ms) {
    var t0 = System.millis();
    while (System.millis() - t0 < ms) {
        for (var k = 0; k < 8; k++) {
            var m = CelerLink.poll();
            if (m === null) break;
            var v = null;
            try { v = JSON.parse(m); } catch (e) {}
            if (v && v.type === "tel") tel = v;
            if (v && v.type === type) return v;
        }
        if (!CelerLink.status().connected) return null;
        System.delay(50);
    }
    return null;
}

function wifiScreen(title, line, color) {
    System.fillScreen(TH.bg);
    center(title, 40, 2, TH.text);
    if (line) center(line, 40 + fh(2) + 14, 1, color || TH.textDim);
}

// Fluxo inteiro; devolve a nota que volta para a tela do D-pad.
function wifiSetup() {
    wifiScreen("WiFi do robo", "procurando redes...", TH.accent);
    if (!CelerLink.send({type: "wifi_scan"})) return "falha ao pedir o scan";
    var res = waitMsg("wifi_list", 9000);
    if (!res) return "robo nao respondeu ao scan";
    var nets = res.nets || [];
    var ROW = 30, y0 = 40 + fh(2) + 14, items = [];
    System.fillScreen(TH.bg);
    center("WiFi do robo", 10, 2, TH.text);
    center(nets.length ? "toque na rede" : "nenhuma rede vista pelo robo", 10 + fh(2) + 4, 1, TH.textDim);
    for (var i = 0; i < nets.length && items.length < 6; i++) {
        var r = [10, y0 + items.length * (ROW + 4), W - 20, ROW];
        items.push({ r: r, ssid: nets[i][0], secure: nets[i][2] === 1 });
        System.fillRect(r[0], r[1], r[2], r[3], TH.card);
        System.drawRect(r[0], r[1], r[2], r[3], TH.stroke);
        txt((nets[i][2] ? "* " : "  ") + nets[i][0], 16, r[1] + (ROW - fh(1)) / 2, 1, TH.text);
        txt(nets[i][1] + "", W - 44, r[1] + (ROW - fh(1)) / 2, 1, TH.textDim);
    }
    var other = [10, y0 + items.length * (ROW + 4), W - 20, ROW];
    System.fillRect(other[0], other[1], other[2], other[3], TH.raised);
    center("outra rede (digitar nome)", other[1] + (ROW - fh(1)) / 2, 1, TH.text);
    var cancel = [10, H - 46, W - 20, 36];
    System.fillRect(cancel[0], cancel[1], cancel[2], cancel[3], TH.card);
    System.drawRect(cancel[0], cancel[1], cancel[2], cancel[3], TH.stroke);
    center("cancelar", cancel[1] + (cancel[3] - fh(1)) / 2, 1, TH.text);

    var ssid = null, secure = true, down = true;  // espera soltar o toque do botao
    while (ssid === null) {
        var tt = System.getTouch();
        var pr = tt.touched && !down;
        down = !!tt.touched;
        if (pr) {
            if (hit(tt, cancel)) return "WiFi: cancelado";
            if (hit(tt, other)) {
                var nm = System.prompt("nome da rede (SSID)", "", {nullOnCancel: true});
                if (!nm) return "WiFi: cancelado";
                ssid = nm;
            }
            for (var j = 0; j < items.length && ssid === null; j++) {
                if (hit(tt, items[j].r)) { ssid = items[j].ssid; secure = items[j].secure; }
            }
        }
        if (!CelerLink.status().connected) return "conexao perdida";
        System.delay(30);
    }
    var pass = "";
    if (secure) {
        pass = System.prompt("senha de " + ssid, "", {mask: true, nullOnCancel: true});
        if (pass === null || pass === undefined) return "WiFi: cancelado";
    }
    wifiScreen("WiFi do robo", "enviando (cifrado)...", TH.accent);
    var ok = CelerLink.sendSealed({type: "wifi", ssid: ssid, pass: pass});
    pass = null;
    if (!ok) return "falha ao enviar (pareie de novo pelo codigo)";
    wifiScreen("WiFi do robo", "robo conectando em " + ssid + "...", TH.accent);
    var r2 = waitMsg("wifi_res", 25000);
    if (!r2) return "sem resposta do robo";
    return r2.ok ? "robo online: " + r2.ip : "robo nao conectou em " + ssid + " (senha?)";
}

// ---- truques do cao ---------------------------------------------------------
// Grade com os truques que o ROBO anunciou na telemetria (tel.tricks: os
// nativos + os que o dono ensinou). Toque manda {type:"trick",name} e o
// robo responde {type:"trick_res",ok} — ok:false = recusou (bateria fraca
// recusa truque pesado com ganido: o robo faz drama, o controle conta).
function trickScreen() {
    var names = tel.tricks || [];
    var ROW = 30, y0 = 40 + fh(2) + 14, items = [];
    System.fillScreen(TH.bg);
    center("truques do cao", 10, 2, TH.text);
    center(names.length ? "toque para rodar" : "o cao nao listou truques",
           10 + fh(2) + 4, 1, TH.textDim);
    for (var i = 0; i < names.length && items.length < 6; i++) {
        var r = [10, y0 + items.length * (ROW + 4), W - 20, ROW];
        items.push({ r: r, name: names[i] });
        System.fillRect(r[0], r[1], r[2], r[3], TH.card);
        System.drawRect(r[0], r[1], r[2], r[3], TH.stroke);
        txt(names[i], 16, r[1] + (ROW - fh(1)) / 2, 1, TH.text);
    }
    var back = [10, H - 46, W - 20, 36];
    System.fillRect(back[0], back[1], back[2], back[3], TH.card);
    System.drawRect(back[0], back[1], back[2], back[3], TH.stroke);
    center("voltar", back[1] + (back[3] - fh(1)) / 2, 1, TH.text);

    var down = true;   // espera soltar o toque que abriu a tela
    while (true) {
        var tt = System.getTouch();
        var pr = tt.touched && !down;
        down = !!tt.touched;
        if (pr) {
            if (hit(tt, back)) return null;
            for (var q = 0; q < items.length; q++) {
                if (!hit(tt, items[q].r)) continue;
                var nm = items[q].name;
                if (!CelerLink.send({ type: "trick", name: nm })) return "falha ao enviar " + nm;
                drawCtrl("rodando " + nm + "...");
                var res = waitMsg("trick_res", 6000);
                if (!res) return nm + ": sem resposta";
                return nm + (res.ok ? "!" : " recusou (bateria?)");
            }
        }
        if (!CelerLink.status().connected) return "conexao perdida";
        System.delay(30);
    }
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
                        if (afterConnect(peers[i])) enterCtrl(peers[i], "pareado!");
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
                if (mode === MODE_CTRL) {  // pareamento cancelado ja foi pro scan
                    backToScan("conexao perdida: " + (target.name || target.id));
                }
                System.delay(30);
                continue;
            }
            st = CelerLink.status();
        }

        if (press && tel && tel.mode && tel.modes && tel.modes.length > 1 && hit(t, MODE)) {
            var next = tel.modes[(tel.modes.indexOf(tel.mode) + 1) % tel.modes.length];
            CelerLink.send({type: "mode", walk: next});  // nomeado: sem ciclo cego
            System.delay(30);
            continue;
        }

        if (press && tel && tel.tricks && tel.tricks.length && hit(t, TRICKS)) {
            held = -1;
            var tnote = trickScreen();
            wasDown = true;  // o toque que fechou a grade nao vira seta
            if (tnote) stickNote(tnote, 6000);
            if (mode === MODE_CTRL) drawCtrl(tnote || null);
            System.delay(30);
            continue;
        }

        if (press && hit(t, BACK)) {
            backToScan(null);
            System.delay(30);
            continue;
        }

        if (press && wifiAvailable() && hit(t, WIFI_BTN)) {
            held = -1;
            var wnote = wifiSetup();
            wasDown = true;  // o toque que fechou o fluxo nao vira seta
            stickNote(wnote, 8000);
            if (mode === MODE_CTRL) drawCtrl(wnote);
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
            if (v && v.type === "say") {
                // resposta do cao (voz 2.0: dog_say) — nota na tela
                stickNote("cao: " + String(v.text || "").substring(0, 60), 6000);
                redraw = true;
            }
        }
        if (redraw) drawCtrl(null);
    }
    System.delay(30);
}
