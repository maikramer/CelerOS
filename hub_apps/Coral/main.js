// Coral — orquestra de aparelhos pela malha CelerNet (API 27). Abra o Coral
// em vários CelerOS com alto-falante; quem toca "Reger" vira maestro: as
// vozes da música (melodia, harmonia, baixo, bateria) são divididas entre os
// músicos e todo mundo entra JUNTO — o "já!" sai com uma contagem e cada
// músico desconta o tempo que a mensagem levou pelos saltos da malha. Cada
// aparelho toca só a sua parte: a música inteira só existe na sala.
//
// Protocolo (prefixo "c"; um quadro cada):
//   ch<1|0>                      músico presente (com/sem alto-falante), 3 s
//   ca<sessão>.<música>.<vozes>  sua parte (unicast, 2 cópias; vozes = bits)
//   cg<sessão>.<ms>              já! a música começa daqui a <ms> (2 vias)
//   cx<sessão>                   parar

var mesh = require("celeros.mesh");
var T = System.theme();
var LEAD = 3500;                 // contagem até a entrada
// latência estimada por salto de 1 quadro (fila + jitter do relay); calibre
// com o RTT/2 do app Sonar se a sua casa tiver muitos saltos
var HOP0 = 150, HOPN = 280;
var VOICES = ["melodia", "harmonia", "baixo", "bateria"];

var SONGS = [
    { name: "Ciranda", bpm: 112, loops: 2, tracks: [
        { wave: "sq", vol: 80, notes: [[60,2],[62,2],[64,2],[65,2],[67,4],[67,4],[69,2],[69,2],[69,2],[69,2],[67,8],
                                       [65,2],[65,2],[65,2],[65,2],[64,4],[64,4],[62,2],[62,2],[62,2],[62,2],[60,8]] },
        { wave: "sq25", vol: 55, notes: [[64,8],[62,8],[65,8],[64,8],[62,8],[60,8],[59,8],[60,8]] },
        { wave: "tri", vol: 90, notes: [[48,8],[43,8],[45,8],[43,8],[41,8],[48,8],[43,8],[48,8]] },
        { drum: true, vol: 85, notes: [[36,4],[42,4],[38,4],[42,4],[36,4],[42,4],[38,4],[42,4],
                                       [36,4],[42,4],[38,4],[42,4],[36,4],[42,4],[38,4],[42,4]] }
    ] },
    { name: "Pulo do Gato", bpm: 140, loops: 3, tracks: [
        { wave: "sq", vol: 80, notes: [[69,2],[72,2],[76,4],[74,2],[72,2],[71,4],[69,2],[71,2],[72,2],[74,2],[76,8],
                                       [77,2],[76,2],[74,4],[72,2],[71,2],[69,4],[68,2],[69,2],[71,2],[68,2],[69,8]] },
        { wave: "sq25", vol: 55, notes: [[60,8],[59,8],[57,8],[64,8],[60,8],[59,8],[56,8],[57,8]] },
        { wave: "tri", vol: 90, notes: [[45,4],[45,4],[52,4],[45,4],[50,4],[50,4],[52,4],[52,4],
                                        [45,4],[45,4],[52,4],[45,4],[52,4],[52,4],[45,8]] },
        { drum: true, vol: 85, notes: [[36,2],[42,2],[38,2],[42,2],[36,2],[42,2],[38,2],[42,2],
                                       [36,2],[42,2],[38,2],[42,2],[36,2],[42,2],[38,2],[42,2],
                                       [36,2],[42,2],[38,2],[42,2],[36,2],[42,2],[38,2],[42,2],
                                       [36,2],[42,2],[38,2],[42,2],[36,2],[42,2],[38,2],[42,2]] }
    ] }
];

var me = null;
var songSel = 0;
var musos = {};                  // id -> {name, speaker, at}
var helloAt = -10000;
var sess = 0;                    // sessão corrente (a que estou tocando/esperando)
var part = null;                 // {song, bits} minha parte na sessão
var startAt = 0;                 // relógio local da entrada (0 = nada agendado)
var playing = false;
var maestro = "";                // nome de quem rege a sessão
var lineup = [];                 // (maestro) [{id, name, bits}]
var goResend = 0;
var goResend2 = false;

function bitsText(bits) {
    var v = [];
    for (var i = 0; i < VOICES.length; i++) if (bits & (1 << i)) v.push(VOICES[i]);
    return v.length ? v.join(" + ") : "ouvinte";
}
function present(now) {
    var out = [];
    for (var id in musos) {
        if (musos.hasOwnProperty(id) && musos[id].speaker && now - musos[id].at < 10000) out.push(id);
    }
    return out;
}

function conduct(now) {
    sess = 1 + Math.floor(Math.random() * 98);
    var ids = present(now);
    if (me.caps.speaker) ids.push(me.id);
    ids.sort();
    if (!ids.length) { UI.toast("Ninguém com alto-falante"); return; }
    var bits = {};
    for (var v = 0; v < VOICES.length; v++) {
        var who = ids[v % ids.length];
        bits[who] = (bits[who] || 0) | (1 << v);
    }
    lineup = [];
    for (var i = 0; i < ids.length; i++) {
        var id = ids[i];
        lineup.push({ id: id, name: id === me.id ? "Você" : musos[id].name, bits: bits[id] });
        if (id !== me.id) CelerNet.send(id, "ca" + sess + "." + songSel + "." + bits[id], { urgent: true });
    }
    // a contagem só sai depois das partes (as 2 cópias levam ~1,5 s)
    maestro = "Você";
    part = { song: songSel, bits: bits[me.id] || 0 };
    goResend = now + 1800;
    startAt = now + 1800 + LEAD;
    UI.invalidate();
}

function go(now) {
    var left = startAt - now;
    if (left > 200) CelerNet.broadcast("cg" + sess + "." + left, 8);
}

function stopAll() {
    CelerNet.broadcast("cx" + sess, 8);
    stopLocal();
}
function stopLocal() {
    if (playing) System.musicStop();
    playing = false;
    startAt = 0;
    part = null;
    lineup = [];
    UI.invalidate();
}

function begin() {
    startAt = 0;
    if (!part || !part.bits) return;     // maestro sem alto-falante só rege
    var s = SONGS[part.song];
    var tr = [];
    for (var i = 0; i < s.tracks.length; i++) if (part.bits & (1 << i)) tr.push(s.tracks[i]);
    playing = System.playMusic({ bpm: s.bpm, loops: s.loops, tracks: tr });
    if (!playing) UI.toast("Alto-falante ocupado");
    UI.invalidate();
}

function onMsg(m, now) {
    var s = "" + m.msg;
    if (s.length < 2 || s.charAt(0) !== "c") return;
    var op = s.charAt(1), a = s.substring(2).split(".");
    if (op === "h") {
        if (!musos[m.from]) UI.invalidate();
        musos[m.from] = { name: m.fromName || m.from, speaker: a[0] === "1", at: now };
    } else if (op === "a") {
        var ns = parseInt(a[0], 10);
        if (ns === sess && part) return;                     // cópia
        if (playing) System.musicStop();
        playing = false;
        sess = ns;
        part = { song: parseInt(a[1], 10) % SONGS.length, bits: parseInt(a[2], 10) };
        maestro = m.fromName || m.from;
        startAt = 0;
        UI.invalidate();
    } else if (op === "g") {
        if (parseInt(a[0], 10) !== sess || startAt || playing) return;   // 2a via ou sessão alheia
        var hops = Math.max(1, m.hops);
        startAt = now + parseInt(a[1], 10) - (HOP0 + (hops - 1) * HOPN);
        UI.invalidate();
    } else if (op === "x") {
        if (parseInt(a[0], 10) === sess) stopLocal();
    }
}

while (true) {
    var full = UI.begin();
    if (UI.header("Coral", { back: true })) { if (playing) System.musicStop(); System.exitApp(); }
    if (!mesh.gate("Ligue a malha (app Matilha) para tocar junto com os aparelhos ao redor.")) {
        UI.end();
        continue;
    }
    me = mesh.me();
    var now = System.millis();
    mesh.each(onMsg, now);
    if (now - helloAt > 3000) {
        helloAt = now;
        CelerNet.broadcast("ch" + (me.caps.speaker ? "1" : "0"), 8);
    }
    // "já!" em duas vias espaçadas: a segunda cobre a primeira perdida
    if (goResend && now >= goResend) { goResend = goResend2 ? 0 : now + 700; go(now); goResend2 = !goResend2; }
    if (startAt && now >= startAt) begin();
    if (playing && !System.musicPlaying()) { playing = false; part = null; UI.invalidate(); }

    var busy = playing || startAt > 0;
    var ns = UI.tabs(10, 48, 220, 30, [SONGS[0].name, SONGS[1].name], songSel);
    if (ns !== songSel && !busy) { songSel = ns; UI.invalidate(); }

    var items = [];
    if (lineup.length) {
        for (var i = 0; i < lineup.length; i++) items.push({ label: lineup[i].name, sub: bitsText(lineup[i].bits) });
    } else {
        var ids = present(now);
        if (me.caps.speaker) items.push({ label: "Você", sub: "músico" });
        for (var k = 0; k < ids.length; k++) items.push({ label: musos[ids[k]].name, sub: "músico" });
    }
    if (!items.length) items.push({ label: "Esperando músicos...", sub: "abra o Coral em outros aparelhos", enabled: false });
    UI.list("musos", 10, 86, 220, 132, items, { rowH: 44 });

    UI.card(10, 224, 220, 46);
    var st;
    if (startAt) st = "Entrada em " + Math.max(0, Math.ceil((startAt - now) / 1000)) + "... (" + maestro + " rege)";
    else if (playing) st = "Tocando: " + bitsText(part ? part.bits : 0);
    else st = part && !part.bits ? maestro + " rege (você só ouve)" : "Toque Reger para começar";
    UI.text(st, 20, 230, { w: 200, id: "st" });
    if (playing) {
        var s = SONGS[part.song];
        var tot = 0;
        for (var n = 0; n < s.tracks[0].notes.length; n++) tot += s.tracks[0].notes[n][1];
        var loopMs = tot * 15000 / s.bpm;
        UI.progress(20, 254, 200, 8, Math.min(100, Math.round(100 * System.musicPos() / (loopMs * s.loops))));
    }
    UI.cardEnd();
    if (UI.button(busy ? "Parar" : "Reger " + SONGS[songSel].name, 10, 278, 220, 36,
                  { style: busy ? "danger" : "primary" })) {
        if (busy) stopAll();
        else conduct(now);
    }
    UI.end();
}
