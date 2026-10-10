// fisica.js — o mundo verlet do Arrasa! (celeros.physics, API 31 nativa).
// Tudo aqui roda em UNIDADES DE PROJETO (240 de largura): a fisica e a
// mesma em qualquer tela — a vista so escala por E.U. O duelo pela malha
// depende disso: o arremesso espelhado (x, y, vx, vy) bate nos dois.
//
//   DOIS MUNDOS (o coracao do design):
//   mundo A (castelo) — relaxacao DURA (10 passadas): a pilha de blocos
//     com ARGAMASSA fica em pe; compacta ~20% em equilibrio e os limiares
//     de tensao toleram isso — so a pancada rompe
//   mundo B (estilingue) — relaxacao FROUXA (3 passadas): o elastico
//     ESTICA de verdade (a relaxacao dura corrigiria a distensao num step
//     e a energia viraria nada). No plano da forquilha a pedra e
//     TRANSFERIDA para o mundo A com a velocidade medida (add em x-v +
//     set em x: px implicito = x-v) — e esse pacote {x, y, vx, vy} que o
//     rival espelha na malha
//
//   bloco     — grade rigida 3x2 (6 pontos + 9 vinculos): 3 pontos de
//               contato por face; pilares de 2 pontos escorregam (a
//               colisao posicional nao tem atrito)
//   pilha     — blocos empilhados no MESMO x, colados por ARGAMASSA (3
//               vinculos fracos nas colunas alinhadas; len = 2*radius =
//               equilibrio exato com a colisao). Segura o castelo parado,
//               estoura na pancada. Pilhas vizinhas a BW+4 ou mais
//   coroa     — o topo da torre: bloco OURO com argamassa (a coroa
//               pendurada balanca e bombeia energia ate a pilha derreter)
//   pedra     — cluster rigido 2x2 (granito: nunca racha); a gasta vira
//               ENTULHO no cenario
//
// Espelho de vinculos do mundo A: sLen/sMat andam parelhos com o array
// nativo (o delStick nativo faz swap-remove; nos replicamos o swap aqui,
// num unico ponto de delecao). NUNCA verletDelPoint no mundo A: o nativo
// removeria vinculos ligados fora da nossa sincronizacao — por isso a
// pedra gasta fica como entulho em vez de sair.

var P = require("celeros.physics");

// ---------------------------------------------------------------- consts --
var MAT = { INFRA: 0, MADEIRA: 1, PEDRA: 2, COROA: 3, ELAST: 4, ARG: 5, OURO: 6 };
var THRES = [Infinity, 0.26, 0.38, 0.30, Infinity, 0.14, Infinity];   // tensao p/ romper
var VALOR = [0, 10, 25, 150, 0, 5, 0];                                // pontos por vinculo

var MW = 240;                    // largura do mundo (unidades de projeto)
var MH = 280;                    // altura do mundo (cabe no 480x480 com HUD)
var CHAO = 280;                  // y do chao (bounds.maxY)
var GRAV = 0.3;                  // gravidade por step (dt=1)
var DAMP = 0.995, BOUNCE = 0.15;
var RADIUS = 1.2, ITERS = 10;    // mundo A: relaxacao dura (pilha firme)
var ITERS_B = 3;                 // mundo B: relaxacao frouxa (elastico mole)
var STEPS_POR_FRAME = 2;

var MAX_PTS = 220;               // auto-teto (nativo: 256 por mundo)

var BERCO = { x: 31, y: 192 };   // pedra armada no estilingue
var GARFO = [{ x: 24, y: 196 }, { x: 38, y: 196 }];
var PEDRA_S = 4.5;               // lado do cluster 2x2
var TRACAO_MAX = 22;             // raio maximo do puxao
var TRACAO_K = 0.55;             // v = TRACAO_K * distensao (traçao max ~12/frame)
var CORTE_X = 32;                // plano da forquilha: corta na SUBIDA (no pico do balanco v~0)
var CORTE_TIMEOUT = 90;          // steps sem cruzar: solta do mesmo jeito
var MORTO_V = 0.8;               // vel/frame abaixo disso = parando
var MORTO_FRAMES = 45;           // por 1,5 s: pedra morreu (vira entulho)
var GRACE_STEPS = 90;            // assentamento inicial sem rachadura

var BW = 16, BH = 11, BGAP = 2.4;   // bloco: largura, altura, junta (junta =
// 2*radius: o bloco nasce encostado; contato desencontrado vira empurrao
// lateral que abre o bloco — por isso colunas SEMPRE alinhadas)

// ------------------------------------------------------------- o mundo ----
var F = {
    mundo: null,                  // mundo A: castelo + pedras em voo
    mundoB: null,                 // mundo B: estilingue (garfos + pedra presa)
    sLen: [], sMat: [],           // espelho parelho dos sticks do mundo A
    blocos: [],                   // [{ pts:[6 idx], mat, vivos, coroa? }]
    blockOf: [],                  // ponto -> bloco (ou -1)
    coroaB: null, coroaCaiu: false,
    pedra: null,                  // { pts, local: 'A'|'B', estado, parados, steps }
    entulhos: [],                 // pts das pedras gastas (ficam no cenario)
    prevXY: null, prevXYB: null,  // xy do frame anterior (velocidade)
    sticksCastelo0: 1,
    steps: 0,                     // steps do mundo A desde o init (grace)
    inits: 0,
    pedras: 0,                    // pedras ainda nao gastas (a atual conta)
    rompidos: 0,
    tracao: null,                 // v analitica capturada na soltura
    lancamento: null              // dados da ultima transferencia (p/ rede)
};

function addStick(a, b, len, mat) {
    F.mundo.stick(a, b, len);
    F.sLen.push(len);
    F.sMat.push(mat);
    return F.sLen.length - 1;
}

// delecao centralizada: replica o swap-remove nativo no espelho
function delStickIdx(i) {
    var n = F.sLen.length;
    if (i < 0 || i >= n) return false;
    if (!F.mundo.delStick(i)) return false;
    F.sLen[i] = F.sLen[n - 1];
    F.sMat[i] = F.sMat[n - 1];
    F.sLen.pop();
    F.sMat.pop();
    return true;
}

// ------------------------------------------------------------- montagem --
// bloco rigido em GRADE 3x2 (6 pontos): 3 pontos de contato por face.
// Triangulacao: 4 horizontais + 3 verticais + 2 diagonais.
function addBloco(x, yBase, mat, w, h) {
    w = w || BW; h = h || BH;
    var hw = w / 2;
    var cols = [-hw, 0, hw];
    var diag = Math.sqrt(hw * hw + h * h);
    var idx = [];
    var ys = [yBase - h, yBase];      // r=0 topo, r=1 base (yBase = onde assenta)
    for (var r = 0; r < 2; r++) {
        for (var c = 0; c < 3; c++) {
            var id = F.mundo.add(x + cols[c], ys[r]);
            if (id < 0) return null;
            idx.push(id);
            F.blockOf[id] = F.blocos.length;
        }
    }
    // idx 0,1,2 = topo (esq, meio, dir); 3,4,5 = base
    addStick(idx[0], idx[1], hw, mat);
    addStick(idx[1], idx[2], hw, mat);
    addStick(idx[3], idx[4], hw, mat);
    addStick(idx[4], idx[5], hw, mat);
    addStick(idx[0], idx[3], h, mat);
    addStick(idx[1], idx[4], h, mat);
    addStick(idx[2], idx[5], h, mat);
    addStick(idx[0], idx[4], diag, mat);
    addStick(idx[2], idx[4], diag, mat);
    var bl = { pts: idx, mat: mat, vivos: 9 };
    F.blocos.push(bl);
    return bl;
}

function dist(a, b) {
    var dx = b.x - a.x, dy = b.y - a.y;
    return Math.sqrt(dx * dx + dy * dy);
}

function layoutFase1() {
    // PILHAS: blocos empilhados no MESMO x (3 pontos de contato alinhados
    // na vertical) colados por ARGAMASSA. Pilhas vizinhas a BW+4 ou mais.
    return { pilhas: [
        { x: 148, mats: [MAT.PEDRA, MAT.MADEIRA] },
        { x: 168, mats: [MAT.PEDRA, MAT.MADEIRA] },
        { x: 206, mats: [MAT.MADEIRA, MAT.PEDRA, MAT.MADEIRA], coroa: true }
    ] };
}

// pedra nova no bercо do mundo B (cluster 2x2 + elasticos ate os garfos)
function pedraNovaB() {
    var B = F.mundoB;
    var c = BERCO, s = PEDRA_S / 2;
    var off = [[-s, -s], [s, -s], [-s, s], [s, s]];
    var pts = [];
    for (var i = 0; i < 4; i++) {
        var id = B.add(c.x + off[i][0], c.y + off[i][1]);
        if (id < 0) return null;
        pts.push(id);
    }
    var d2 = PEDRA_S, dg = PEDRA_S * Math.SQRT2;
    B.stick(pts[0], pts[1], d2); B.stick(pts[2], pts[3], d2);
    B.stick(pts[0], pts[2], d2); B.stick(pts[1], pts[3], d2);
    B.stick(pts[0], pts[3], dg); B.stick(pts[1], pts[2], dg);
    // elasticos: comprimento de repouso ate os garfos (idx 0 e 1 no mundo B)
    B.stick(0, pts[0], dist(GARFO[0], { x: c.x - s, y: c.y - s }));
    B.stick(1, pts[1], dist(GARFO[1], { x: c.x + s, y: c.y - s }));
    F.prevXYB = null;
    F.pedra = { pts: pts, local: "B", estado: "pronta", parados: 0, steps: 0 };
    return F.pedra;
}

function initB() {
    if (F.mundoB) F.mundoB.free();
    F.mundoB = P.verletFast({ iterations: ITERS_B, radius: 0 });
    F.mundoB.add(GARFO[0].x, GARFO[0].y);   // idx 0 e 1 = garfos
    F.mundoB.add(GARFO[1].x, GARFO[1].y);
    F.mundoB.pin(0); F.mundoB.pin(1);
    pedraNovaB();
}

// ------------------------------------------------------------- api -------
F.init = function (layout, pedras) {
    F.inits++;
    if (F.mundo) F.mundo.free();
    F.mundo = P.verletFast({ iterations: ITERS, radius: RADIUS });
    F.sLen = []; F.sMat = [];
    F.blocos = []; F.blockOf = [];
    F.coroaB = null; F.coroaCaiu = false;
    F.rompidos = 0; F.steps = 0; F.prevXY = null; F.lancamento = null; F.tracao = null;
    F.entulhos = [];
    F.pedras = pedras || 5;

    layout = layout || layoutFase1();

    // pilhas de colunas alinhadas com argamassa
    for (var p = 0; p < layout.pilhas.length; p++) {
        var pl = layout.pilhas[p];
        var yBase = CHAO;
        var anterior = null;
        for (var n = 0; n < pl.mats.length; n++) {
            var bl = addBloco(pl.x, yBase, pl.mats[n]);
            if (anterior) {
                for (var c = 0; c < 3; c++) {
                    addStick(anterior.pts[c], bl.pts[3 + c], BGAP, MAT.ARG);
                }
            }
            anterior = bl;
            yBase -= BH + BGAP;
        }
        // coroa: o TOPO DA TORRE e um bloco OURO (mesma largura, argamassa
        // alinhada como os outros niveis — geometria estavel; coroa
        // pendulada balanca e derrete a pilha). As pontas sao desenho.
        if (pl.coroa && anterior) {
            var mini = addBloco(pl.x, yBase, MAT.OURO, BW, 6);
            mini.coroa = true;
            F.coroaB = mini;
            for (var cc = 0; cc < 3; cc++) {
                addStick(anterior.pts[cc], mini.pts[3 + cc], BGAP, MAT.ARG);
            }
        }
    }
    F.sticksCastelo0 = F.sLen.length;
    initB();
    return F;
};

// puxao do dedo (coordenadas do MUNDO). devolve false se nao havia pedra
// armada ou o dedo nao pegou
F.puxar = function (x, y) {
    if (!F.pedra || F.pedra.local !== "B" || F.pedra.estado !== "pronta") return false;
    var c = F.centroPedra();
    var dx = x - c.x, dy = y - c.y;
    if (dx * dx + dy * dy > 22 * 22) return false;   // raio folgado: a pedra armada pende (elastico mole)
    F.pedra.estado = "carregando";
    F.arrastando(x, y);
    return true;
};

F.arrastando = function (x, y) {
    if (!F.pedra || F.pedra.local !== "B" || F.pedra.estado !== "carregando") return;
    var dx = x - BERCO.x, dy = y - BERCO.y;
    var d = Math.sqrt(dx * dx + dy * dy);
    if (d > TRACAO_MAX) { dx *= TRACAO_MAX / d; dy *= TRACAO_MAX / d; }
    var cx = Math.min(BERCO.x + dx, BERCO.x + 2);   // nunca a frente do garfo
    var cy = BERCO.y + dy;
    var s = PEDRA_S / 2;
    var off = [[-s, -s], [s, -s], [-s, s], [s, s]];
    for (var i = 0; i < 4; i++) F.mundoB.set(F.pedra.pts[i], cx + off[i][0], cy + off[i][1]);
};

F.soltar = function () {
    if (!F.pedra || F.pedra.local !== "B" || F.pedra.estado !== "carregando") return;
    // velocidade do lance: ANALITICA da tracao (o verletSet do carrego
    // deixa px defasado — a v "medida" seria fase de oscilacao, nao
    // fisica; a analitica e deterministica e identica nos dois aparelhos
    var c = F.centroPedra();
    var dx = BERCO.x - c.x, dy = BERCO.y - c.y;
    var d = Math.sqrt(dx * dx + dy * dy);
    F.tracao = d > 1 ? { vx: dx / d * d * TRACAO_K, vy: dy / d * d * TRACAO_K }
                     : { vx: 0, vy: 0 };
    F.pedra.estado = "lancando";
    F.pedra.steps = 0;
};

F.centroPedra = function () {
    if (!F.pedra) return { x: BERCO.x, y: BERCO.y };
    var m = F.pedra.local === "B" ? F.mundoB : F.mundo;
    var xy = m.xy(), sx = 0, sy = 0;
    for (var i = 0; i < 4; i++) { sx += xy[F.pedra.pts[i] * 2]; sy += xy[F.pedra.pts[i] * 2 + 1]; }
    return { x: sx / 4, y: sy / 4 };
};

F.velPedra = function (xy) {
    if (!F.pedra || F.pedra.local !== "A" || !F.prevXY) return 99;
    var v = 0;
    for (var i = 0; i < 4; i++) {
        var k = F.pedra.pts[i] * 2;
        v += Math.abs(xy[k] - F.prevXY[k]) + Math.abs(xy[k + 1] - F.prevXY[k + 1]);
    }
    return v / 4;
};

// rachadura: rompe vinculos do castelo acima da tensao do material;
// devolve eventos e reconta os vinculos vivos de cada bloco
function scanTensao(xy, ev) {
    // grace: a queda do assentamento comprime os sticks no 1o impacto —
    // nao e rachadura, e o castelo nascendo
    if (F.steps < GRACE_STEPS) return;
    var st = F.mundo.sticks();
    var romper = [];
    for (var i = 0; i < F.sLen.length; i++) {
        var m = F.sMat[i];
        if (m === MAT.INFRA || m === MAT.ELAST) continue;
        var a = st[i * 2] * 2, b = st[i * 2 + 1] * 2;
        var dx = xy[b] - xy[a], dy = xy[b + 1] - xy[a + 1];
        var d = Math.sqrt(dx * dx + dy * dy), L = F.sLen[i];
        if (Math.abs(d - L) / L > THRES[m]) romper.push(i);
    }
    for (var r = romper.length - 1; r >= 0; r--) {
        var i2 = romper[r];
        var st2 = F.mundo.sticks();
        var a2 = st2[i2 * 2] * 2, b2 = st2[i2 * 2 + 1] * 2;
        var m2 = F.sMat[i2];
        delStickIdx(i2);
        F.rompidos++;
        ev.push({ t: "crack", x: (xy[a2] + xy[b2]) / 2, y: (xy[a2 + 1] + xy[b2 + 1]) / 2,
                  mat: m2, valor: VALOR[m2] });
    }
    // vinculos vivos por bloco (p/ desenhar quad inteiro ou viga a viga)
    for (var bl = 0; bl < F.blocos.length; bl++) F.blocos[bl].vivos = 0;
    var st3 = F.mundo.sticks();
    for (var s = 0; s < st3.length / 2; s++) {
        var m3 = F.sMat[s];
        if (m3 === MAT.INFRA || m3 === MAT.ELAST) continue;
        var ba = F.blockOf[st3[s * 2]];
        if (ba >= 0 && ba === F.blockOf[st3[s * 2 + 1]]) F.blocos[ba].vivos++;
    }
}

// transfere a pedra do mundo B (estilingue) para o mundo A (castelo) com
// a velocidade medida entre frames — o mesmo pacote que a malha espelha
function transferirPedra(xyB) {
    var d2 = PEDRA_S, dg = PEDRA_S * Math.SQRT2;
    var vx = F.tracao ? F.tracao.vx : 0;
    var vy = F.tracao ? F.tracao.vy : 0;
    var pts = [];
    for (var i = 0; i < 4; i++) {
        var k = F.pedra.pts[i] * 2;
        // add em (x - v) e set em x: px implicito fica x - v
        var id = F.mundo.add(xyB[k] - vx, xyB[k + 1] - vy);
        F.mundo.set(id, xyB[k], xyB[k + 1]);
        F.blockOf[id] = -1;
        pts.push(id);
    }
    F.mundo.stick(pts[0], pts[1], d2); F.mundo.stick(pts[2], pts[3], d2);
    F.mundo.stick(pts[0], pts[2], d2); F.mundo.stick(pts[1], pts[3], d2);
    F.mundo.stick(pts[0], pts[3], dg); F.mundo.stick(pts[1], pts[2], dg);
    for (var e = 0; e < 6; e++) { F.sLen.push(0); F.sMat.push(MAT.INFRA); }
    F.lancamento = { x: xyB[F.pedra.pts[0] * 2], y: xyB[F.pedra.pts[0] * 2 + 1],
                     vx: vx, vy: vy };
    F.pedra = { pts: pts, local: "A", estado: "voando", parados: 0, steps: 0 };
    return { t: "lancou", x: F.lancamento.x, y: F.lancamento.y };
}

// 1 quadre do jogo: estilingue (B) + castelo (A) + rachadura + quedas
F.step = function () {
    var ev = [];
    var optsA = { gravity: { x: 0, y: GRAV }, damp: DAMP,
                  bounds: { x: 0, y: 0, w: MW, h: CHAO }, bounce: BOUNCE };

    // --- mundo B: estilingue ---
    if (F.pedra && F.pedra.local === "B") {
        var optsB = { gravity: { x: 0, y: GRAV }, damp: DAMP, bounce: BOUNCE };
        for (var sb = 0; sb < STEPS_POR_FRAME; sb++) F.mundoB.step(1, optsB);
        var xyB = F.mundoB.xy();
        if (F.pedra.estado === "lancando") {
            F.pedra.steps += STEPS_POR_FRAME;
            var cB = F.centroPedra();
            if (cB.x >= CORTE_X || F.pedra.steps >= CORTE_TIMEOUT) {
                ev.push(transferirPedra(xyB));
            }
        }
        F.prevXYB = xyB;
    }

    // --- mundo A: castelo ---
    var velAntes = 0;
    if (F.pedra && F.pedra.local === "A" && F.prevXY) {
        velAntes = F.velPedra(F.mundo.xy());
    }
    for (var s = 0; s < STEPS_POR_FRAME; s++) F.mundo.step(1, optsA);
    F.steps += STEPS_POR_FRAME;

    var xy = F.mundo.xy();
    scanTensao(xy, ev);

    // impacto da pedra: velocidade que despencou num quadre so
    if (F.pedra && F.pedra.local === "A" && F.pedra.estado === "voando" && velAntes > 4) {
        var vel = F.velPedra(xy);
        if (vel < velAntes * 0.55) {
            var ci = F.centroPedra();
            ev.push({ t: "impacto", forca: velAntes - vel, x: ci.x, y: ci.y });
        }
    }
    // pedra parou: vira entulho, gasta a ficha
    if (F.pedra && F.pedra.local === "A" && F.pedra.estado === "voando") {
        if (F.velPedra(xy) < MORTO_V) F.pedra.parados++;
        else F.pedra.parados = 0;
        if (F.pedra.parados >= MORTO_FRAMES) {
            F.pedra.estado = "entulho";
            F.entulhos.push(F.pedra.pts);
            F.pedras--;
            ev.push({ t: "morreu" });
        }
    }
    // coroa no chao (centro do bloco ouro)
    if (!F.coroaCaiu && F.coroaB) {
        var cb = F.coroaB.pts;
        var ccy = (xy[cb[1] * 2 + 1] + xy[cb[4] * 2 + 1]) / 2;
        if (ccy >= CHAO - 12) {
            F.coroaCaiu = true;
            ev.push({ t: "coroa", x: (xy[cb[1] * 2] + xy[cb[4] * 2]) / 2, y: ccy });
        }
    }
    F.prevXY = F.mundo.xy();
    return ev;
};

F.count = function () { return F.mundo.count() + F.mundoB.count(); };
F.sticks = function () { return F.sLen.length; };
F.integridade = function () { return F.sLen.length / F.sticksCastelo0; };

// armar a proxima pedra no estilingue (restam fichas?)
F.arMar = function () {
    if (F.pedra && F.pedra.local === "B") return false;
    if (F.pedras <= 0 || F.count() + 8 > MAX_PTS) return false;
    initB();
    return true;
};

F.MAT = MAT;
F.CHAO = CHAO;
F.MW = MW;
F.MH = MH;
F.BERCO = BERCO;
F.GARFO = GARFO;

module.exports = F;
