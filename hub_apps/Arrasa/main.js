// Arrasa! — duelo de catapultas com fisica VERLET NATIVA (API 31): o
// estilingue e uma corda de verdade que o dedo tensiona (2 vinculos aos
// garfos fixos) e solta — a tracao lanca a pedra e o corte acontece no
// plano da forquilha. Castelos sao blocos rigidos que RACHAM por tensao
// (|dist-len|/len acima do limite do material rompe o vinculo) e desabam
// de verdade; escombros empilham. Derrube a COROA com as pedras que tem.
//
// Modulos: main.js (cenas/visao/HUD) + fisica.js (mundo verlet, em
// UNIDADES DE PROJETO 240x280 — a fisica e a mesma em toda tela, a vista
// so escala). Engine e fisica sao DEPS do hub (API 30). Render (engine
// 1.2): cenario na camada suja E.dirty com painter de ceu/chao, HUD fora
// do recorte, menus em cena static. Trilha chiptune e o relogio: racha
// no tempo forte vale DOBRO.

var E = require("celeros.engine");
var F = require("fisica");

E.init({ dir: "Arrasa", fps: 30, native: true, save: "arrasa.", particles: 96 });
var W = E.W, H = E.H, u = E.u;
var T = E.theme;

if (typeof __harness !== "undefined") __harness.arrasa = { F: F, E: E };

// ------------------------------------------------------------- vista -----
// mundo 240x280 em unidades de projeto; ESC escala pra caber na tela
// (altura manda: sobra lateral vira ceu/chao continuo do painter)
var HUD_H = u(18);
var ESC = Math.min(W / F.MW, (H - HUD_H) / F.MH);
var OX = Math.floor((W - F.MW * ESC) / 2);
function sx(x) { return Math.round(OX + x * ESC); }
function sy(y) { return Math.round(y * ESC); }
function mwx(tx) { return (tx - OX) / ESC; }
function mwy(ty) { return ty / ESC; }

var C = {
    ceuTopo: System.color(96, 146, 200),
    ceuBaixo: System.color(214, 202, 172),
    grama: System.color(84, 148, 66),
    terra: System.color(112, 80, 52),
    madeira: System.color(188, 132, 74),
    pedra: System.color(146, 146, 152),
    proj: System.color(104, 100, 94),
    couro: System.color(92, 62, 36),
    mastro: System.color(120, 84, 48),
    ouro: 0xFFE0, branco: 0xFFFF, vermelho: 0xF800,
    noite: System.color(26, 22, 34),
    painel: System.color(38, 32, 46),
    texto: System.color(232, 226, 214)
};
C.madSombra = System.mixColor(C.madeira, 0x0000, 32);
C.pedSombra = System.mixColor(C.pedra, 0x0000, 32);

// painter do fundo (chamado pela camada suja): ceu em degradue ate a
// linha do chao, grama + terra abaixo; as bordas laterais continuam o ceu
function corCeu(yT) {
    var t = Math.max(0, Math.min(1, yT / (F.CHAO * ESC)));
    return System.mixColor(C.ceuTopo, C.ceuBaixo, Math.round(t * 100));
}
function painterFundo(x, y, w, h) {
    var cT = F.CHAO * ESC;
    if (y < cT) {
        var hc = Math.min(h, cT - y);
        System.fillGradient(x, y, w, hc, corCeu(y), corCeu(y + hc - 1));
    }
    if (y + h > cT) {
        var gy = Math.max(y, cT), gh = (y + h) - gy;
        System.fillRect(x, gy, w, gh, C.terra);
        var fg = Math.min(Math.round(3 * ESC), (y + h) - cT);
        if (fg > 0 && y <= cT + 3 * ESC) System.fillRect(x, cT, w, fg, C.grama);
    }
}

// -------------------------------------------------------------- audio ----
// Am 104 bpm, loop de 4 compassos; nota = [midi, semicolcheias]
var SONG = {
    bpm: 104, loops: 8,
    tracks: [
        { wave: "tri", vol: 58, notes: [
            [45, 4], [45, 4], [45, 4], [45, 4], [41, 4], [41, 4], [41, 4], [41, 4],
            [43, 4], [43, 4], [43, 4], [43, 4], [40, 4], [40, 4], [43, 4], [43, 4]
        ] },
        { wave: "sq", vol: 30, notes: [
            [69, 2], [72, 2], [76, 4], [72, 2], [69, 2], [76, 4],
            [65, 2], [69, 2], [72, 4], [69, 2], [65, 2], [72, 4],
            [67, 2], [71, 2], [74, 4], [71, 2], [67, 2], [74, 4],
            [64, 2], [67, 2], [71, 4], [72, 2], [74, 2], [76, 4]
        ] },
        { drum: true, vol: 52, notes: [
            [36, 4], [42, 2], [42, 2], [38, 4], [42, 2], [42, 2],
            [36, 4], [42, 2], [42, 2], [38, 4], [42, 2], [42, 2],
            [36, 4], [42, 2], [42, 2], [38, 4], [42, 2], [42, 2],
            [36, 4], [42, 2], [42, 2], [38, 2], [38, 2], [36, 4]
        ] }
    ]
};

var SND = {
    puxa: [[240, 25], [290, 20]],
    lanca: [[950, 45], [620, 40], [380, 35]],
    craM: [[1250, 16], [760, 12]],
    craP: [[520, 14], [330, 11], [210, 9]],
    thud: [[170, 22], [85, 18]],
    coroa: [[523, 70], [659, 70], [784, 120]],
    perdeu: [[330, 120], [262, 150], [196, 220]],
    ok: [[660, 25], [880, 30]]
};

// racha no tempo forte (compasso 4/4: batida 0 de cada 4) vale dobro
function critico() {
    var b = E.audio.beat();
    if (b < 0) return false;
    var f = Math.floor(b);
    return f % 4 === 0 && (b - f) < 0.2;
}

// --------------------------------------------------------- estado jogo ---
var g = { score: 0, novoRec: false, fimT: 0, venceu: false, dica: true, hi: 0 };
var D = E.dirty;

function resetJogo() {
    F.init(null, 5);
    g.score = 0; g.novoRec = false; g.fimT = 0; g.venceu = false;
}

// ------------------------------------------------------------ desenho ----
function corMat(m) {
    if (m === F.MAT.MADEIRA) return C.madeira;
    if (m === F.MAT.PEDRA) return C.pedra;
    if (m === F.MAT.OURO) return C.ouro;
    return C.texto;
}

function drawQuad(xy, bl) {
    // cantos externos da grade 3x2: topo-esq, topo-dir, base-esq, base-dir
    var p = [bl.pts[0], bl.pts[2], bl.pts[3], bl.pts[5]];
    var x0 = sx(xy[p[0] * 2]), y0 = sy(xy[p[0] * 2 + 1]);
    var x1 = sx(xy[p[1] * 2]), y1 = sy(xy[p[1] * 2 + 1]);
    var x2 = sx(xy[p[2] * 2]), y2 = sy(xy[p[2] * 2 + 1]);
    var x3 = sx(xy[p[3] * 2]), y3 = sy(xy[p[3] * 2 + 1]);
    var cor = corMat(bl.mat);
    var som = bl.mat === F.MAT.MADEIRA ? C.madSombra : C.pedSombra;
    System.fillTriangle(x0, y0, x1, y1, x2, y2, cor);
    System.fillTriangle(x1, y1, x2, y2, x3, y3, som);
    var mnx = Math.min(x0, x1, x2, x3), mny = Math.min(y0, y1, y2, y3);
    var mxx = Math.max(x0, x1, x2, x3), mxy = Math.max(y0, y1, y2, y3);
    D.add(mnx - 1, mny - 1, mxx - mnx + 3, mxy - mny + 3);
}

function drawVigas(xy, st) {
    var esp = Math.max(2, Math.round(2.2 * ESC));
    for (var i = 0; i < st.length / 2; i++) {
        var m = F.sMat[i];
        if (m !== F.MAT.MADEIRA && m !== F.MAT.PEDRA && m !== F.MAT.OURO) continue;
        var ba = F.blockOf[st[i * 2]];
        if (ba < 0 || F.blocos[ba].vivos > 7) continue;
        var a = st[i * 2] * 2, b = st[i * 2 + 1] * 2;
        var x0 = sx(xy[a]), y0 = sy(xy[a + 1]), x1 = sx(xy[b]), y1 = sy(xy[b + 1]);
        System.drawWideLine(x0, y0, x1, y1, esp, corMat(m));
        D.add(Math.min(x0, x1) - esp, Math.min(y0, y1) - esp,
              Math.abs(x1 - x0) + esp * 2 + 2, Math.abs(y1 - y0) + esp * 2 + 2);
    }
}

function drawCoroa(xy) {
    if (!F.coroaB) return;
    var p = F.coroaB.pts;
    var x0 = sx(xy[p[0] * 2]), y0 = sy(xy[p[0] * 2 + 1]);
    var x2 = sx(xy[p[2] * 2]), y2 = sy(xy[p[2] * 2 + 1]);
    var cx = (x0 + x2) / 2, cy = (y0 + y2) / 2;
    var r = Math.max(3, Math.round(3.2 * ESC));
    var h = r + Math.max(2, Math.round(1.6 * ESC));
    // pontas da coroa sobre o mini-bloco
    System.fillTriangle(x0, y0, x0, y0 - h, cx - r / 2, cy - h / 2, C.ouro);
    System.fillTriangle(x2, y2, x2, y2 - h, cx + r / 2, cy - h / 2, C.ouro);
    System.fillCircle(cx, cy - h / 3, Math.max(1, r / 2), C.ouro);
    D.add(Math.min(x0, x2) - r - 1, cy - h - r, Math.abs(x2 - x0) + r * 2 + 3, h + r * 2);
}

function drawEstilingue() {
    var esp = Math.max(2, Math.round(2.4 * ESC));
    var bx = sx(31), by = sy(F.CHAO), ty = sy(204);
    System.drawWideLine(bx, by, bx, ty, esp, C.mastro);
    System.drawWideLine(bx, ty, sx(F.GARFO[0].x), sy(F.GARFO[0].y), esp, C.mastro);
    System.drawWideLine(bx, ty, sx(F.GARFO[1].x), sy(F.GARFO[1].y), esp, C.mastro);
    // elasticos ate os pontos de cima da pedra presa (mundo B)
    if (F.pedra && F.pedra.local === "B" && F.mundoB) {
        var xyB = F.mundoB.xy();
        var fe = Math.max(1, Math.round(0.9 * ESC));
        System.drawWideLine(sx(F.GARFO[0].x), sy(F.GARFO[0].y),
                            sx(xyB[F.pedra.pts[0] * 2]), sy(xyB[F.pedra.pts[0] * 2 + 1]), fe, C.couro);
        System.drawWideLine(sx(F.GARFO[1].x), sy(F.GARFO[1].y),
                            sx(xyB[F.pedra.pts[1] * 2]), sy(xyB[F.pedra.pts[1] * 2 + 1]), fe, C.couro);
    }
    D.add(sx(18) - 2, ty - 2, sx(46) - sx(18) + 4, by - ty + 4);
}

function drawPedra(xy, pts) {
    var r = Math.max(2, Math.round(3.4 * ESC));
    var cx = 0, cy = 0;
    for (var i = 0; i < 4; i++) { cx += xy[pts[i] * 2]; cy += xy[pts[i] * 2 + 1]; }
    cx = sx(cx / 4); cy = sy(cy / 4);
    System.fillSmoothCircle(cx, cy, r, C.proj);
    System.fillCircle(cx - r / 3, cy - r / 3, Math.max(1, r / 3),
                      System.mixColor(C.proj, 0xFFFF, 30));
    D.add(cx - r - 1, cy - r - 1, r * 2 + 3, r * 2 + 3);
}

function drawMundo() {
    var xy = F.mundo.xy();
    var st = F.mundo.sticks();
    drawEstilingue();
    for (var e = 0; e < F.entulhos.length; e++) drawPedra(xy, F.entulhos[e]);
    if (F.pedra) {
        var xyp = F.pedra.local === "B" ? F.mundoB.xy() : xy;
        drawPedra(xyp, F.pedra.pts);
    }
    for (var b = 0; b < F.blocos.length; b++) {
        if (F.blocos[b].vivos > 7) drawQuad(xy, F.blocos[b]);
    }
    drawVigas(xy, st);
    drawCoroa(xy);
    if (g.dica && F.pedra && F.pedra.local === "B" && F.pedra.estado === "pronta") {
        System.setTextDatum(5);
        System.setTextColor(C.texto, corCeu(sy(150)));
        System.drawString("puxe a pedra para tras e solte", sx(120), sy(150));
        System.setTextDatum(0);
        D.add(sx(40), sy(142), sx(200) - sx(40), Math.round(18 * ESC));
    }
}

// HUD fora do recorte: so repinta quando o que mostra muda
var hudKey = "";
function drawHUD(force) {
    var key = g.score + "|" + F.pedras + "|" + (g.dica ? 1 : 0);
    if (!force && key === hudKey) return;
    hudKey = key;
    System.fillRect(0, 0, W, HUD_H, C.noite);
    System.drawFastHLine(0, HUD_H, W, System.mixColor(C.noite, C.ouro, 40));
    E.gfx.text(String(g.score), u(6), HUD_H / 2, { valign: "middle", px: u(12),
               color: C.texto, screen: true });
    E.gfx.text("RECORDE " + g.hi, u(6), HUD_H - u(2), { valign: "bottom", ts: "tiny",
               color: System.mixColor(C.noite, C.texto, 55), screen: true });
    // fichas de pedra
    var rr = Math.max(2, u(3)), gy = HUD_H / 2;
    var gx = W - u(40);
    for (var p = 0; p < 5; p++) {
        var cx = gx - p * (rr * 2 + u(2));
        if (p < F.pedras) System.fillCircle(cx, gy, rr, C.proj);
        else System.drawCircle(cx, gy, rr, System.mixColor(C.noite, C.texto, 45));
    }
    // botao pausa (II)
    var pw = u(22), px = W - pw - u(4), py = u(3), ph = HUD_H - u(6);
    E.gfx.rect(px, py, pw, ph, C.painel, { r: u(4), screen: true });
    var bw = Math.max(2, u(2)), bh = Math.round(ph * 0.4);
    System.fillRect(px + pw / 2 - bw - u(1), py + (ph - bh) / 2, bw, bh, C.texto);
    System.fillRect(px + pw / 2 + u(1), py + (ph - bh) / 2, bw, bh, C.texto);
}

// --------------------------------------------------------- eventos -------
function processaEventos(evs) {
    var somCrack = false;
    for (var i = 0; i < evs.length; i++) {
        var ev = evs[i];
        if (ev.t === "crack") {
            var mult = critico() ? 2 : 1;
            var val = ev.valor * mult;
            g.score += val;
            E.fx.burst(sx(ev.x), sy(ev.y), { n: 5, color: corMat(ev.mat),
                        speed: 26 * ESC, life: 0.4, size: Math.max(1, ESC) });
            E.fx.popText(sx(ev.x), sy(ev.y), "+" + val,
                         { color: mult > 1 ? C.ouro : C.texto, ts: "tiny" });
            if (mult > 1) E.fx.popText(sx(ev.x), sy(ev.y) - u(10), "NO TEMPO!",
                                       { color: C.ouro, ts: "tiny", life: 0.9 });
            if (!somCrack) {
                E.audio.sfx(ev.mat === F.MAT.PEDRA ? SND.craP : SND.craM);
                somCrack = true;
            }
        } else if (ev.t === "lancou") {
            g.dica = false;
            E.audio.sfx(SND.lanca);
        } else if (ev.t === "impacto") {
            E.cam.shake(Math.min(6, 2 + ev.forca / 6), 0.25);
            E.audio.sfx(SND.thud);
        } else if (ev.t === "coroa") {
            g.score += 500;
            g.venceu = true;
            E.fx.flash(C.ouro, 400);
            E.fx.popText(sx(ev.x), sy(ev.y) - u(14), "A COROA CAIU!", { color: C.ouro,
                       ts: "label", life: 1.6 });
            E.audio.sfx(SND.coroa);
        } else if (ev.t === "morreu") {
            if (F.pedras > 0) F.arMar();
        }
    }
}

// ------------------------------------------------------------- cenas -----
E.run({
    titulo: {
        static: true,
        enter: function () {
            E.cam.reset();
            g.hi = E.save.num("hi", 0);
        },
        update: function () {
            if (!this.btn) return;
            if (E.hit(this.btn[0])) {
                E.audio.sfx(SND.ok);
                E.goto("jogando");
            } else if (E.hit(this.btn[1])) {
                System.exitApp();
            }
        },
        draw: function () {
            System.fillScreen(C.noite);
            // ceu do jogo como pano de fundo do titulo
            System.fillGradient(0, 0, W, Math.round(H * 0.42), C.ceuTopo, C.ceuBaixo);
            System.fillRect(0, Math.round(H * 0.42), W, u(5), C.grama);
            System.fillRect(0, Math.round(H * 0.42) + u(5), W, H, C.terra);
            E.gfx.text("ARRASA!", W / 2, Math.round(H * 0.17), { align: "center",
                       px: u(26), fit: W - u(20), color: C.ouro, screen: true });
            E.gfx.text("estilingue de fisica real: puxe a pedra e derrube a coroa",
                       W / 2, Math.round(H * 0.30), { align: "center", ts: "small",
                       fit: W - u(14), color: C.texto, screen: true });
            E.gfx.text("os castelos racham e desabam de verdade", W / 2,
                       Math.round(H * 0.30) + u(12), { align: "center", ts: "small",
                       fit: W - u(14), color: C.texto, screen: true });
            if (g.hi > 0) {
                E.gfx.text("RECORDE " + g.hi, W / 2, Math.round(H * 0.42) - u(10),
                           { align: "center", ts: "small", color: C.ouro, screen: true });
            }
            var bw = Math.min(u(150), W - u(40)), bh = u(27);
            var bx = Math.round((W - bw) / 2), by = H - bh * 2 - u(10) - u(14);
            this.btn = [
                E.gfx.button("JOGAR", bx, by, bw, bh, { color: C.ouro, screen: true }),
                E.gfx.button("SAIR", bx, by + bh + u(10), bw, bh, {
                    primary: false, bg: C.painel,
                    stroke: System.mixColor(C.ouro, C.noite, 45),
                    textColor: C.texto, screen: true })
            ];
        }
    },

    jogando: {
        fps: 30,
        enter: function () {
            resetJogo();
            hudKey = "";
            D.enable(painterFundo);
            D.clip(0, HUD_H + 1, W, H - HUD_H - 1);
            E.cam.reset();
            if (E.data.musicPos !== undefined) {
                E.audio.music(SONG, { startMs: E.data.musicPos });
                E.data.musicPos = undefined;
            } else {
                E.audio.music(SONG);
            }
        },
        update: function (dt) {
            var inp = E.input;
            if (inp.justDown && F.pedra && F.pedra.estado === "pronta") {
                if (F.puxar(mwx(inp.x), mwy(inp.y))) E.audio.sfx(SND.puxa);
            }
            if (inp.down && F.pedra && F.pedra.estado === "carregando") {
                F.arrastando(mwx(inp.x), mwy(inp.y));
            }
            if (inp.justUp) F.soltar();

            processaEventos(F.step());

            // pausa: botao II do HUD
            if (inp.tap && inp.tap.x > W - u(30) && inp.tap.y < HUD_H + u(4)) {
                E.audio.sfx(SND.ok);
                var pos = typeof System.musicPos === "function" ? System.musicPos() : -1;
                E.data.musicPos = pos > 0 ? pos : undefined;
                E.goto("pausa");
                return;
            }
            // vitoria: deixa o escombro assentar 1,6 s antes do placar
            if (g.venceu) {
                g.fimT += dt;
                if (g.fimT > 1.6) { E.goto("fim"); return; }
            } else if (F.pedras === 0 && F.pedra && F.pedra.estado === "entulho") {
                E.goto("fim");
            }
        },
        draw: function () {
            drawMundo();
            E.fx.draw();
            D.unclip();
            drawHUD(false);
        }
    },

    pausa: {
        static: true,
        enter: function () {
            E.audio.stop();
            this.first = true;
        },
        update: function () {
            if (!this.btn) return;
            if (E.hit(this.btn[0])) {
                E.audio.sfx(SND.ok);
                E.goto("jogando");
            } else if (E.hit(this.btn[1])) {
                System.exitApp();
            }
        },
        draw: function () {
            var pw = Math.min(u(180), W - u(30)), ph = u(120);
            var px = Math.round((W - pw) / 2), py = Math.round((H - ph) / 2);
            if (this.first) {
                this.first = false;
                E.gfx.panel(px, py, pw, ph, { bg: C.painel, stroke: C.ouro, screen: true });
                E.gfx.text("PAUSA", W / 2, py + u(24), { align: "center", valign: "middle",
                           px: u(20), color: C.ouro, screen: true });
                E.gfx.text(g.score + " pontos", W / 2, py + u(42), { align: "center",
                           ts: "small", color: C.texto, screen: true });
            }
            var bw = pw - u(30), bh = u(25), bx = Math.round((W - bw) / 2), by = py + u(58);
            System.fillRect(bx - 2, by - 2, bw + 4, bh * 2 + u(10) + 4, C.painel);
            this.btn = [
                E.gfx.button("CONTINUAR", bx, by, bw, bh, { color: C.ouro, screen: true }),
                E.gfx.button("SAIR", bx, by + bh + u(10), bw, bh, {
                    primary: false, bg: C.noite, stroke: C.ouro,
                    textColor: C.texto, screen: true })
            ];
        }
    },

    fim: {
        static: true,
        enter: function () {
            E.audio.stop();
            this.t = 0;
            this.first = true;
            // vitoria: pedras nao usadas viram bonus
            if (g.venceu) {
                g.score += F.pedras * 100;
                E.audio.sfx(SND.coroa);
            } else {
                E.audio.sfx(SND.perdeu);
            }
            g.novoRec = E.save.best("hi", g.score);
            if (g.novoRec) g.hi = g.score;
        },
        update: function (dt) {
            this.t += dt;
            if (this.t < 0.5 || !this.btn) return;   // engole o tap fantasma
            if (E.hit(this.btn[0])) {
                E.audio.sfx(SND.ok);
                E.goto("jogando");
            } else if (E.hit(this.btn[1])) {
                E.goto("titulo");
            }
        },
        draw: function () {
            if (this.first) {
                this.first = false;
                System.fillScreen(C.noite);
                E.gfx.text(g.venceu ? "A COROA CAIU!" : "AS PEDRAS ACABARAM", W / 2,
                           Math.round(H * 0.16), { align: "center", px: u(18),
                           fit: W - u(20), color: g.venceu ? C.ouro : C.vermelho,
                           screen: true });
                E.gfx.text(String(g.score), W / 2, Math.round(H * 0.30), { align: "center",
                           px: u(30), color: C.texto, screen: true });
                E.gfx.text("pontos", W / 2, Math.round(H * 0.30) + u(24), { align: "center",
                           ts: "small", color: System.mixColor(C.noite, C.texto, 55),
                           screen: true });
                if (g.novoRec) {
                    E.gfx.text("NOVO RECORDE!", W / 2, Math.round(H * 0.30) + u(40),
                               { align: "center", ts: "label", color: C.ouro, screen: true });
                } else {
                    E.gfx.text("recorde " + g.hi, W / 2, Math.round(H * 0.30) + u(40),
                               { align: "center", ts: "small",
                                 color: System.mixColor(C.noite, C.texto, 55), screen: true });
                }
            }
            var bw = Math.min(u(150), W - u(40)), bh = u(26);
            var bx = Math.round((W - bw) / 2), by = H - bh * 2 - u(10) - u(14);
            this.btn = [
                E.gfx.button("DE NOVO", bx, by, bw, bh, { color: C.ouro, screen: true }),
                E.gfx.button("MENU", bx, by + bh + u(10), bw, bh, {
                    primary: false, bg: C.painel,
                    stroke: System.mixColor(C.ouro, C.noite, 45),
                    textColor: C.texto, screen: true })
            ];
        }
    }
}, "titulo");
