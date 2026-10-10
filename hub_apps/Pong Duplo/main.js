// Pong Duplo — um Pong que atravessa DUAS telas pelo Celer Link (API 22+;
// pareamento por código). Coloque dois CelerOS lado a lado: a bola sai pelo
// topo de uma tela e entra pelo topo da outra, espelhada. Cada um defende a
// sua base; 5 pontos vencem. Um aparelho hospeda (mostra o código de 6
// dígitos), o outro entra e digita o código.
//
// Mensagens do link (JSON pequeno; o link é par-a-par, até 240 B):
//   {t:"b", x, vx, vy}   a bola cruzou para o seu lado (x em milésimos)
//   {t:"p"}              errei: ponto seu
//   {t:"s"}              saque: vou sacar daqui a pouco (só informativo)
// O "Piloto automático" defende sozinho (bom para demonstração e treino).

// Som: System.sfx (API 32) mistura o efeito por cima da trilha e NAO trava
// o loop do jogo; firmware antigo cai no playTone (bloqueante)
var somFn = typeof System.sfx === "function" ? System.sfx :
            (typeof System.playTone === "function" ? System.playTone : null);

var hasLink = typeof CelerLink !== "undefined";
var T = System.theme();
var TOP = 44, BOT = 316, PAD_Y = 292, PAD_W = 56, PAD_H = 8, R = 5, WIN = 5;
var MENU = 0, HOST = 1, SCAN = 2, PLAY = 3, END = 4;
var scr = MENU;
var auto = Storage.get("auto", "0") === "1";
var peers = [];
var padX = 120;
var ball = null;           // {x, y, vx, vy} quando a bola está do nosso lado
var serveAt = 0;
var mine = 0, theirs = 0;
var lastT = 0;
var note = "";
var aimErr = 0;            // piloto: desvio da mira sorteado a cada bola (erra como gente)

function resetMatch() {
    mine = 0; theirs = 0; ball = null; padX = 120; serveAt = 0;
}
function serve(now) {
    var dir = Math.random() < 0.5 ? -1 : 1;
    ball = { x: 120, y: 200, vx: dir * (40 + Math.random() * 60), vy: -170 };
    aimErr = 0;
    serveAt = 0;
    CelerLink.send({ t: "s" });
}
function leave() {
    CelerLink.disconnect();
    CelerLink.stop();
    scr = MENU;
    resetMatch();
    UI.invalidate();
}

function onLink(raw, now) {
    var o;
    try { o = JSON.parse(raw); } catch (e) { return; }
    if (!o || !o.t) return;
    if (o.t === "b" && scr === PLAY) {
        // entra pelo topo, espelhada: o "x" de lá é o (240 - x) daqui
        ball = { x: 240 - o.x * 240 / 1000, y: TOP + R, vx: -o.vx, vy: Math.abs(o.vy) };
        aimErr = (Math.random() - 0.5) * 100;
    } else if (o.t === "p" && scr === PLAY) {
        mine++;
        if (mine >= WIN) { scr = END; somFn && somFn([[660, 90], [880, 90], [1320, 220]]); }
    }
    UI.invalidate();
}

function physics(now) {
    var dt = Math.min(60, now - lastT) / 1000;
    lastT = now;
    if (auto) {
        var target = ball ? ball.x + aimErr : 120;
        var step = 150 * dt;   // piloto com limite de velocidade: erra bolas rápidas
        padX += Math.max(-step, Math.min(step, target - padX));
    }
    padX = Math.max(PAD_W / 2, Math.min(240 - PAD_W / 2, padX));
    if (serveAt && now >= serveAt) serve(now);
    if (!ball) return;
    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;
    if (ball.x < R) { ball.x = R; ball.vx = Math.abs(ball.vx); }
    if (ball.x > 240 - R) { ball.x = 240 - R; ball.vx = -Math.abs(ball.vx); }
    if (ball.vy > 0 && ball.y + R >= PAD_Y && ball.y + R <= PAD_Y + PAD_H + 6 &&
        Math.abs(ball.x - padX) <= PAD_W / 2 + R) {
        ball.vy = -Math.min(420, Math.abs(ball.vy) * 1.07);
        ball.vx += (ball.x - padX) * 3;
        System.beep(880, 15);
    }
    if (ball.y < TOP) {
        CelerLink.send({ t: "b", x: Math.round(ball.x * 1000 / 240), vx: Math.round(ball.vx),
                         vy: Math.round(ball.vy) });
        ball = null;
    } else if (ball.y > BOT) {
        ball = null;
        theirs++;
        CelerLink.send({ t: "p" });
        System.beep(200, 120);
        if (theirs >= WIN) scr = END;
        else serveAt = now + 1200;   // quem perdeu o ponto saca
    }
}

function drawField() {
    System.fillRect(0, TOP, 240, BOT - TOP, T.bg);
    for (var x = 4; x < 240; x += 16) System.fillRect(x, TOP, 8, 2, T.stroke);
    // placar no fundo do campo: primitiva crua, repintada com o campo a cada
    // frame (um UI.text só redesenha quando muda — o fillRect o apagaria)
    var sc = mine + " × " + theirs;
    System.setTextColor(T.stroke);
    System.drawString(sc, 120 - (System.textWidth(sc, 4) >> 1), 150, 4);
    System.fillSmoothRoundRect(Math.round(padX - PAD_W / 2), PAD_Y, PAD_W, PAD_H, 4, T.accent);
    if (ball) System.fillSmoothCircle(Math.round(ball.x), Math.round(ball.y), R, T.text);
}

while (true) {
    var full = UI.begin();
    var now = System.millis();
    if (UI.header("Pong Duplo", { back: true, sub: scr === PLAY ? mine + " × " + theirs : "" })) {
        if (scr === MENU || !hasLink) System.exitApp();
        leave();
    }
    if (!hasLink) {
        UI.card(10, 56, 220, 80);
        UI.text("Esta placa não tem Bluetooth no firmware.", 22, 70, { w: 196, lines: 3 });
        UI.cardEnd();
        UI.end();
        continue;
    }

    if (scr === MENU) {
        UI.text("Dois aparelhos lado a lado: a bola passa de uma tela para a outra.", 10, 54,
                { role: "caption", color: T.textDim, w: 220, lines: 3 });
        if (UI.button("Hospedar partida", 10, 110, 220, 44)) {
            CelerLink.start(null, { pairing: true });
            scr = HOST;
            resetMatch();
        }
        if (UI.button("Entrar numa partida", 10, 162, 220, 44, { style: "ghost" })) {
            scr = SCAN;
            peers = [];
            UI.invalidate();
        }
        UI.text("Piloto automático", 10, 234);
        var a = UI.toggle(176, 230, auto);
        if (a !== auto) { auto = a; Storage.set("auto", auto ? "1" : "0"); }
        if (note) UI.text(note, 10, 280, { role: "caption", color: T.warn, w: 220, id: "note" });
    } else if (scr === HOST) {
        var st = CelerLink.status();
        UI.text("Esperando o adversário...", 120, 70, { role: "title", align: "center", id: "h1" });
        UI.text("Visível como " + st.name, 120, 100, { role: "caption", align: "center", id: "h2" });
        if (st.pairing && st.code) {
            UI.card(30, 130, 180, 80);
            UI.text("Código", 120, 140, { role: "caption", align: "center", id: "h3" });
            UI.text(st.code, 120, 166, { role: "display", align: "center", id: "h4" });
            UI.cardEnd();
        }
        if (st.connected) {
            scr = PLAY;
            lastT = now;
            serveAt = now + 1500;
            UI.invalidate();
        }
        if (UI.button("Cancelar", 10, 266, 220, 40, { style: "ghost" })) leave();
    } else if (scr === SCAN) {
        if (!peers.length) {
            UI.text("Procurando...", 120, 70, { align: "center", id: "s1" });
            UI.end();
            peers = CelerLink.scan(2500);
            if (!peers.length) peers = [{ id: "", name: "Ninguém hospedando por perto", rssi: 0 }];
            UI.invalidate();
            continue;
        }
        var items = [];
        for (var i = 0; i < peers.length; i++) {
            items.push(peers[i].id ? { label: peers[i].name, sub: peers[i].rssi + " dBm",
                                       bars: peers[i].rssi > -60 ? 4 : peers[i].rssi > -72 ? 3 : 2 }
                                   : { label: peers[i].name, enabled: false });
        }
        var hit = UI.list("peers", 10, 50, 220, 210, items, { rowH: 48 });
        if (hit >= 0 && peers[hit].id) {
            if (!CelerLink.connect(peers[hit].id)) {
                UI.toast("Não conectou");
            } else {
                // código de 6 dígitos da tela do outro (até 3 tentativas — o
                // anfitrião derruba na 3a errada)
                var tries = 0;
                while (CelerLink.status().pairing && tries < 3) {
                    var c = System.prompt("Código na tela do outro", "", { hint: "num" });
                    if (!c) break;
                    tries++;
                    if (!CelerLink.verify(c)) UI.toast("Código errado");
                }
                if (CelerLink.status().connected) {
                    scr = PLAY;
                    lastT = System.millis();
                    resetMatch();
                } else {
                    CelerLink.disconnect();
                    note = "Pareamento recusado";
                    scr = MENU;
                }
            }
            UI.invalidate();
        }
        if (UI.button("Procurar de novo", 10, 268, 220, 40, { style: "ghost" })) { peers = []; UI.invalidate(); }
    } else if (scr === PLAY || scr === END) {
        var msg;
        while ((msg = CelerLink.poll()) !== null) onLink(msg, now);
        if (scr === PLAY && !CelerLink.status().connected) {
            note = "Conexão perdida";
            leave();
            UI.end();
            continue;
        }
        if (scr === PLAY) {
            var tc = UI.touch();
            if (tc.down && tc.y > TOP) padX = tc.x;
            physics(now);
            drawField();
        } else {
            UI.text(mine >= WIN ? "Você venceu!" : "Você perdeu", 120, 110,
                    { role: "title", align: "center", color: mine >= WIN ? T.ok : T.err, id: "e1" });
            UI.text(mine + " × " + theirs, 120, 150, { role: "display", align: "center", id: "e2" });
            if (UI.button("Sair", 10, 266, 220, 40)) leave();
        }
    }
    UI.end(scr === PLAY ? 40 : 30);
}
