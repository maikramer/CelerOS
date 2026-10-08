// MeshLab — bateria de bancada da CelerNet e do Celer Link (3 placas). NÃO é
// app de fábrica. Todo nó com o MeshLab aberto RESPONDE (pings, contagem de
// broadcasts/unicasts, eco do link); o nó cujo /local/meshlab_plan.txt diz
// "master" roda os testes. Cada nó grava o que viu em /local/meshlab_out.txt
// (linhas "t=<ms> <tag> ..."), lido depois com `celerctl cat`.
//
// Plano (uma linha, tokens separados por espaço):
//   master [pres] [rtt] [size] [uni] [burst] [link]  — testes a rodar, em ordem
//   peer [periph]                                   — periph: vira periférico do link
// Protocolo (prefixo "l"; nunca "P"):
//   l?<k>            ping (unicast, 1 cópia)    -> l!<k>
//   lb<sz>.<i>.xxx   broadcast de teste (tamanho sz)
//   lu<c>.<i>.xxx    unicast de teste (c = cópias pedidas)
//   lq               fim de rodada: cada nó grava o resumo

var OUT = "/local/meshlab_out.txt";
var plan = (FS.readTextFile("/local/meshlab_plan.txt") || "peer").replace(/\s+$/, "").split(/\s+/);
var role = plan[0];
var lines = [];
var dirty = false;
var savedAt = 0;
function L(s) {
    lines.push("t=" + System.millis() + " " + s);
    if (lines.length > 600) lines.shift();
    dirty = true;
}
function flush(force) {
    if (!dirty || (!force && System.millis() - savedAt < 3000)) return;
    FS.writeTextFile(OUT, lines.join("\n") + "\n");
    dirty = false;
    savedAt = System.millis();
}

if (typeof CelerNet === "undefined") { L("sem CelerNet"); flush(true); System.exitApp(); }
System.keepAwake(true);
var music = System.musicStop();
// stop+start: o firmware relê o /local/celernet_tune.txt (A/B de bancada)
CelerNet.stop();
CelerNet.start({});
var tune = FS.readTextFile("/local/celernet_tune.txt") || "padrao";
// controle de coexistência: "nowifi" no plano derruba o STA (volta no reboot)
var hasNet = typeof Net !== "undefined";
if (plan.indexOf("nowifi") >= 0 && hasNet && typeof Net.wifiDisconnect === "function") Net.wifiDisconnect();
function wifiOn() { return hasNet && typeof Net.isConnected === "function" ? Net.isConnected() : "?"; }
var me = Pack.me();
L("boot role=" + role + " plan=" + plan.join(",") + " me=" + me.id + "/" + me.name + " musicStopped=" + music +
  " tune=" + tune.replace(/\s+$/, ""));

// ---- contadores do respondedor ----------------------------------------
var rxB = {};      // tamanho -> {i: true}
var rxU = {};      // cópias -> {i: n vezes}
var rxHops = {};
var pings = {};    // k -> enviado em
var rtts = [];
var lastStatusAt = 0;

function pad(s, n) { while (s.length < n) s += "x"; return s.substring(0, n); }
function st() {
    var s = CelerNet.status();
    return "q=" + s.txQueued + " drop=" + s.txDropped + " rxdrop=" + s.rxDropped + " relayed=" + s.relayed +
           " heard=" + s.heard;
}
function summary(tag) {
    var p = [];
    for (var sz in rxB) if (rxB.hasOwnProperty(sz)) {
        var n = 0; for (var k in rxB[sz]) if (rxB[sz].hasOwnProperty(k)) n++;
        p.push("b" + sz + "=" + n);
    }
    for (var c in rxU) if (rxU.hasOwnProperty(c)) {
        var u = 0, d = 0; for (var j in rxU[c]) if (rxU[c].hasOwnProperty(j)) { u++; d += rxU[c][j] - 1; }
        p.push("u" + c + "=" + u + "(dup" + d + ")");
    }
    p.push("pj=" + pj.ok + "/" + (pj.ok + pj.bad) + (pj.bad ? " cauda=" + pj.tail : ""));
    L(tag + " " + p.join(" ") + " hops=" + JSON.stringify(rxHops) + " " + st());
}

var linkEcho = false;
var pj = { ok: 0, bad: 0, tail: "" };   // envelopes Pack: JSON intacto?
var wasPlaying = false;
var lastQ = "";
var qN = 0;
// fim de rodada em 3 vias (1 quadro se perde fácil): o receptor deduplica
function endRound() {
    qN++;
    for (var i = 0; i < 3; i++) { CelerNet.broadcast("lq" + qN, 4); wait(700); }
    wait(2000);
}
function serve(now) {
    var m;
    while ((m = CelerNet.poll()) !== null) {
        var s = "" + m.msg;
        if (s.charAt(0) !== "l") continue;
        var op = s.charAt(1);
        rxHops[m.hops] = (rxHops[m.hops] || 0) + 1;
        if (op === "?") {
            CelerNet.send(m.from, "l!" + s.substring(2), { copies: 1, urgent: true });
        } else if (op === "!") {
            var k = s.substring(2);
            if (pings[k]) { rtts.push({ k: k, rtt: now - pings[k], hops: m.hops, rssi: m.rssi }); delete pings[k]; }
        } else if (op === "b") {
            var f = s.substring(2).split(".");
            (rxB[f[0]] = rxB[f[0]] || {})[f[1]] = true;
        } else if (op === "u") {
            var g = s.substring(2).split(".");
            var bag = rxU[g[0]] = rxU[g[0]] || {};
            bag[g[1]] = (bag[g[1]] || 0) + 1;
        } else if (op === "q") {
            var qn = s.substring(2);
            if (qn === lastQ) continue;          // 2a/3a via do mesmo fim de rodada
            lastQ = qn;
            summary("resumo de " + (m.fromName || m.from) + " rssi=" + m.rssi);
            rxB = {}; rxU = {}; rxHops = {};
        }
    }
    var e;
    while ((e = Pack.poll()) !== null) {
        try { JSON.parse(e.data); pj.ok++; }
        catch (err) { pj.bad++; pj.tail = JSON.stringify(e.data.substring(e.data.length - 6)); }
    }
    var playing = System.musicPlaying();
    if (playing !== wasPlaying) {
        wasPlaying = playing;
        L("musica " + (playing ? "TOCANDO pos=" + System.musicPos() : "parou"));
    }
    if (typeof CelerLink !== "undefined" && linkEcho) {
        var lm;
        while ((lm = CelerLink.poll()) !== null) CelerLink.send(lm);
    }
    if (now - lastStatusAt > 10000) {
        lastStatusAt = now;
        var info = System.getInfo();
        L("st " + st() + " wifi=" + wifiOn() + " free=" + info.freeRAM + " minFree=" + info.minFreeRAM + " nodes=" +
          CelerNet.nodes().map(function(n) { return n.name + "/" + n.hops + "/" + n.rssi + "/" + n.lastSeen; }).join(","));
    }
}

// espera ms servindo o protocolo e a tela (o mestre também responde)
var label = "";
function wait(ms) {
    var until = System.millis() + ms;
    do {
        UI.begin();
        if (UI.header("MeshLab " + role, { back: true })) { flush(true); System.exitApp(); }
        UI.text(label, 10, 60, { w: 220, lines: 4, id: "lb" });
        UI.text(lines.length ? lines[lines.length - 1] : "", 10, 140, { role: "caption", w: 220, lines: 8, id: "ll" });
        serve(System.millis());
        flush(false);
        UI.end(30);
    } while (System.millis() < until);
}

function peersNow() {
    return CelerNet.nodes();
}

// ---- testes do mestre ------------------------------------------------------
function tPres() {
    label = "presença 20 s";
    for (var i = 0; i < 4; i++) {
        L("pres " + JSON.stringify(Pack.members()));
        wait(5000);
    }
}
function tRtt() {
    var ps = peersNow();
    for (var p = 0; p < ps.length; p++) {
        label = "rtt -> " + ps[p].name;
        rtts = []; pings = {};
        var fails = 0;
        for (var i = 0; i < 20; i++) {
            var k = p + "" + (10 + i);
            if (CelerNet.send(ps[p].id, "l?" + k, { copies: 1, urgent: true })) pings[k] = System.millis();
            else fails++;
            wait(800);
        }
        wait(5000);
        var r = rtts.map(function(x) { return x.rtt; }).sort(function(a, b) { return a - b; });
        L("rtt " + ps[p].name + " hops=" + ps[p].hops + " ok=" + r.length + "/20 sendFail=" + fails +
          " min=" + r[0] + " med=" + r[r.length >> 1] + " max=" + r[r.length - 1] + " all=" + r.join(","));
    }
}
function tSize() {
    var sizes = [12, 16, 17, 40, 100, 200, 434];
    for (var s = 0; s < sizes.length; s++) {
        var sz = sizes[s];
        var fr = sz <= 16 ? 1 : Math.ceil(sz / 14);
        label = "broadcast " + sz + " B (" + fr + " quadros)";
        var refused = 0;
        for (var i = 0; i < 10; i++) {
            if (!CelerNet.broadcast(pad("lb" + sz + "." + i + ".", sz), 4)) refused++;
            wait(fr * 160 + 600);
        }
        L("size " + sz + " frames=" + fr + " sent=" + (10 - refused) + " refused=" + refused + " " + st());
    }
    wait(6000);
    endRound();
}
function tUni() {
    var ps = peersNow();
    for (var c = 1; c <= 2; c++) {
        for (var p = 0; p < ps.length; p++) {
            label = "unicast 100 B x" + c + " -> " + ps[p].name;
            var refused = 0;
            for (var i = 0; i < 10; i++) {
                if (!CelerNet.send(ps[p].id, pad("lu" + c + "." + i + ".", 100), { copies: c })) refused++;
                wait(c * 1800 + 600);
            }
            L("uni copies=" + c + " -> " + ps[p].name + " refused=" + refused + " " + st());
        }
    }
    wait(8000);
    endRound();
}
function tBurst() {
    label = "rajada: 30 broadcasts de 16 B sem pausa";
    var ok = 0, t0 = System.millis();
    for (var i = 0; i < 30; i++) if (CelerNet.broadcast(pad("lbR." + i + ".", 16), 4)) ok++;
    L("burst aceitos=" + ok + "/30 em " + (System.millis() - t0) + " ms " + st());
    var drainT0 = System.millis();
    while (CelerNet.status().txQueued > 0 && System.millis() - drainT0 < 30000) wait(200);
    L("burst fila vazia em " + (System.millis() - drainT0) + " ms " + st());
    label = "rajada de 434 B x2 cópias (62 quadros > fila de 40)";
    var ps = peersNow();
    if (ps.length) {
        var big = pad("lu9.0.", 434);
        var r = CelerNet.send(ps[0].id, big, { copies: 2 });
        L("big434x2 -> " + ps[0].name + " ret=" + r + " " + st());
        wait(15000);
        L("big434x2 depois " + st());
    }
    wait(5000);
    endRound();
}
function tPack() {
    var ps = peersNow();
    if (!ps.length) { L("pack: sem par"); return; }
    label = "Pack.send JSON -> " + ps[0].name;
    for (var i = 0; i < 6; i++) {
        // tamanhos que NAO sao multiplo de 14 (o bug da cauda de lixo)
        var o = { t: "pj", i: i, pad: pad("", 20 + i * 9) };
        L("pack send " + JSON.stringify(o).length + "B ret=" + Pack.send(ps[0].id, o));
        wait(4000);
    }
    label = "handoff de musica -> " + ps[0].name;
    var song = { bpm: 120, loops: 8, tracks: [
        { wave: "sq", vol: 60, notes: [[64, 2], [67, 2], [71, 2], [72, 2], [71, 2], [67, 3]] },
        { drum: true, vol: 70, notes: [[36, 4], [42, 2], [42, 2], [38, 4], [42, 1]] }] };
    L("hop play=" + System.playMusic(song));
    wait(3000);
    L("hop pos=" + System.musicPos() + " handoff=" + Pack.handoffMusic(ps[0].id) + " local=" + System.musicPlaying());
    wait(12000);
    endRound();
}
function tLink() {
    if (typeof CelerLink === "undefined") { L("link: sem CelerLink"); return; }
    label = "link: scan";
    var found = null;
    for (var a = 0; a < 3 && !found; a++) {
        var sc = CelerLink.scan(3000);
        L("link scan " + JSON.stringify(sc));
        for (var i = 0; i < sc.length; i++) if (sc[i].name.indexOf("MeshLab") === 0) found = sc[i];
        wait(500);
    }
    if (!found) { L("link: periferico nao achado"); return; }
    var t0 = System.millis();
    var ok = CelerLink.connect(found.id, 8000);
    L("link connect " + ok + " em " + (System.millis() - t0) + " ms " + JSON.stringify(CelerLink.status()));
    if (!ok) return;
    // RTT de eco com 20 B e 240 B; malha ligada em paralelo (coexistência)
    var szs = [20, 240];
    for (var s = 0; s < szs.length; s++) {
        var r = [], lost = 0;
        for (var j = 0; j < 20; j++) {
            var msg = pad("e" + j + ".", szs[s]);
            var ts = System.millis();
            if (!CelerLink.send(msg)) { lost++; continue; }
            var got = false;
            while (System.millis() - ts < 2000 && !got) {
                var x = CelerLink.poll();
                if (x === msg) { r.push(System.millis() - ts); got = true; } else System.delay(5);
            }
            if (!got) lost++;
            serve(System.millis());
        }
        r.sort(function(p, q) { return p - q; });
        L("link eco " + szs[s] + "B ok=" + r.length + "/20 lost=" + lost + " min=" + r[0] + " med=" + r[r.length >> 1] +
          " max=" + r[r.length - 1] + " " + JSON.stringify(CelerLink.status()));
    }
    // malha durante o link: um ping para cada nó
    label = "malha com link aberto";
    rtts = []; pings = {};
    var ps = peersNow();
    for (var p = 0; p < ps.length; p++) {
        for (var k = 0; k < 5; k++) {
            var key = "9" + p + k;
            if (CelerNet.send(ps[p].id, "l?" + key, { copies: 1, urgent: true })) pings[key] = System.millis();
            wait(800);
        }
    }
    wait(5000);
    L("malha com link: rtt " + JSON.stringify(rtts) + " " + st());
    CelerLink.disconnect();
    wait(3000);
    // o periferico volta a anunciar depois da queda? (achado do code-review)
    var back = false;
    for (var b = 0; b < 3 && !back; b++) {
        var sc2 = CelerLink.scan(3000);
        for (var q = 0; q < sc2.length; q++) if (sc2[q].id === found.id) back = true;
    }
    L("link: periferico anuncia de novo apos disconnect=" + back);
}

if (role === "master") {
    wait(8000);   // presença assentar
    for (var t = 1; t < plan.length; t++) {
        L("== " + plan[t]);
        if (plan[t] === "pres") tPres();
        else if (plan[t] === "rtt") tRtt();
        else if (plan[t] === "size") tSize();
        else if (plan[t] === "uni") tUni();
        else if (plan[t] === "burst") tBurst();
        else if (plan[t] === "link") tLink();
        else if (plan[t] === "pack") tPack();
        else if (plan[t] === "nowifi") L("wifi=" + wifiOn());
        flush(true);
    }
    L("== fim " + st());
    flush(true);
    label = "fim";
    while (true) wait(60000);
} else {
    if (plan[1] === "periph" && typeof CelerLink !== "undefined") {
        CelerLink.start("MeshLab-" + me.id, { pairing: false });
        linkEcho = true;
        L("periph anunciando MeshLab-" + me.id);
    }
    label = "respondendo (" + me.name + ")";
    while (true) {
        wait(60000);
        flush(true);
    }
}
