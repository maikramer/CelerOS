// Celer Remote (toolkit UI, API 22; WiFi do robo com API 21; truques +
// respostas com o Dog Face 1.7+): controle remoto via Celer Link (Bluetooth).
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
// Interface: lista de pares com sinal (UI.list), cabecalho nativo, D-pad
// desenhado a mao (circulo + setas suavizadas, redesenho so quando a seta
// segurada muda), botoes de marcha/truques/WiFi e avisos numa linha sob o
// status (no scan, erros de conexao vao em UI.toast).

var W = 240, H = 320;
var TH = System.theme();
var LX = 8, LW = 224;

var MODE_SCAN = 0, MODE_CTRL = 1;
var mode = MODE_SCAN;
var peers = [];
var target = null;      // par conectado (para reconectar)
var tel = null;

var REPEAT_MS = 250;    // repeticao do move (o robo tem keepalive de ~900 ms)
var RECONNECT_TRIES = 3;

// ---- helpers ----------------------------------------------------------------
// Avisos da tela de controle (say do cao, truque, IP do WiFi): fila mostrada
// numa linha sob o status — um por vez, cada um pelo seu tempo; sobrevivem
// aos redesenhos da telemetria
var notes = [], noteUntil = 0;
function note(msg, ms) {
    notes.push({ t: msg, ms: ms || 6000 });
    if (notes.length > 4) notes.shift();
    if (notes.length === 1) noteUntil = System.millis() + notes[0].ms;
}
function curNote() {
    while (notes.length && System.millis() >= noteUntil) {
        notes.shift();
        if (notes.length) noteUntil = System.millis() + notes[0].ms;
    }
    return notes.length ? notes[0].t : "";
}
function inR(t, r) { return t.x >= r[0] && t.x < r[0] + r[2] && t.y >= r[1] && t.y < r[1] + r[3]; }

// Tela "avulsa" (fora do laco): espera bloqueante de scan/connect/link
function busy(title, line) {
    UI.invalidate();
    UI.begin(TH.bg);
    UI.header(title);
    UI.spinner(120, 138, 18);
    if (line) UI.text(line, 120, 172, { align: "center", color: TH.textDim, w: LW, lines: 2 });
    UI.end();
}

// Barras de sinal pelo RSSI (0..4)
function bars(rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -78) return 2;
    if (rssi >= -88) return 1;
    return 0;
}

// Esvazia a fila do link: telemetria nova, respostas do cao (say) e, se
// pedido, devolve a primeira mensagem do tipo `want`
function pollLink(want) {
    for (var k = 0; k < 8; k++) {
        var m = CelerLink.poll();
        if (m === null) break;
        var v = null;
        try { v = JSON.parse(m); } catch (e) {}
        if (!v) continue;
        if (v.type === "tel") tel = v;
        else if (v.type === "say") note("cão: " + String(v.text || "").substring(0, 60), 6000);
        if (want && v.type === want) return v;
    }
    return null;
}

// Espera uma mensagem do tipo pedido (a telemetria segue atualizando)
function waitMsg(type, ms) {
    var t0 = System.millis();
    while (System.millis() - t0 < ms) {
        var v = pollLink(type);
        if (v) return v;
        if (!CelerLink.status().connected) return null;
        System.delay(50);
    }
    return null;
}

// ---- tela de scan --------------------------------------------------------------
function doScan() {
    busy("Celer Remote", "procurando CelerOS por perto...");
    peers = CelerLink.scan(3000);
    // o firmware ja ordena por RSSI; ordena de novo por garantia (fw antigo)
    peers.sort(function (a, b) { return b.rssi - a.rssi; });
    UI.resetScroll("peers");
    UI.invalidate();
}

function scanFrame() {
    UI.header("Celer Remote", { sub: peers.length ? peers.length + " por perto" : "" });
    if (peers.length === 0) {
        UI.card(LX, 56, LW, 120);
        UI.text("Nenhum CelerOS por perto", 120, 76, { role: "title", align: "center", color: TH.textDim, w: LW - 16 });
        UI.text("Ligue o robô (ou outro aparelho com Celer Link) e procure de novo.", 120, 116,
                { role: "caption", align: "center", color: TH.textDim, w: LW - 24, lines: 3 });
        UI.cardEnd();
    } else {
        var rows = [];
        for (var i = 0; i < peers.length; i++) {
            rows.push({ label: peers[i].name || "(sem nome)", sub: peers[i].id,
                        right: peers[i].rssi + " dBm", bars: bars(peers[i].rssi) });
        }
        var k = UI.list("peers", LX, 48, LW, 214, rows, { rowH: 48 });
        if (k >= 0) connectTo(peers[k]);
    }
    if (UI.button("Procurar de novo", LX, 272, LW, 40, { style: peers.length ? "ghost" : "primary" })) doScan();
}

function connectTo(p) {
    busy("Conectando", p.name || p.id);
    if (CelerLink.connect(p.id, 5000)) {
        if (afterConnect(p)) enterCtrl(p, "pareado!");
    } else {
        UI.toast("falha ao conectar: " + (p.name || p.id), 4000);
        UI.invalidate();
    }
}

// ---- pareamento ------------------------------------------------------------------
// Robo com pareamento (status().pairing): pede o codigo ao usuario e
// verifica no firmware. "ok"/"cancel"/"fail" — em cancel/fail a sessao ja
// voltou pro scan (o robo derruba sozinho em 3 erros ou 60 s sem digitar).
function pairingFrame(name, msg) {
    UI.invalidate();
    UI.begin(TH.bg);
    UI.header("Pareamento", { sub: name || "robô" });
    UI.card(LX, 60, LW, 110);
    UI.text("Digite o código", 120, 76, { role: "title", align: "center" });
    UI.text("código na tela do robô", 120, 108, { align: "center", color: TH.accent });
    UI.text("6 dígitos, válido por 60 s", 120, 136, { role: "caption", align: "center", color: TH.textDim });
    UI.cardEnd();
    if (msg) UI.text(msg, 120, 190, { align: "center", color: TH.err, w: LW });
    UI.end();
}
function ensurePairing(name) {
    var err = null;
    for (var wrong = 0; wrong < 3; ) {
        pairingFrame(name, err);
        var c = System.prompt("codigo do robo (6 digitos)", "", {hint: "num"});
        if (!c) {
            backToScan("pareamento cancelado");
            return "cancel";
        }
        if (!/^[0-9]{6}$/.test(c)) { err = "só números, 6 dígitos"; continue; }
        if (CelerLink.verify(c)) return "ok";
        wrong++;
        err = "código errado (" + wrong + "/3)";
    }
    backToScan("código errado 3x");
    return "fail";
}

// Pos-conexao (novo ou reconexao): peer com pareamento pede codigo antes
// do D-pad. true = link pronto para controlar.
function afterConnect(p) {
    if (!CelerLink.status().pairing) return true;
    return ensurePairing(p && (p.name || p.id)) === "ok";
}

// ---- tela de controle (D-pad) -------------------------------------------------
// D-pad: [x, y, w, h, dir] — areas de toque (o desenho e o circulo por cima)
var CX = 120, CY = 168, PR = 80;
var PAD = [
    [88, 92, 64, 50, "up"],
    [88, 196, 64, 50, "down"],
    [36, 140, 54, 56, "left"],
    [150, 140, 54, 56, "right"],
    [94, 144, 52, 48, "stop"]
];
var STOP_IDX = 4;
var MODE_BTN = [LX, 256, 108, 30];
var TRICK_BTN = [LX + 116, 256, 108, 30];
var WIFI_BTN = [W - 62, 4, 56, 32];
var held = -1;
var padDrawn = -2;      // seta desenhada por ultimo (-2 = redesenhar)
var lastSend = 0;
var chromeSig = "";     // muda quando botoes aparecem/somem (tel.modes/tricks/wifi)

function wifiAvailable() {
    return !!(tel && tel.wifi && typeof CelerLink.sendSealed === "function");
}
function hasModes() { return !!(tel && tel.mode && tel.modes && tel.modes.length > 1); }
function hasTricks() { return !!(tel && tel.tricks && tel.tricks.length); }

// Desenho proprio do D-pad: so quando a seta segurada muda ou frame total
function drawPad(full) {
    if (!full && padDrawn === held) return;
    padDrawn = held;
    System.fillSmoothCircle(CX, CY, PR, TH.card);
    System.fillSmoothCircle(CX, CY, PR - 2, TH.raised);
    var A = 16;  // meia largura da seta
    var tris = [
        [CX, CY - 64, CX - A, CY - 40, CX + A, CY - 40],   // up
        [CX, CY + 64, CX - A, CY + 40, CX + A, CY + 40],   // down
        [CX - 64, CY, CX - 40, CY - A, CX - 40, CY + A],   // left
        [CX + 64, CY, CX + 40, CY - A, CX + 40, CY + A]    // right
    ];
    for (var i = 0; i < 4; i++) {
        var t = tris[i];
        if (held === i) {
            var hx = (t[0] + t[2] + t[4]) / 3, hy = (t[1] + t[3] + t[5]) / 3;
            System.fillSmoothCircle(Math.round(hx), Math.round(hy), 26, TH.accentD);
        }
        System.fillTriangle(t[0], t[1], t[2], t[3], t[4], t[5], held === i ? TH.accent : TH.text);
    }
    System.fillSmoothCircle(CX, CY, 24, held === STOP_IDX ? TH.err : TH.bg);
    System.fillSmoothRoundRect(CX - 8, CY - 8, 16, 16, 3, held === STOP_IDX ? TH.text : TH.err);
}

function hitPad(t) {
    for (var i = 0; i < PAD.length; i++) if (inR(t, PAD[i])) return i;
    return -1;
}

function sendDir(i) {
    var dir = PAD[i][4];
    CelerLink.send(dir === "stop" ? {type: "stop"} : {type: "move", dir: dir});
    lastSend = System.millis();
}

function enterCtrl(p, msg) {
    target = p;
    mode = MODE_CTRL;
    tel = null;
    held = -1;
    chromeSig = "";
    if (msg) note(msg, 2500);
    UI.invalidate();
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
    if (msg) UI.toast(msg, 4000);
    UI.invalidate();
}

// Link caiu: tenta o mesmo par algumas vezes (o robo segue anunciando).
function reconnect() {
    for (var k = 1; k <= RECONNECT_TRIES; k++) {
        busy((target && target.name) || "Robô", "reconectando (" + k + "/" + RECONNECT_TRIES + ")...");
        if (CelerLink.connect(target.id, 4000) && afterConnect(target)) {
            UI.invalidate();
            return true;
        }
        if (mode !== MODE_CTRL) return false;  // pareamento cancelado: ja foi pro scan
        System.delay(300);
    }
    return false;
}

function ctrlFrame(full) {
    var st = CelerLink.status();
    var name = (target && target.name) || st.peer || "?";
    if (UI.header(name, { back: true, sub: wifiAvailable() ? "" : (st.rssi ? st.rssi + " dBm" : "") })) {
        backToScan(null);
        return;
    }
    // WiFi do robo (API 21): verde = robo ja online
    if (wifiAvailable() && UI.button("WiFi", WIFI_BTN[0], WIFI_BTN[1], WIFI_BTN[2], WIFI_BTN[3],
                                     { color: tel.net ? TH.ok : TH.accentD, textColor: tel.net ? TH.onAccent : TH.text,
                                       role: "caption" })) {
        held = -1;
        note(wifiSetup(), 8000);
        UI.invalidate();
        return;
    }

    // estado: conexao + telemetria
    UI.badge(st.connected ? "conectado" : "desconectado", LX + 2, 48,
             { color: st.connected ? TH.ok : TH.err, textColor: TH.onAccent });
    UI.text(tel ? "batt " + tel.batt + " mV" + (tel.state ? "  ·  " + tel.state : "") : "aguardando telemetria...",
            W - LX - 2, 50, { role: "caption", align: "right", color: TH.textDim, w: 140 });
    UI.text(curNote(), 120, 66,
            { role: "caption", align: "center", color: TH.accent, w: LW, lines: 2 });

    drawPad(full);

    // Troca de marcha: so com robo que lista os modos dele (tel.modes) E ha
    // mais de um — o destino vai por nome, escolhido da lista do proprio
    // robo (um ciclo as cegas salvava no robo uma marcha que nao anda)
    if (hasModes() && UI.button("marcha: " + tel.mode, MODE_BTN[0], MODE_BTN[1], MODE_BTN[2], MODE_BTN[3],
                                { style: "ghost", role: "caption" })) {
        var next = tel.modes[(tel.modes.indexOf(tel.mode) + 1) % tel.modes.length];
        CelerLink.send({type: "mode", walk: next});
    }
    // Truques que o ROBO anunciou (tel.tricks): robo velho/sem truques nao mostra
    if (hasTricks() && UI.button("truques (" + tel.tricks.length + ")", TRICK_BTN[0], TRICK_BTN[1],
                                 TRICK_BTN[2], TRICK_BTN[3], { style: "ghost", role: "caption" })) {
        held = -1;
        var tnote = trickScreen();
        if (tnote) note(tnote, 6000);
        UI.invalidate();
        return;
    }
    UI.text("segure para andar · soltar para", 120, 298, { role: "caption", align: "center", color: TH.textDim });

    // D-pad: segurar repete o move; soltar a seta manda stop
    var t = UI.touch();
    var h2 = t.down ? hitPad(t) : -1;
    if (h2 !== held) {
        var was = held;
        held = h2;
        if (held >= 0) sendDir(held);
        else if (was >= 0 && was !== STOP_IDX) CelerLink.send({type: "stop"});  // soltou a seta
    } else if (held >= 0 && held !== STOP_IDX && System.millis() - lastSend > REPEAT_MS) {
        sendDir(held);  // segurar a seta: repete o comando (anda enquanto segura)
    }
}

// ---- WiFi do robo (API 21) -----------------------------------------------
// O robo nao tem teclado: ele escaneia as redes que ELE ve e devolve a lista
// ({type:"wifi_scan"} -> {type:"wifi_list"}); a senha vai SELADA
// (CelerLink.sendSealed: AES-GCM com a chave do pareamento — no ar, quem nao
// gravou o proprio pareamento nao le) e ele responde {type:"wifi_res"}.
// Fluxo inteiro; devolve a nota que volta para a tela do D-pad.
function wifiSetup() {
    busy("WiFi do robô", "procurando redes...");
    if (!CelerLink.send({type: "wifi_scan"})) return "falha ao pedir o scan";
    var res = waitMsg("wifi_list", 9000);
    if (!res) return "robô não respondeu ao scan";
    var nets = res.nets || [];
    var rows = [];
    for (var i = 0; i < nets.length; i++) {
        rows.push({ label: nets[i][0], sub: nets[i][2] ? "protegida" : "aberta",
                    right: nets[i][1] + " dBm", bars: bars(nets[i][1]) });
    }
    rows.push({ label: "Outra rede...", sub: "digitar o nome (SSID)" });

    var ssid = null, secure = true;
    UI.resetScroll("wifi");
    UI.invalidate();
    while (ssid === null) {
        UI.begin(TH.bg);
        if (UI.header("WiFi do robô", { back: true, sub: nets.length ? "" : "nenhuma rede vista" })) {
            UI.end();
            return "WiFi: cancelado";
        }
        var k = UI.list("wifi", LX, 48, LW, 264, rows, { rowH: 40 });
        UI.end();
        if (k === rows.length - 1) {
            var nm = System.prompt("nome da rede (SSID)", "", {nullOnCancel: true});
            if (!nm) return "WiFi: cancelado";
            ssid = nm;
        } else if (k >= 0) {
            ssid = nets[k][0];
            secure = nets[k][2] === 1;
        }
        pollLink(null);
        if (!CelerLink.status().connected) return "conexão perdida";
    }
    var pass = "";
    if (secure) {
        pass = System.prompt("senha de " + ssid, "", {mask: true, nullOnCancel: true});
        if (pass === null || pass === undefined) return "WiFi: cancelado";
    }
    busy("WiFi do robô", "enviando (cifrado)...");
    var ok = CelerLink.sendSealed({type: "wifi", ssid: ssid, pass: pass});
    pass = null;
    if (!ok) return "falha ao enviar (pareie de novo pelo código)";
    busy("WiFi do robô", "robô conectando em " + ssid + "...");
    var r2 = waitMsg("wifi_res", 25000);
    if (!r2) return "sem resposta do robô";
    return r2.ok ? "robô online: " + r2.ip : "robô não conectou em " + ssid + " (senha?)";
}

// ---- truques do cao ---------------------------------------------------------
// Lista com os truques que o ROBO anunciou na telemetria (tel.tricks: os
// nativos + os que o dono ensinou). Toque manda {type:"trick",name} e o
// robo responde {type:"trick_res",ok} — ok:false = recusou (bateria fraca
// recusa truque pesado com ganido: o robo faz drama, o controle conta).
function trickScreen() {
    var names = tel.tricks || [];
    UI.resetScroll("tricks");
    UI.invalidate();
    while (true) {
        UI.begin(TH.bg);
        if (UI.header("Truques do cão", { back: true, sub: names.length + " no robô" })) {
            UI.end();
            return null;
        }
        var k = UI.list("tricks", LX, 48, LW, 264, names, { rowH: 36 });
        UI.end();
        if (k >= 0) {
            var nm = names[k];
            if (!CelerLink.send({ type: "trick", name: nm })) return "falha ao enviar " + nm;
            busy("Truques do cão", "rodando " + nm + "...");
            var res = waitMsg("trick_res", 6000);
            if (!res) return nm + ": sem resposta";
            return nm + (res.ok ? "!" : " recusou (bateria?)");
        }
        pollLink(null);
        if (!CelerLink.status().connected) return "conexão perdida";
    }
}

// ---- sem Celer Link (placa sem BT) ---------------------------------------
if (typeof CelerLink === "undefined") {
    UI.begin(TH.bg);
    UI.header("Celer Remote");
    UI.end();
    UI.alert("Sem Celer Link", "Esta placa não tem Bluetooth (desativado no build).", "Sair");
    System.exitApp();
}

// ---- laco principal -------------------------------------------------------
doScan();  // primeiro scan automatico
while (true) {
    if (mode === MODE_CTRL && !CelerLink.status().connected) {
        held = -1;
        if (!reconnect()) {
            if (mode === MODE_CTRL) {  // pareamento cancelado ja foi pro scan
                backToScan("conexão perdida: " + (target.name || target.id));
            }
            continue;
        }
    }
    if (mode === MODE_CTRL) {
        // esvazia a fila: so a telemetria mais nova importa; botoes que
        // aparecem/somem com a telemetria pedem frame total
        pollLink(null);
        var sig = (hasModes() ? "m" : "") + (hasTricks() ? "t" + tel.tricks.length : "") + (wifiAvailable() ? "w" : "");
        if (sig !== chromeSig) {
            chromeSig = sig;
            UI.invalidate();
        }
    }

    var full = UI.begin(TH.bg);
    if (full) padDrawn = -2;
    if (mode === MODE_SCAN) scanFrame();
    else ctrlFrame(full);
    UI.end();
}
