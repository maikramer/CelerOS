// Sonar — o instrumento da malha (CelerNet, API 27). Três abas:
//   Radar: a vizinhança em anéis de SALTOS (1..4), cada nó com o papel
//          (cor) e o sinal do último salto (mais perto do anel = mais forte).
//   Ping:  RTT e perda de verdade para um nó escolhido — 10 pings de 1 quadro
//          (unicast sem cópias: perda medida, não mascarada).
//   Censo: chama todo mundo com o Sonar aberto e monta a tabela de quem
//          respondeu (saltos, RTT, bateria, tempo ligado).
// Enquanto aberto, o Sonar RESPONDE pings e censos dos outros.
//
// Protocolo (tudo cabe num quadro de 16 B; prefixo "s", nunca "P" — o
// serviço Pack engole mensagem crua que começa com P):
//   s?<seq>               ping (unicast)        -> s!<seq> (unicast)
//   s*<nonce>             censo (broadcast)     -> s=<nonce>|<bat%>|<min ligado>

var hasMesh = typeof CelerNet !== "undefined" && typeof Pack !== "undefined";
var T = System.theme();

var tab = 0;
var nodes = [];
var nodesAt = -10000;

// ---- ping ----------------------------------------------------------------
var PING_N = 10, PING_GAP = 1500, PING_TIMEOUT = 6000;
var pingTarget = null;     // {id, name}
var pingSel = -1;
var ping = null;           // sessão em curso/terminada
function pingStart(tgt) {
    ping = { target: tgt, sent: 0, got: 0, dup: 0, fail: 0, nextAt: System.millis(),
             out: {}, rtts: [], running: true, seqBase: Math.floor(Math.random() * 80) };
}
function pingTick(now) {
    if (!ping || !ping.running) return;
    if (ping.sent < PING_N && now >= ping.nextAt) {
        var seq = ping.seqBase + ping.sent;
        if (CelerNet.send(ping.target.id, "s?" + seq, { copies: 1, urgent: true })) {
            ping.out[seq] = now;
        } else {
            ping.fail++;          // destino sumiu da presença ou fila cheia
        }
        ping.sent++;
        ping.nextAt = now + PING_GAP;
    }
    // acabou quando nenhum ping segue no prazo (respondeu ou venceu)
    var open = 0;
    for (var k in ping.out) {
        if (ping.out.hasOwnProperty(k) && ping.out[k] > 0 && now - ping.out[k] < PING_TIMEOUT) open++;
    }
    if (ping.sent >= PING_N && open === 0) {
        ping.running = false;
        UI.invalidate();
    }
}
function pingReply(seq, now) {
    if (!ping || !ping.out.hasOwnProperty(seq)) return;
    if (ping.out[seq] <= 0) { ping.dup++; return; }
    if (now - ping.out[seq] > PING_TIMEOUT) return;   // venceu: ja contou como perda
    ping.rtts.push(now - ping.out[seq]);
    ping.out[seq] = -1;
    ping.got++;
}
function pingStats() {
    var p = ping, mn = 0, mx = 0, sum = 0;
    for (var i = 0; i < p.rtts.length; i++) {
        var r = p.rtts[i];
        if (i === 0 || r < mn) mn = r;
        if (r > mx) mx = r;
        sum += r;
    }
    var avg = p.rtts.length ? Math.round(sum / p.rtts.length) : 0;
    var done = p.sent - p.fail;
    var loss = done > 0 && !p.running ? Math.round(100 * (done - p.got) / done) : 0;
    return { min: mn, max: mx, avg: avg, loss: loss };
}

// ---- censo ---------------------------------------------------------------
var census = null;         // {nonce, at, rows: {id: {...}}}
var replyQ = [];           // respostas de censo agendadas (espalhadas no tempo)
function censusStart() {
    var nonce = "" + (10 + Math.floor(Math.random() * 89));
    census = { nonce: nonce, at: System.millis(), rows: {}, order: [] };
    if (!CelerNet.broadcast("s*" + nonce, 4)) UI.toast("Malha desligada ou fila cheia");
}
function myBat() {
    if (typeof System.batteryInfo === "function") {
        var b = System.batteryInfo();
        if (b && typeof b.pct === "number") return b.pct;
    }
    return -1;
}

// ---- recepção (sempre: o Sonar aberto responde aos outros) ---------------
function drain(now) {
    var m, got = false;
    while ((m = CelerNet.poll()) !== null) {
        var s = "" + m.msg;
        if (s.length < 3 || s.charAt(0) !== "s") continue;
        var op = s.charAt(1), arg = s.substring(2);
        if (op === "?") {
            CelerNet.send(m.from, "s!" + arg, { copies: 1, urgent: true });
        } else if (op === "!") {
            pingReply(parseInt(arg, 10), now);
            got = true;
        } else if (op === "*") {
            // espalha as respostas: todos ouviram o mesmo censo ao mesmo tempo
            replyQ.push({ to: m.from, at: now + 200 + Math.floor(Math.random() * 1800), nonce: arg });
        } else if (op === "=") {
            var f = arg.split("|");
            if (census && f[0] === census.nonce && !census.rows[m.from]) {
                census.rows[m.from] = { name: m.fromName || m.from, hops: m.hops, rssi: m.rssi,
                                        rtt: now - census.at, bat: parseInt(f[1], 10),
                                        up: parseInt(f[2], 10) };
                census.order.push(m.from);
                got = true;
            }
        }
    }
    for (var i = 0; i < replyQ.length; i++) {
        if (now >= replyQ[i].at) {
            var r = replyQ.splice(i--, 1)[0];
            CelerNet.send(r.to, "s=" + r.nonce + "|" + myBat() + "|" + Math.floor(now / 60000), { copies: 1 });
        }
    }
    if (got) UI.invalidate();
}

// ---- desenho -------------------------------------------------------------
function roleColor(c) {
    if (c & 8) return T.warn;      // patas
    if (c & 1) return T.accent;    // alto-falante
    if (c & 4) return T.ok;        // tela
    return T.textDim;
}
function hash(id) {
    var h = 7;
    for (var i = 0; i < id.length; i++) h = (h * 31 + id.charCodeAt(i)) & 0xFFFF;
    return h;
}
function drawRadar(y0) {
    var cx = 120, cy = y0 + 104;
    var R = [0, 28, 52, 76, 100];
    for (var h = 4; h >= 1; h--) {
        System.fillSmoothCircle(cx, cy, R[h], h % 2 ? T.card : T.raised);
    }
    for (var k = 1; k <= 4; k++) System.drawCircle(cx, cy, R[k], T.stroke);
    System.fillSmoothCircle(cx, cy, 7, T.accent);
    for (var i = 0; i < nodes.length; i++) {
        var n = nodes[i];
        var hops = Math.max(1, Math.min(4, n.hops));
        // sinal forte puxa para dentro do anel (-40 dBm) e fraco para fora (-95)
        var f = Math.max(0, Math.min(1, (-40 - n.rssi) / 55));
        var r = R[hops - 1] + 8 + f * (R[hops] - R[hops - 1] - 12);
        var a = (hash(n.id) % 360) * Math.PI / 180;
        var x = Math.round(cx + Math.cos(a) * r), y = Math.round(cy + Math.sin(a) * r);
        System.fillSmoothCircle(x, y, 6, roleColor(n.caps));
        UI.text(n.name || n.id, x, y + 8, { role: "caption", align: "center", w: 70 });
    }
    UI.text(nodes.length ? nodes.length + " nós · anel = saltos" : "Ouvindo o ar...",
            120, y0 + 214, { role: "caption", align: "center", color: T.textDim, id: "radarcap" });
}

function nodeItems() {
    var items = [];
    for (var i = 0; i < nodes.length; i++) {
        var n = nodes[i];
        var bars = n.rssi > -55 ? 4 : n.rssi > -67 ? 3 : n.rssi > -78 ? 2 : 1;
        items.push({ label: n.name || n.id, sub: n.hops + (n.hops === 1 ? " salto" : " saltos") +
                     " · " + n.rssi + " dBm", bars: bars });
    }
    if (!items.length) items.push({ label: "Ninguém ouvido ainda", enabled: false });
    return items;
}

function gateScreen(full) {
    if (!hasMesh) {
        UI.card(10, 56, 220, 90);
        UI.text("Esta placa não tem Bluetooth no firmware.", 22, 70, { w: 196, lines: 3 });
        UI.cardEnd();
        return;
    }
    UI.card(10, 56, 220, 110);
    UI.text("A malha está desligada.", 22, 70, { w: 196 });
    UI.text("O Sonar mede a malha CelerNet: ligue para ouvir os vizinhos.", 22, 94,
            { role: "caption", w: 196, lines: 3, color: T.textDim });
    UI.cardEnd();
    if (UI.button("Ligar a malha", 10, 176, 220, 36)) {
        if (!CelerNet.start({})) UI.toast("Sem RAM para o rádio agora");
    }
}

while (true) {
    var full = UI.begin();
    if (UI.header("Sonar", { back: true, sub: hasMesh ? CelerNet.status().name : "" })) System.exitApp();
    if (!hasMesh || !CelerNet.status().active) {
        gateScreen(full);
        UI.end();
        continue;
    }
    var now = System.millis();
    if (now - nodesAt > 1000) {
        nodesAt = now;
        var fresh = CelerNet.nodes();
        if (JSON.stringify(fresh) !== JSON.stringify(nodes)) UI.invalidate();
        nodes = fresh;
    }
    var nt = UI.tabs(10, 48, 220, 30, ["Radar", "Ping", "Censo"], tab);
    if (nt !== tab) { tab = nt; UI.invalidate(); }

    if (tab === 0) {
        if (full) drawRadar(84);
    } else if (tab === 1) {
        var hit = UI.list("pingNodes", 10, 86, 220, 104, nodeItems(), { rowH: 34, selected: pingSel });
        if (hit >= 0 && nodes[hit]) {
            pingSel = hit;
            pingTarget = { id: nodes[hit].id, name: nodes[hit].name || nodes[hit].id };
            UI.invalidate();
        }
        UI.card(10, 198, 220, 80);
        if (ping) {
            var st = pingStats();
            UI.text(ping.target.name + ": " + ping.got + "/" + ping.sent +
                    (ping.running ? " ..." : " · perda " + st.loss + "%"), 20, 206, { w: 200, id: "p1" });
            UI.text(ping.rtts.length ? "RTT " + st.min + " / " + st.avg + " / " + st.max + " ms" : "RTT -",
                    20, 228, { role: "caption", w: 200, id: "p2" });
            UI.progress(20, 252, 200, 8, Math.round(100 * ping.sent / PING_N));
            if (ping.dup) UI.text(ping.dup + " dup", 200, 228, { role: "caption", align: "right", id: "p3" });
        } else {
            UI.text(pingTarget ? "Alvo: " + pingTarget.name : "Escolha um nó acima", 20, 206, { w: 200, id: "p1" });
            UI.text("10 pings de 1 quadro, sem cópias", 20, 228,
                    { role: "caption", color: T.textDim, w: 200, id: "p2" });
        }
        UI.cardEnd();
        var busy = ping && ping.running;
        if (UI.button(busy ? "Parar" : "Pingar 10x", 10, 284, 220, 30,
                      { disabled: !pingTarget && !busy, style: busy ? "danger" : "primary" })) {
            if (busy) ping.running = false;
            else pingStart(pingTarget);
        }
    } else {
        if (UI.button("Chamar todos", 10, 86, 220, 32)) censusStart();
        var rows = [];
        if (census) {
            for (var c = 0; c < census.order.length; c++) {
                var r = census.rows[census.order[c]];
                rows.push({ label: r.name, sub: r.hops + (r.hops === 1 ? " salto · " : " saltos · ") + r.rtt +
                            " ms · " + r.up + " min",
                            right: r.bat >= 0 ? r.bat + "%" : "USB" });
            }
            if (!rows.length) rows.push({ label: "Esperando respostas...", enabled: false });
        } else {
            rows.push({ label: "Toque para chamar quem tem o Sonar aberto", enabled: false });
        }
        UI.list("census", 10, 126, 220, 188, rows, { rowH: 46 });
    }

    drain(now);
    pingTick(now);
    UI.end();
}
