// CelerNet — painel da malha BLE entre CelerOS (API 26). Liga/desliga o
// nó (persiste entre reboots), mostra os dispositivos ouvidos com sinal,
// saltos e idade, manda mensagem de teste para toda a área e acompanha
// as que chegam. Em placa sem Bluetooth mostra um aviso e sai.

var hasMesh = typeof CelerNet !== "undefined";
var rx = [];            // ultimas mensagens recebidas (mais nova primeiro)
var nodes = [];
var nodesAt = -10000;

function pushRx(m) {
    var quem = m.fromName || m.from;
    rx.unshift(quem + " (" + m.hops + " salto" + (m.hops === 1 ? "" : "s") + "): " + m.msg);
    if (rx.length > 3) rx.pop();
}

function drainRx() {
    var m, got = false;
    while ((m = CelerNet.poll()) !== null) {
        pushRx(m);
        got = true;
    }
    if (got) UI.invalidate();
}

function sendTest() {
    var txt = System.prompt("Mensagem para a malha", "");
    if (txt === "") return;  // X = cancelar
    if (!CelerNet.broadcast({ type: "msg", de: CelerNet.status().name, texto: txt })) {
        UI.toast("Malha desligada ou fila cheia");
    }
}

while (true) {
    var full = UI.begin();

    if (UI.header("CelerNet", { back: true })) {
        System.exitApp();
    }

    if (!hasMesh) {
        UI.card(10, 56, 220, 90);
        UI.text("Esta placa não tem Bluetooth no firmware.", 22, 70,
                { role: "body", w: 196, lines: 3 });
        UI.cardEnd();
        UI.text("A malha existe nas placas com BLE (SmartDisplay, cachorro e relógio).", 10, 160,
                { role: "caption", w: 220, lines: 3 });
        UI.end();
        continue;
    }

    var st = CelerNet.status();
    var y = 56;

    // liga/desliga + identidade
    UI.card(10, y, 220, 52);
    var on = UI.toggle(20, y + 8, st.active);
    UI.text("Malha " + (on ? "ativa" : "desligada"), 74, y + 8);
    if (on !== st.active) {
        var ok = on ? CelerNet.start({}) : CelerNet.stop();
        if (!ok && on) UI.toast("Sem RAM para o rádio agora");
        UI.invalidate();
    }
    st = CelerNet.status();
    UI.text(st.name + " · rede " + st.net + " · " + st.heard + " ouvidos",
            20, y + 32, { role: "caption", w: 200 });
    UI.cardEnd();
    y += 60;

    // presenca (atualiza 1x/s)
    if (System.millis() - nodesAt > 1000) {
        nodesAt = System.millis();
        nodes = CelerNet.nodes();
        UI.invalidate();
    }
    var items = [];
    for (var i = 0; i < nodes.length; i++) {
        var n = nodes[i];
        items.push({ label: n.name || n.id,
                     sub: n.hops + (n.hops === 1 ? " salto" : " saltos") + " · há " + n.lastSeen + " s",
                     right: n.rssi + " dBm" });
    }
    if (items.length === 0) items.push({ label: "Nenhum nó ouvido ainda", enabled: false });
    UI.list("nodes", 10, y, 220, 104, items, { rowH: 34 });
    y += 112;

    // mensagem de teste
    if (UI.button("Enviar mensagem", 10, y, 220, 30,
                  { style: st.active ? "primary" : "ghost", disabled: !st.active })) {
        sendTest();
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
