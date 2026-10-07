// PackBench — suite E2E da matilha em 3 placas (API 27). NÃO é app de
// fábrica: instrumento de bancada. Sem FS (o objeto exige permissão
// "fs"): o relatório sai por System.notify e é lido com
// `celerctl cat /local/notifications.txt` (grep "bench|").
//
// Papel pelos CAPS (zero configuração): mestre = quem tem MOTORS (dog)
// ou MIC (watch — fallback com o dog dormindo); o resto escuta
// (SmartDisplay). O mestre mede RTT de unicast (5 pings com ACK por
// envelope), broadcast com confirmações e o HANDOFF da música para cada
// peer com speaker (a posição de retomada do peer prova o seek).
//
// Contra o idle-home (watch/dog derrubam app parado após 30 s sem
// toque): o peer roda em atos de 24 s com estado em Storage — com
// /local/autostart.txt = "PackBench" o relançamento reabre este app e
// segue de onde parou (a música do handoff toca nativamente; o app
// relançado só a encontra tocando).

function N(s) { System.notify("bench", s); }

function boardName() {
    var b = System.getInfo().board;
    if (b.indexOf("zzpet") >= 0 || b.indexOf("spotpear") >= 0) return "Bench-dog";
    if (b.indexOf("waveshare") >= 0) return "Bench-watch";
    if (b.indexOf("smartdisplay") >= 0) return "Bench-board";
    return "Bench-x";
}

function header(title) {
    if (UI.header(title, { back: true })) System.exitApp();
}

if (typeof CelerNet === "undefined" || typeof Pack === "undefined") {
    N("sem BT nesta placa");
    System.exitApp();
}
var NAME = boardName();
// start idempotente: re-startar um no ATIVO reseta seqs/filas/dedup no
// meio da malha (suspeito da degradacao dog<->watch) — so sobe se caiu
if (!CelerNet.status().active) CelerNet.start({ name: NAME });
System.keepAwake(120000);  // 2 min de tela acesa (onde ha estados de tela)
var me = Pack.me();
var role = me.caps.motors ? "master" : "peer";

// Drena envelopes custom; devolve {from, o} ou null
function packJson() {
    var e = Pack.poll();
    if (e === null) return null;
    try { return { from: e.from, o: JSON.parse(e.data) }; } catch (err) { return null; }
}

if (role === "master" && Storage.get("suite8") === "ran") {
    role = "peer";  // relançado pelo idle-home: nao re-mede nada
}

if (role === "master") {
    N("mestre " + NAME + " caps " + JSON.stringify(me.caps));

    // fase 1: presença (20 s) — cada no novo com caps/rssi/hops
    var seen = {};
    var t1 = System.millis();
    while (System.millis() - t1 < 20000) {
        var f = UI.begin();
        header("PackBench mestre");
        var ns = CelerNet.nodes();
        for (var i = 0; i < ns.length; i++) {
            var k = ns[i].name || ns[i].id;
            if (!seen[k]) {
                seen[k] = true;
                N("vejo " + k + " caps=" + ns[i].caps + " rssi=" + ns[i].rssi +
                  " hops=" + ns[i].hops);
            }
        }
        UI.text("nos: " + ns.length, 10, 60);
        for (var j = 0; j < ns.length && j < 5; j++) {
            UI.text((ns[j].name || ns[j].id) + " " + ns[j].rssi + " dBm caps " +
                    ns[j].caps, 10, 80 + j * 14, { role: "caption" });
        }
        UI.end();
        System.delay(150);
    }

    var members = Pack.members();

    // fase 2: RTT unicast — 5 pings com ACK (o RTT inclui a taxa do flood:
    // ~1 mensagem/s cheia; esse e o custo honesto do protocolo)
    for (var m = 0; m < members.length; m++) {
        var peer = members[m];
        var rtts = [];
        var lost = 0;
        for (var p = 0; p < 5; p++) {
            var t0 = System.millis();
            Pack.send(peer.id, { p: p, d: "b" });
            var rtt = -1;
            while (System.millis() - t0 < 15000 && rtt < 0) {
                System.delay(80);
                var fg = UI.begin();
                header("PackBench ping");
                var pj2;
                while (rtt < 0 && (pj2 = packJson()) !== null) {
                    if (pj2.o.a === p) {
                        rtt = System.millis() - t0;
                    }
                }
                UI.text(peer.name + " #" + p + " " + (rtt >= 0 ? rtt + " ms" : "..."),
                        10, 60);
                UI.end();
            }
            if (rtt >= 0) rtts.push(rtt); else lost++;
        }
        if (rtts.length > 0) {
            rtts.sort(function (a, b) { return a - b; });
            N("rtt " + peer.name + ": " + rtts[0] + "/" +
              rtts[Math.floor(rtts.length / 2)] + "/" + rtts[rtts.length - 1] +
              " ms (perdas " + lost + "/5)");
        } else {
            N("rtt " + peer.name + ": SEM ACK (5/5)");
        }
    }

    // fase 3: broadcast com confirmações (peer responde {back} por unicast)
    var bAcks = 0;
    CelerNet.broadcast({ bcast: 1, from: NAME });
    var tB = System.millis();
    while (System.millis() - tB < 6000) {
        System.delay(100);
        var fb = UI.begin();
        header("PackBench bcast");
        var pj3;
        while ((pj3 = packJson()) !== null) {
            if (pj3.o.back !== undefined) bAcks++;
        }
        while (CelerNet.poll() !== null) {}  // drena ecos do broadcast
        UI.text("confirmacoes: " + bAcks, 10, 60);
        UI.end();
    }
    N("broadcast: " + bAcks + " confirmacoes");

    // fase 4: handoff da música para cada peer com speaker — a festa tem
    // que continuar no peer DO MESMO PONTO (o peer notifica "festa pos X";
    // X ~= posEnvio prova o seek do MusicEngine)
    var song = { bpm: 132, loops: 16, tracks: [
        { wave: "sq",  vol: 80, notes: [[64,2],[67,2],[71,2],[72,2],[71,2],[67,2],[64,4],[0,2],[64,2],[69,2],[71,4],[67,4]] },
        { wave: "tri", vol: 70, notes: [[40,4],[47,4],[40,4],[47,4],[45,4],[40,4],[43,4],[47,4]] },
        { drum: true, vol: 90, notes: [[36,4],[42,2],[42,2],[38,4],[42,2],[42,2]] }
    ]};
    for (var h = 0; h < members.length; h++) {
        var tgt = members[h];
        if (!tgt.caps.speaker) continue;
        System.delay(600);  // beep da notify anterior segura o speaker ~300 ms
        if (!System.playMusic(song)) {
            N("handoff: alto-falante do mestre ocupado");
            break;
        }
        var tS = System.millis();
        while (System.millis() - tS < 6000) {
            System.delay(200);
            var fm = UI.begin();
            header("PackBench festa");
            UI.text("tocando... " + System.musicPos() + " ms", 10, 60);
            UI.end();
        }
        var posEnv = System.musicPos();
        var ok = Pack.handoffMusic(tgt.id);
        N("handoff->" + tgt.name + " @" + posEnv + "ms ok=" + ok);
        var tW = System.millis();
        while (System.millis() - tW < 5000) {
            System.delay(150);
            var fw = UI.begin();
            header("PackBench");
            UI.text("local: " + (System.musicPlaying() ? "tocando" : "parado"), 10, 60);
            UI.end();
        }
        N("apos handoff local playing=" + System.musicPlaying() + " (esperado false)");
    }

    var st = CelerNet.status();
    N("fim: txDrop=" + st.txDropped + " rxDrop=" + st.rxDropped +
      " relayed=" + st.relayed + " ouvidos=" + st.heard);
    Storage.set("suite8", "ran");
    N("suite concluida - proximos lancamentos entram em modo peer");
    System.exitApp();
}

// ---------------- peer: responde ACKs e reporta festas (atos de 24 s) ---
// No quadro (sem homeApp = sem idle-home) o peer fica vivo a suíte toda:
// 6 atos ~2,4 min; no watch/dog cada ato sai e o autostart relança.
var ATOS = NAME === "Bench-board" ? 6 : 1;
var counts = { ack: 0, festa: 0, bcast: 0 };
var saved = Storage.get("counts");
if (saved) { try { counts = JSON.parse(saved); } catch (e5) {} }
for (var ato = 0; ato < ATOS; ato++) {
var wasPlaying = System.musicPlaying();
var festaPosReportada = false;
var rx1 = true;
var t0p = System.millis();
while (System.millis() - t0p < 24000) {
    var fp = UI.begin();
    header("PackBench peer");
    var pj;
    while ((pj = packJson()) !== null) {
        var o2 = pj.o;
        if (rx1) { rx1 = false; N("rx: " + ("" + o2).substring(0, 60)); }
        if (o2.p !== undefined) {
            Pack.send(pj.from, { a: o2.p });
            counts.ack++;
        }
    }
    var mb;
    var cru1 = true;
    while ((mb = CelerNet.poll()) !== null) {
        if (cru1) {
            cru1 = false;
            N("cru de " + (mb.fromName || mb.from) + ": " + ("" + mb.msg).substring(0, 40));
        }
        try {
            var ob2 = JSON.parse(mb.msg);
            if (ob2 && ob2.bcast !== undefined && ob2.from) {
                Pack.send(ob2.from, { back: ob2.bcast });
                counts.bcast++;
            }
        } catch (eb) {}
    }
    var tocando = System.musicPlaying();
    if (tocando && !wasPlaying) {
        counts.festa++;
        festaPosReportada = false;
    }
    if (tocando && !festaPosReportada) {
        festaPosReportada = true;
        N("festa chegou! pos inicial " + System.musicPos() + " ms");
    }
    if (!tocando && wasPlaying) {
        N("festa terminou @" + System.musicPos() + " ms");
    }
    wasPlaying = tocando;
    var nsp = CelerNet.nodes();
    UI.text("acks " + counts.ack + " festas " + counts.festa +
            " bcasts " + counts.bcast, 10, 60);
    UI.text("nos: " + nsp.length, 10, 78, { role: "caption" });
    for (var q = 0; q < nsp.length && q < 4; q++) {
        UI.text((nsp[q].name || nsp[q].id) + " " + nsp[q].rssi + " dBm",
                10, 94 + q * 13, { role: "caption" });
    }
    UI.end();
    System.delay(120);
}
Storage.set("counts", JSON.stringify(counts));
N("peer " + NAME + ": acks=" + counts.ack + " festas=" + counts.festa +
  " bcasts=" + counts.bcast);
}
// autostart relança (watch/dog); no quadro o launcher segue com a malha viva
System.exitApp();
