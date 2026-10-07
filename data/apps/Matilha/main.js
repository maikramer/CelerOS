// Matilha — painel do bando de CelerOS (API 27). A malha BLE (CelerNet)
// carrega a presença com o PAPEL de cada um (som, mic, tela, patas, leds,
// rede) e os envelopes do OS: mensagem direta por membro e a FESTA que
// viaja — a música chiptune em curso continua no vizinho com alto-falante,
// do mesmo ponto. Em placa sem Bluetooth mostra um aviso e sai.

var hasMesh = typeof CelerNet !== "undefined";
var rx = [];               // log do que chega (mais novo primeiro)
var members = [];
var membersAt = -10000;
var me = null;

function pushRx(quem, texto) {
    rx.unshift(quem + ": " + texto);
    if (rx.length > 3) rx.pop();
}

function drainRx() {
    var got = false, m;
    while ((m = CelerNet.poll()) !== null) {
        // broadcast livre (sem envelope): texto do app
        var t = "" + m.msg;
        try { var o = JSON.parse(t); if (o && o.texto) t = o.texto; } catch (e) {}
        pushRx(m.fromName || m.from, t);
        got = true;
    }
    while ((m = Pack.poll()) !== null) {
        var d = "" + m.data;
        try { var p = JSON.parse(d); if (p && p.texto) d = p.texto; } catch (e) {}
        pushRx((m.fromName || m.from) + " (direta)", d);
        got = true;
    }
    if (got) UI.invalidate();
}

function roleStr(c) {
    var r = [];
    if (c.speaker) r.push("som");
    if (c.mic) r.push("mic");
    if (c.display) r.push("tela");
    if (c.motors) r.push("patas");
    if (c.leds) r.push("leds");
    if (c.hub) r.push("rede");
    return r.length ? r.join(" · ") : "?";
}

function sendDirect(name) {
    var txt = System.prompt("Mensagem para " + name, "");
    if (txt === "") return;
    if (!Pack.send(name, { de: Pack.me().name, texto: txt })) {
        UI.toast("Não deu: malha desligada ou destino sumiu");
    }
}

function sendAll(myName) {
    var txt = System.prompt("Mensagem para a matilha", "");
    if (txt === "") return;
    if (!CelerNet.broadcast({ type: "msg", de: myName, texto: txt })) {
        UI.toast("Malha desligada ou fila cheia");
    }
}

while (true) {
    UI.begin();

    if (UI.header("Matilha", { back: true })) {
        System.exitApp();
    }

    if (!hasMesh) {
        UI.card(10, 56, 220, 90);
        UI.text("Esta placa não tem Bluetooth no firmware.", 22, 70,
                { role: "body", w: 196, lines: 3 });
        UI.cardEnd();
        UI.text("A matilha existe nas placas com BLE (SmartDisplay, cachorro e relógio).", 10, 160,
                { role: "caption", w: 220, lines: 3 });
        UI.end();
        continue;
    }

    var st = CelerNet.status();
    me = Pack.me();
    var y = 56;

    // liga/desliga + quem sou eu
    UI.card(10, y, 220, 64);
    var on = UI.toggle(20, y + 8, st.active);
    UI.text("Malha " + (on ? "ativa" : "desligada"), 74, y + 8);
    if (on !== st.active) {
        var ok = on ? CelerNet.start({}) : CelerNet.stop();
        if (!ok && on) UI.toast("Sem RAM para o rádio agora");
        UI.invalidate();
    }
    st = CelerNet.status();
    UI.text(me.name + " · " + roleStr(me.caps), 20, y + 32, { role: "caption", w: 200, lines: 1 });
    UI.text("rede " + st.net + " · " + st.heard + " ouvidos", 20, y + 46,
            { role: "caption", w: 200, lines: 1 });
    UI.cardEnd();
    y += 72;

    // bando (atualiza 1x/s): tap = mensagem direta pra aquele membro
    if (System.millis() - membersAt > 1000) {
        membersAt = System.millis();
        members = Pack.members();
        UI.invalidate();
    }
    var items = [];
    for (var i = 0; i < members.length; i++) {
        var mb = members[i];
        items.push({ label: mb.name || mb.id,
                     sub: roleStr(mb.caps) + " · há " + mb.lastSeen + " s",
                     right: mb.rssi + " dBm" });
    }
    if (items.length === 0) items.push({ label: "Ninguém ouvido ainda", enabled: false });
    var hit = UI.list("members", 10, y, 220, 96, items, { rowH: 32 });
    if (hit >= 0 && members[hit]) sendDirect(members[hit].name || members[hit].id);
    y += 104;

    // a festa viaja: passa a música em curso pro vizinho com speaker
    var tocando = System.musicPlaying();
    if (UI.button(tocando ? "Passar a música" : "Passar a música (nada tocando)",
                  10, y, 220, 30,
                  { style: tocando && st.active ? "primary" : "ghost",
                    disabled: !tocando || !st.active })) {
        if (Pack.handoffMusic()) UI.toast("A festa seguiu viagem");
        else UI.toast("Ninguém com alto-falante por perto");
    }
    y += 38;

    if (UI.button("Mensagem para todos", 10, y, 220, 30,
                  { style: st.active ? "solid" : "ghost", disabled: !st.active })) {
        sendAll(me.name);
    }
    y += 38;

    // log do que chega
    UI.text("Chegou:", 10, y, { role: "caption" });
    for (var k = 0; k < rx.length; k++) {
        UI.text(rx[k], 10, y + 16 + k * 16, { role: "caption", w: 220, lines: 1 });
    }

    drainRx();
    UI.end();
}
