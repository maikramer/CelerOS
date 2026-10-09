// Detona! — bomberman de grade no CelerOS, sobre a game engine do SDK.
// Tela cheia nos pixels nativos do vidro (API 28; 480x480 no SmartDisplay
// 4"), trilha chiptune como relogio: o pavio das bombas e quantizado na
// BATIDA e as correntes cascateiam de meio tempo em meio tempo — a
// explosao cai no compasso. Modulos: main.js (cenas/visao/HUD) +
// arena.js (simulacao) + niveis.js (geracao por seed, temas, trilhas).
// engine.js/physics.js vendorizadas por enquanto (migram p/ deps do hub).
//
// Pega de teste (so existe no harness): o test.js dirige a sim.
if (typeof __harness !== "undefined") {
    __harness.detona = { arena: require("arena"), niveis: require("niveis"),
                         ia: require("ia"), E: null };   // E entra depois do require abaixo
}

var E = require("engine");
var arena = require("arena");
var NV = require("niveis");
var ia = require("ia");
if (typeof __harness !== "undefined") __harness.detona.E = E;

E.init({ dir: "Detona", fps: 30, native: true, save: "detona.", particles: 140 });
var W = E.W, H = E.H;

// layout adaptativo: grade 15x13 + faixa de HUD; no 480 nativo da 32px
// de celula com HUD de 64, no canvas virtual 240x320 da 16px com HUD 44
var CELL = Math.min(Math.floor(W / NV.COLS), Math.floor((H - 44) / NV.ROWS));
var HUD = H - CELL * NV.ROWS;
var OX = Math.floor((W - CELL * NV.COLS) / 2);
var OY = 0;
var SZ = { jogador: CELL - 2, bomba: CELL - 4, bloco: CELL };
// PNGs sao masterizados p/ celula 32: em tela menor o blit 1:1 estoura
// a grade — sem nativo/sem folga os painters assumem (e ganham slot)
var USA_PNG = E.caps.native && CELL >= 28;

var C = {
    preto: 0x0000,
    branco: 0xFFFF, ouro: 0xFFE0, laranja: 0xFD20, vermelho: 0xF800,
    verde: 0x07E0, ciano: 0x07FF, magenta: 0xF81F,
    cinza: System.mixColor(0x0000, 0xFFFF, 25)
};

var hi = E.save.num("hi", 0);
var prog = { mundo: E.save.num("mundo", 1), nivel: E.save.num("nivel", 1) };

// ------------------------------------------------------------- sprites ---
function painterJogador(w, h, x, y) {
    var r = w * 0.42;
    System.fillCircle(x + w / 2, y + h / 2, r, C.branco);
    System.fillCircle(x + w / 2, y + h / 2, r * 0.62, C.ciano);
    System.fillCircle(x + w / 2, y + h * 0.4, r * 0.34, 0x001F);
}
function painterBomba(w, h, x, y) {
    System.fillCircle(x + w / 2, y + h / 2 + 2, w * 0.36, 0x4208);
    System.fillCircle(x + w / 2 - 3, y + h / 2 - 3, Math.max(2, w * 0.1), C.cinza);
    System.fillRect(x + w / 2 - 1, y + 2, 2, Math.max(3, h * 0.2), 0x8410);
}
function painterMacio(w, h, x, y) {
    System.fillRect(x + 1, y + 1, w - 2, h - 2, 0x8410);
    System.drawLine(x + 1, y + 1, x + w - 2, y + h - 2, 0x4208);
    System.drawLine(x + w - 2, y + 1, x + 1, y + h - 2, 0x4208);
    System.fillRect(x + 1, y + 1, w - 2, 2, 0xA514);
}
function painterDuro(w, h, x, y) {
    System.fillRect(x, y, w, h, 0x7BEF);
    System.fillRect(x + 1, y + 1, w - 2, h - 2, 0x528A);
    System.fillRect(x + 2, y + 2, w - 4, 3, 0x39E7);
    System.fillRect(x + 2, y + 2, 3, h - 4, 0x39E7);
}
function painterBalao(w, h, x, y) {
    var r = w * 0.42;
    System.fillCircle(x + w / 2, y + h / 2, r, 0xFE19);
    System.fillCircle(x + w / 2 - 3, y + h / 2 - 3, r * 0.3, 0xFFE0);
    System.fillRect(x + w / 2 - 4, y + h / 2 + 1, 2, 3, 0x001F);
    System.fillRect(x + w / 2 + 2, y + h / 2 + 1, 2, 3, 0x001F);
}
function painterFantasma(w, h, x, y) {
    System.fillCircle(x + w / 2, y + h * 0.42, w * 0.4, 0x5FDF);
    System.fillRect(x + w * 0.1, y + h * 0.42, w * 0.8, h * 0.28, 0x5FDF);
    System.fillRect(x + w / 2 - 4, y + h * 0.4, 2, 3, 0x001F);
    System.fillRect(x + w / 2 + 2, y + h * 0.4, 2, 3, 0x001F);
}
function painterCacador(w, h, x, y) {
    System.fillCircle(x + w / 2, y + h / 2, w * 0.42, 0xFD00);
    System.fillCircle(x + w / 2, y + h * 2 / 3, w * 0.2, 0xFB80);
    System.fillRect(x + w / 2 - 5, y + h / 2 - 3, 3, 3, C.preto);
    System.fillRect(x + w / 2 + 2, y + h / 2 - 3, 3, 3, C.preto);
}
function painterChefe(w, h, x, y) {
    System.fillCircle(x + w / 2, y + h / 2, w * 0.46, 0x4208);
    System.fillCircle(x + w / 2, y + h / 2, w * 0.38, 0x630C);
    System.fillTriangle(x + w * 0.3, y + 6, x + w * 0.45, y + 6,
                        x + w * 0.37, y + h * 0.16, 0xFFE0);
    System.fillTriangle(x + w * 0.55, y + 6, x + w * 0.7, y + 6,
                        x + w * 0.63, y + h * 0.16, 0xFFE0);
    System.fillCircle(x + w * 0.38, y + h * 0.45, 4, C.vermelho);
    System.fillCircle(x + w * 0.62, y + h * 0.45, 4, C.vermelho);
}
var SPR = E.spr.load([
    { name: "jogador", file: USA_PNG ? "jogador" : null,
      w: SZ.jogador, h: SZ.jogador, paint: painterJogador },
    { name: "bomba", file: USA_PNG ? "bomba" : null,
      w: SZ.bomba, h: SZ.bomba, paint: painterBomba },
    { name: "macio", file: USA_PNG ? "bloco_macio" : null,
      w: SZ.bloco, h: SZ.bloco, paint: painterMacio },
    { name: "duro", file: USA_PNG ? "bloco_duro" : null,
      w: SZ.bloco, h: SZ.bloco, paint: painterDuro },
    { name: "balao", file: USA_PNG ? "balao" : null,
      w: SZ.jogador, h: SZ.jogador, paint: painterBalao },
    { name: "fantasma", file: USA_PNG ? "fantasma" : null,
      w: SZ.jogador, h: SZ.jogador, paint: painterFantasma },
    { name: "cacador", file: USA_PNG ? "perseguidor" : null,
      w: SZ.jogador, h: SZ.jogador, paint: painterCacador },
    { name: "chefe", file: USA_PNG ? "chefe" : null,
      w: CELL * 2 - 4, h: CELL * 2 - 4, paint: painterChefe }
]);

// ---------------------------------------------------------------- sons ---
E.audio.sfxTable.planta = [700, 35];
E.audio.sfxTable.bum = [[100, 70], [60, 150], [40, 120]];
E.audio.sfxTable.power = E.audio.sfxTable.coin;
E.audio.sfxTable.escudo = [[1200, 40], [900, 60]];
E.audio.sfxTable.morte = [[300, 90], [220, 90], [140, 220]];
E.audio.sfxTable.venceu = [[523, 90], [659, 90], [784, 160]];
E.audio.sfxTable.bicho = [[520, 40], [390, 70]];
E.audio.sfxTable.hit = [220, 50];
E.audio.sfxTable.chute = [[440, 30], [660, 40]];

// ---------------------------------------------------------------- cenas --
var RECT_JOGAR, RECT_SOBRE;   // botoes: hit no update, desenho no draw

var tituloBase = false;
function drawTituloBase() {
    System.drawPNG(E.spr.bases[0] + "titulo.png", 0, 0);
    tituloBase = true;
}

E.run({
    titulo: {
        fps: 30,
        enter: function () {
            tituloBase = false;
            hi = E.save.num("hi", 0);
            E.audio.music(NV.SONG_MENU);
        },
        update: function () {
            RECT_JOGAR = { x: W / 2 - 90, y: H * 0.56, w: 180, h: 50 };
            RECT_SOBRE = { x: W / 2 - 90, y: H * 0.56 + 62, w: 180, h: 50 };
            if (E.hit(RECT_JOGAR)) {
                E.data.retomar = false;
                E.data.modo = "campanha";
                prog.mundo = E.save.num("mundo", 1);
                prog.nivel = E.save.num("nivel", 1);
                E.goto("jogando");
            } else if (E.hit(RECT_SOBRE)) {
                E.data.retomar = false;
                E.data.modo = "sobre";
                E.goto("jogando");
            }
        },
        draw: function () {
            if (!tituloBase) drawTituloBase();
            // repinta so a metade de baixo (o PNG fica; nada de decode/frame)
            System.fillRect(0, Math.floor(H * 0.5), W, Math.ceil(H * 0.5) + 1, C.preto);
            E.gfx.text("DETONA!", W / 2, Math.floor(H * 0.36), {
                color: C.ouro, size: 3, align: "center", screen: true });
            E.gfx.text("campanha " + hi + " . sobrevivencia " + E.save.num("sobre.hi", 0),
                       W / 2, Math.floor(H * 0.47), {
                       color: C.cinza, size: 1, align: "center", screen: true });
            E.gfx.button("JOGAR", RECT_JOGAR.x, RECT_JOGAR.y,
                         RECT_JOGAR.w, RECT_JOGAR.h, { primary: true });
            E.gfx.button("SOBREVIVENCIA", RECT_SOBRE.x, RECT_SOBRE.y,
                         RECT_SOBRE.w, RECT_SOBRE.h);
            E.gfx.text("arraste p/ andar . toque p/ bomba", W / 2,
                       Math.floor(H * 0.56) + 128, {
                       color: C.cinza, size: 1, align: "center", screen: true });
        }
    },

    jogando: {
        fps: 0,   // sem teto: o frame vale o que a placa der
        enter: function () {
            var sobre = E.data.modo === "sobre";
            if (!E.data.retomar) {
                var plano = sobre ? NV.gerar(1, 1, { sobrevivencia: true })
                                  : NV.gerar(prog.mundo, prog.nivel);
                arena.iniciar(prog.mundo, prog.nivel, plano);
                arena.layout(CELL, OX, OY);
                wireArena();
                ia.ligar();               // bichos do plano (+ chefe no 8º)
                if (sobre) { ia.onda(); E.data.ondaBeat = -1; }
            } else {
                E.data.retomar = false;
            }
            E.audio.music(NV.musica(prog.mundo, prog.nivel, sobre));
        },
        update: function (dt) {
            var s = arena.state();
            // pausa: canto superior direito
            if (E.input.tap && E.input.tap.x > W - 48 && E.input.tap.y < 48) {
                E.data.retomar = true;
                E.goto("pausa");
                return;
            }
            // detonador remoto: chip BOOM no canto inferior direito do HUD
            if (s && s.stats.remote && E.input.tap &&
                E.input.tap.x > W - 76 && E.input.tap.y > CELL * NV.ROWS) {
                arena.detonar();
                E.input.tap = null;
            }
            // direcao por drag (a ultima direcao persiste enquanto desliza)
            var ax = 0, ay = 0;
            if (E.input.down) {
                if (Math.abs(E.input.dx) > Math.abs(E.input.dy)) ax = E.input.dx > 0 ? 1 : -1;
                else if (E.input.dy !== 0) ay = E.input.dy > 0 ? 1 : -1;
            }
            arena.mover(ax, ay, dt);
            if (E.input.tap) arena.plantar();
            arena.update(dt);
            // sobrevivencia: onda na batida (a cada 12) ou arena vazia
            if (s && s.sobrevivencia && !s.fim) {
                var bInt = Math.floor(arena.beatNow());
                if (bInt !== E.data.ondaBeat) {
                    E.data.ondaBeat = bInt;
                    if (bInt % 12 === 0 ||
                        (s.enemies.length === 0 && s.ondaAte <= 0)) ia.onda();
                }
            }
        },
        draw: function () { drawMundo(); }
    },

    pausa: {
        fps: 30,
        update: function () {
            var r1 = { x: W / 2 - 100, y: H * 0.38, w: 200, h: 50 };
            var r2 = { x: W / 2 - 100, y: H * 0.38 + 66, w: 200, h: 50 };
            if (E.hit(r1)) { E.goto("jogando"); }          // retomar (estado vivo)
            if (E.hit(r2)) { arena.parar(); E.goto("titulo"); }
        },
        draw: function () {
            drawMundo();
            System.fillRect(0, 0, W, H, C.preto);
            E.gfx.text("PAUSA", W / 2, Math.floor(H * 0.26), {
                color: C.ouro, size: 3, align: "center", screen: true });
            E.gfx.button("CONTINUAR", W / 2 - 100, H * 0.38, 200, 50, { primary: true });
            E.gfx.button("SAIR", W / 2 - 100, H * 0.38 + 66, 200, 50);
        }
    },

    fim: {
        fps: 30,
        enter: function () {
            E.audio.stop();
            var info = E.data.fimInfo || { fim: 'dead', score: 0 };
            E.data.novoRec = E.save.best(info.sobre ? "sobre.hi" : "hi", info.score);
        },
        update: function () {
            var info = E.data.fimInfo || { fim: 'dead', score: 0 };
            var r1 = { x: W / 2 - 100, y: H * 0.60, w: 200, h: 50 };
            var r2 = { x: W / 2 - 100, y: H * 0.60 + 62, w: 200, h: 44 };
            if (E.hit(r1)) {
                E.data.retomar = false;
                if (!info.sobre && info.fim === 'win') {
                    prog.mundo = E.save.num("mundo", 1);
                    prog.nivel = E.save.num("nivel", 1);
                }
                E.data.modo = info.sobre ? "sobre" : "campanha";
                E.goto("jogando");
            } else if (E.hit(r2)) {
                E.goto("titulo");
            }
        },
        draw: function () {
            var info = E.data.fimInfo || { fim: 'dead', score: 0 };
            System.fillRect(0, 0, W, H, C.preto);
            var titulo = info.sobre ? "ONDA " + info.onda :
                         info.fim === 'win' ? "FASE LIMPA!" : "FIM DE JOGO";
            E.gfx.text(titulo, W / 2, Math.floor(H * 0.30), {
                        color: C.ouro, size: 3, align: "center", screen: true });
            E.gfx.text("PONTOS " + info.score, W / 2, Math.floor(H * 0.44), {
                        color: C.branco, size: 2, align: "center", screen: true });
            if (E.data.novoRec) E.gfx.text("NOVO RECORDE!", W / 2, Math.floor(H * 0.52), {
                        color: C.laranja, size: 2, align: "center", screen: true });
            var rotulo = info.sobre ? "DE NOVO"
                       : info.fim === 'win' ? "PROXIMA FASE" : "TENTAR DE NOVO";
            E.gfx.button(rotulo, W / 2 - 100, H * 0.60, 200, 50, { primary: true });
            E.gfx.button("MENU", W / 2 - 100, H * 0.60 + 62, 200, 44);
        }
    }
}, "titulo");

// ---------------------------------------------------------------- visao --

function wireArena() {
    var s = arena.state();
    s.onSfx = function (nome) {
        if (nome === 'bum') E.audio.duck(650);
        E.audio.sfx(nome);
    };
    s.onFx = function (tipo, a, b) {
        if (tipo === 'bum') {
            var cx = OX + (a.c + 0.5) * CELL, cy = OY + (a.r + 0.5) * CELL;
            E.fx.ring(cx, cy, { speed: CELL * 9, color: C.laranja, life: 0.4 });
            E.fx.burst(cx, cy, { n: 18, colors: [C.ouro, C.laranja, C.vermelho],
                                 speed: CELL * 5, life: 0.5 });
            E.cam.shake(9, 0.35);
            E.fx.flash(C.ouro, 120);
        } else if (tipo === 'macio') {
            var mx = OX + (a.c + 0.5) * CELL, my = OY + (a.r + 0.5) * CELL;
            E.fx.burst(mx, my, { n: 8, colors: [0x8410, 0x6204, C.cinza],
                                 speed: CELL * 3, life: 0.45, grav: CELL * 6 });
        } else if (tipo === 'power') {
            var cel = arena.celulaPlayer();
            var px = OX + (cel.c + 0.5) * CELL, py = OY + (cel.r + 0.5) * CELL;
            E.fx.popText(px, py - CELL, nomePower(b), { color: C.ciano });
        } else if (tipo === 'bicho') {
            // a veio em pixels do centro do bicho
            E.fx.burst(a.x, a.y, { n: 14, colors: [C.branco, C.laranja],
                                   speed: CELL * 4, life: 0.5 });
            E.fx.popText(a.x, a.y - CELL / 2, "+" + a.pontos, { color: C.ouro });
        } else if (tipo === 'morte') {
            E.fx.burst(a.x, a.y, { n: 22, colors: [C.branco, C.ciano],
                                   speed: CELL * 4, life: 0.6 });
            E.fx.flash(C.vermelho, 300);
            E.cam.shake(12, 0.5);
        }
    };
    s.onFim = function () {
        var st = arena.state();
        var sobre = !!st.sobrevivencia;
        E.data.fimInfo = { fim: st.fim, score: st.score, sobre: sobre,
                           onda: st.onda, mundo: st.mundo, nivel: st.nivel };
        if (st.fim === 'win') {
            var nv = st.nivel + 1, mu = st.mundo;
            if (nv > NV.NIVEIS_POR_MUNDO) { nv = 1; mu++; }
            E.save.set("mundo", String(mu));
            E.save.set("nivel", String(nv));
        } else if (sobre) {
            E.save.best("sobre.hi", st.score);
        }
        E.after(900, function () { arena.parar(); E.goto("fim"); });
    };
}

function nomePower(k) {
    if (k === 'B') return "+BOMBA";
    if (k === 'C') return "+CHAMA";
    if (k === 'V') return "VELOCIDADE";
    if (k === 'K') return "CHUTE";
    if (k === 'R') return "DETONADOR";
    if (k === 'E') return "ESCUDO";
    if (k === 'X') return "+VIDA";
    return "?";
}

function drawMundo() {
    var s = arena.state();
    if (!s) return;
    var t = s.tema;
    var beat = arena.beatNow();
    var pulso = beat - Math.floor(beat);   // 0..1 dentro da batida

    // chao procedural: lajotas 2 tons por paridade + variacao por seed
    for (var r = 0; r < NV.ROWS; r++) {
        for (var c = 0; c < NV.COLS; c++) {
            var x = OX + c * CELL, y = OY + r * CELL;
            var v = s.variacao[r * NV.COLS + c];
            var base = (c + r) % 2 === 0 ? t.a : t.b;
            System.fillRect(x, y, CELL, CELL, v > 0.85 ?
                            System.mixColor(base, t.detalhe, 8) : base);
            System.fillRect(x, y + CELL - 1, CELL, 1, t.junta);
            System.fillRect(x + CELL - 1, y, 1, CELL, t.junta);
        }
    }
    // blocos
    for (var r2 = 0; r2 < NV.ROWS; r2++) {
        for (var c2 = 0; c2 < NV.COLS; c2++) {
            var ch = s.grid[r2][c2];
            if (ch === '#') E.spr.blit("duro", OX + c2 * CELL, OY + r2 * CELL);
            else if (ch === '%') E.spr.blit("macio", OX + c2 * CELL, OY + r2 * CELL);
        }
    }
    // saida achada: portinha pulsando na batida
    if (s.exit.achada) {
        var ex = OX + s.exit.c * CELL, ey = OY + s.exit.r * CELL;
        var brilho = s.exit.aberta ? 40 + Math.floor(40 * pulso) : 18;
        System.fillRect(ex + 2, ey + 2, CELL - 4, CELL - 4,
                        System.mixColor(t.a, C.verde, brilho));
        System.fillRect(ex + 5, ey + 4, CELL - 10, CELL - 8,
                        System.mixColor(C.preto, C.verde, 60));
        if (s.exit.aberta) {
            E.gfx.text(">", ex + CELL / 2, ey + CELL / 2, {
                color: C.branco, size: 2, align: "center", valign: "middle" });
        }
    }
    // powerups: caixinha com glifo, pulso na batida
    for (var key in s.powerups) {
        var pc = parseInt(key, 10);
        var pr = parseInt(key.slice(key.indexOf(',') + 1), 10);
        if (s.grid[pr][pc] !== '.') continue;
        var kx = OX + pc * CELL + 2, ky = OY + pr * CELL + 2, kw = CELL - 4;
        var k = s.powerups[key];
        var kor = k === 'B' ? C.ciano : k === 'C' ? C.laranja : k === 'V' ? C.verde :
                  k === 'K' ? C.magenta : k === 'R' ? C.vermelho :
                  k === 'E' ? C.branco : C.ouro;
        var p2 = 0.5 + 0.5 * pulso;
        System.fillRect(kx, ky, kw, kw,
                        System.mixColor(C.preto, kor, 25 + Math.floor(20 * p2)));
        E.gfx.text(glifoPower(k), kx + kw / 2, ky + kw / 2, {
            color: kor, size: 1, align: "center", valign: "middle" });
    }
    // bombas: blit + faisca do pavio piscando na batida
    for (var i = 0; i < s.bombs.length; i++) {
        var bo = s.bombs[i];
        var bx = OX + bo.c * CELL + (CELL - SZ.bomba) / 2;
        var by = OY + bo.r * CELL + (CELL - SZ.bomba) / 2 - Math.floor(2 * pulso);
        E.spr.blit("bomba", bx, by);
        if (pulso < 0.5) System.fillCircle(bx + SZ.bomba / 2, by + 1, 2, C.ouro);
    }
    // labaredas: nucleo + bracos em 3 tons, crescendo dentro da batida
    for (var f = 0; f < s.flames.length; f++) {
        drawChama(s.flames[f], pulso);
    }
    // inimigos: bob no compasso; chefe pisca ao levar acerto
    for (var en = 0; en < s.enemies.length; en++) {
        var e = s.enemies[en];
        if (e.morto) continue;
        var nome = e.kind === 'cacador' ? "cacador" : e.kind;
        var ew = e.kind === 'chefe' ? CELL * 2 - 4 : SZ.jogador;
        var eflash = e.flash > 0;
        if (e.flash > 0) e.flash -= E.dt;
        var exx = e.fx * CELL - ew / 2;
        var eyy = e.fy * CELL - ew / 2 + Math.floor(2 * Math.sin(beat * Math.PI * 2 + en));
        if (eflash && Math.floor(beat * 16) % 2 === 0) {
            System.fillCircle(e.fx * CELL, e.fy * CELL, ew * 0.5, C.branco);
        } else {
            E.spr.blit(nome, exx, eyy);
        }
    }
    // jogador (pisca invulneravel; bob no compasso)
    var inv = s.stats.inv > 0 && Math.floor(beat * 8) % 2 === 0;
    if (!inv && !s.fim) {
        var bob = Math.floor(2 * Math.sin(beat * Math.PI * 2));
        E.spr.blit("jogador", s.player.x - SZ.jogador / 2,
                   s.player.y - SZ.jogador / 2 + bob);
        if (s.stats.shield) System.drawCircle(s.player.x, s.player.y,
                                              SZ.jogador * 0.62, C.ciano);
    }
    // chefe na arena: barra de vida no topo
    for (var ch2 = 0; ch2 < s.enemies.length; ch2++) {
        if (s.enemies[ch2].kind !== 'chefe' || s.enemies[ch2].morto) continue;
        var bw = Math.floor(W * 0.7);
        E.gfx.bar((W - bw) / 2, 6, bw, 8, s.enemies[ch2].hp / 8,
                  { fg: C.vermelho, screen: true });
        break;
    }
    drawHUD(s, pulso);
    E.fx.draw();
}

function drawChama(fl, pulso) {
    var x = OX + fl.c * CELL, y = OY + fl.r * CELL;
    var meia = CELL / 2;
    var cresce = 0.55 + 0.45 * pulso;
    if (fl.tipo === 'nucleo') {
        System.fillCircle(x + meia, y + meia, meia * 0.95 * cresce + 2, C.branco);
        System.fillCircle(x + meia, y + meia, meia * 0.7 * cresce, C.ouro);
        System.fillCircle(x + meia, y + meia, meia * 0.4 * cresce, 0xFB18);
    } else {
        var len = CELL * cresce, esp = Math.floor(CELL * 0.7);
        var x0 = fl.dx > 0 ? x + meia : fl.dx < 0 ? x + meia - len : x + meia - esp / 2;
        var y0 = fl.dy > 0 ? y + meia : fl.dy < 0 ? y + meia - len : y + meia - esp / 2;
        var w_ = fl.dy !== 0 ? esp : len;
        var h_ = fl.dy !== 0 ? len : esp;
        System.fillRect(x0, y0, w_, h_, C.laranja);
        System.fillRect(x0 + (fl.dy !== 0 ? 2 : 0), y0 + (fl.dx !== 0 ? 2 : 0),
                        fl.dy !== 0 ? esp - 4 : w_, fl.dx !== 0 ? esp - 4 : h_, C.ouro);
        if (fl.tipo === 'ponta') {
            System.fillCircle(x + meia + fl.dx * (meia * 0.6),
                              y + meia + fl.dy * (meia * 0.6), 3, C.branco);
        }
    }
}

function glifoPower(k) {
    return k === 'B' ? "B" : k === 'C' ? "C" : k === 'V' ? "V" :
           k === 'K' ? "K" : k === 'R' ? "R" : k === 'E' ? "E" : "+";
}

function drawHUD(s, pulso) {
    var y0 = OY + NV.ROWS * CELL;
    var t = s.tema;
    System.fillRect(0, y0, W, H - y0, t.hud);
    System.fillRect(0, y0, W, 2, t.detalhe);
    var cy = Math.floor((y0 + H) / 2);

    // vidas: coracoes
    for (var v = 0; v <= s.stats.vidas && v < 6; v++) {
        System.fillCircle(8 + v * 14, cy - 4, 4, C.vermelho);
        System.fillCircle(12 + v * 14, cy - 4, 4, C.vermelho);
        System.fillTriangle(4 + v * 14, cy - 1, 16 + v * 14, cy - 1,
                            10 + v * 14, cy + 6, C.vermelho);
    }
    // contadores: bomba / chama / patins
    E.gfx.text("B" + s.stats.bombs + " C" + s.stats.flame + " V" + s.stats.vel,
               Math.floor(W * 0.34), cy, {
               color: t.hudTxt, size: 2, valign: "middle", screen: true });
    // tempo (vermelho piscando no fim) ou onda na sobrevivencia
    if (s.sobrevivencia) {
        E.gfx.text("ONDA " + s.onda, W / 2 + 46, cy, {
            color: t.hudTxt, size: 2, valign: "middle", screen: true });
        E.gfx.text("SOBRE", W / 2 + 46, y0 + 9, {
            color: t.hudTxt, size: 1, align: "center", screen: true });
    } else {
        var seg = Math.max(0, Math.ceil(s.tLeft));
        E.gfx.text(String(seg), W / 2 + 46, cy, {
            color: seg < 30 && pulso > 0.5 ? C.vermelho : t.hudTxt,
            size: 2, valign: "middle", screen: true });
        E.gfx.text(s.mundo + "." + s.nivel, W / 2 + 46, y0 + 9, {
            color: t.hudTxt, size: 1, align: "center", screen: true });
    }
    E.gfx.text(String(s.score), s.stats.remote ? W - 76 : W - 10, cy, {
        color: C.ouro, size: 2, align: "right", valign: "middle", screen: true });
    // detonador remoto: chip BOOM pulsando no canto direito
    if (s.stats.remote) {
        var bx = W - 64, by = y0 + 6, bw2 = 56, bh = H - y0 - 12;
        var pulsa = 20 + Math.floor(18 * pulso);
        System.fillRect(bx, by, bw2, bh,
                        System.mixColor(C.preto, C.vermelho, pulsa));
        E.gfx.text("BOOM", bx + bw2 / 2, y0 + (H - y0) / 2, {
            color: C.branco, size: 1, align: "center", valign: "middle", screen: true });
    }
}
