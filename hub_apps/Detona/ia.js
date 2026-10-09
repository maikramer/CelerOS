// ia.js — os bichos do Detona!. Tres andarilhos de grade + o chefe:
//
//   balao     2.1 cel/s, vagueia (segue reto, dobra no bloqueio)
//   fantasma  1.5 cel/s, ATRAVESSA bloco macio (flow com passable proprio)
//   cacador   3.0 cel/s, desce o campo de fluxo P.flow ate o jogador
//   chefe     2x2 celulas, 8 acertos, anda devagar e JOGA bombas na
//             batida forte (a cada 8 tempos) perto do jogador
//
// O campo e recomputado ~2x/s (ou quando o labirinto muda): O(celulas),
// deterministico. Contato com o jogador = ferir(); labareda mata (o
// arena.ferirInimigosNa chama de volta pra ca).

var E = require("celeros.engine");
var arena = require("arena");
var P = require("celeros.physics");
var NV = require("niveis");
var GRID = require("celeros.grid");
var S = System;

var PERFIS = {
    balao:    { vel: 2.1, pontos: 100, w: 1 },
    fantasma: { vel: 1.5, pontos: 200, w: 1 },
    cacador:  { vel: 3.0, pontos: 300, w: 1 },
    chefe:    { vel: 0.9, pontos: 1500, w: 2 }
};

var flow = null, flowGhost = null, flowAte = 0;
var ultimoBeat = -1;
var R = NV.rng(1);   // rng da fase: decisoes deterministicas (testes/versus)

function ligar() {
    var st = arena.state();
    R = NV.rng(st.mundo * 7717 + st.nivel * 131);
    ultimoBeat = -1;
    st.enemies.length = 0;
    for (var i = 0; i < st.spawns.length; i++) {
        var sp = st.spawns[i];
        colocar(sp.kind, sp.c, sp.r);
    }
    arena.setIA(update);
    st._ferirNa = ferirNa;
    recompute(true);
}

function colocar(kind, c, r) {
    var st = arena.state();
    var pf = PERFIS[kind];
    var e = {
        kind: kind, c: c, r: r, tc: c, tr: r,     // celula atual e alvo
        fx: c + pf.w / 2, fy: r + pf.w / 2,       // centro em celulas (fracao)
        vel: pf.vel, pontos: pf.pontos, w: pf.w,
        hp: kind === 'chefe' ? 8 : 1,
        dir: { dx: 0, dy: 0 },
        andavel: false,                            // chefe: cooldown de passo
        passoAte: 0, jogaAte: 8,
        morto: false
    };
    st.enemies.push(e);
    return e;
}

function recompute(forca) {
    var st = arena.state();
    var agora = S.millis();
    if (!forca && agora < flowAte) return;
    flowAte = agora + 500;
    var cel = arena.celulaPlayer();
    var tiles = P.tiles(st.grid, 1, 1, {   // grid logico: 1 unidade por celula
        solid: function (ch) { return ch === '#' || ch === '%' || ch === 'B'; }
    });
    flow = P.flow(tiles, cel.c, cel.r);
    // o fantasma ignora macio (e bomba): so o duro bloqueia
    var tilesG = P.tiles(st.grid, 1, 1, {
        solid: function (ch) { return ch === '#'; }
    });
    flowGhost = P.flow(tilesG, cel.c, cel.r);
}

function livre(c, r, ghost) {
    var ch = arena.em(c, r);
    if (ch === '.') return true;
    if (ghost && ch === '%') return true;
    return false;
}

function decide(e) {
    recompute(false);
    var alvo = null;
    if (e.kind === 'cacador' && flow) {
        var n = flow.next(Math.floor(e.fx), Math.floor(e.fy));
        if (n && arena.em(n.c, n.r) === '.') alvo = n;
    } else if (e.kind === 'fantasma' && flowGhost) {
        var n2 = flowGhost.next(Math.floor(e.fx), Math.floor(e.fy));
        if (n2) alvo = n2;
    }
    if (!alvo) {
        // vagueio: segue reto quando da; dobra aleatorio no bloqueio;
        // so re (voltar) em beco sem saida
        var dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
        var opcoes = [], tras = null;
        for (var d = 0; d < 4; d++) {
            var nc = e.c + dirs[d][0], nr = e.r + dirs[d][1];
            var ok = e.kind === 'chefe' ? chefeCabe(nc, nr)
                                        : livre(nc, nr, e.kind === 'fantasma');
            if (!ok) continue;
            if (dirs[d][0] === -e.dir.dx && dirs[d][1] === -e.dir.dy) { tras = dirs[d]; continue; }
            opcoes.push(dirs[d]);
        }
        var escolha = null;
        var teimoso = e.kind === 'balao' || e.kind === 'chefe';
        if (opcoes.length) {
            if (teimoso && (e.dir.dx !== 0 || e.dir.dy !== 0) && R() < 0.75) {
                for (var q = 0; q < opcoes.length; q++) {
                    if (opcoes[q][0] === e.dir.dx && opcoes[q][1] === e.dir.dy) {
                        escolha = opcoes[q];
                        break;
                    }
                }
            }
            if (!escolha) escolha = opcoes[Math.floor(R() * opcoes.length)];
        } else if (tras) escolha = tras;
        if (escolha) alvo = { c: e.c + escolha[0], r: e.r + escolha[1] };
    }
    if (alvo) {
        e.tc = alvo.c; e.tr = alvo.r;
        e.dir = { dx: alvo.c - e.c, dy: alvo.r - e.r };
    }
}

function chefeCabe(c, r) {
    // 2x2: as 4 celulas dentro da arena e nenhuma em duro/macico
    for (var rr = r; rr < r + 2; rr++) {
        for (var cc = c; cc < c + 2; cc++) {
            if (arena.em(cc, rr) !== '.') return false;
        }
    }
    return true;
}

function update(dt, beat) {
    var st = arena.state();
    if (!st || st.fim) return;
    var cell = st.cell;

    // chefe joga bomba na batida forte (a cada 8 tempos)
    if (Math.floor(beat) !== ultimoBeat) {
        ultimoBeat = Math.floor(beat);
        if (ultimoBeat > 0 && ultimoBeat % 8 === 0) {
            for (var b = 0; b < st.enemies.length; b++) {
                if (st.enemies[b].kind === 'chefe' && !st.enemies[b].morto) chefeJoga(st.enemies[b]);
            }
        }
    }

    var px = st.player.x / cell - 0.5, py = st.player.y / cell - 0.5;   // em celulas
    for (var i = 0; i < st.enemies.length; i++) {
        var e = st.enemies[i];
        if (e.morto) continue;

        // caminhada: avanca ao alvo em celulas/s
        var passo = e.vel * dt;
        var dx = (e.tc + e.w / 2) - e.fx, dy = (e.tr + e.w / 2) - e.fy;
        var dist = Math.sqrt(dx * dx + dy * dy);
        if (dist <= passo) {
            e.fx = e.tc + e.w / 2; e.fy = e.tr + e.w / 2;
            e.c = e.tc; e.r = e.tr;
            decide(e);
        } else {
            e.fx += dx / dist * passo;
            e.fy += dy / dist * passo;
        }

        // contato com o jogador (centro a centro < 0.8 celula)
        var ddx = e.fx - (px + 0.5), ddy = e.fy - (py + 0.5);
        if (ddx * ddx + ddy * ddy < 0.64) arena.tocarPlayer();
    }

    // varre mortos (swap-pop)
    for (var m = st.enemies.length - 1; m >= 0; m--) {
        if (st.enemies[m].morto) st.enemies.splice(m, 1);
    }
}

function chefeJoga(e) {
    var st = arena.state();
    var cel = arena.celulaPlayer();
    // joga perto do jogador: 1-2 celulas em direcao aleatoria
    for (var t = 0; t < 12; t++) {
        var ang = Math.floor(R() * 4);
        var dist = 1 + Math.floor(R() * 2);
        var c = cel.c + [1, -1, 0, 0][ang] * dist;
        var r = cel.r + [0, 0, 1, -1][ang] * dist;
        if (arena.addBomba(c, r, 2, 2)) {
            st.onSfx('planta');
            return;
        }
    }
}

// labareda tocou a celula (c,r): bicho comum morre, chefe perde 1 hp
function ferirNa(c, r) {
    var st = arena.state();
    if (!st || st.fim) return;
    for (var i = 0; i < st.enemies.length; i++) {
        var e = st.enemies[i];
        if (e.morto) continue;
        var atingido = false;
        for (var rr = e.r; rr < e.r + e.w && !atingido; rr++)
            for (var cc = e.c; cc < e.c + e.w; cc++)
                if (cc === c && rr === r) { atingido = true; break; }
        if (!atingido) continue;
        if (e.kind === 'chefe') {
            e.hp--;
            e.flash = 0.25;
            st.onSfx('hit');
            if (e.hp <= 0) matar(e, true);
        } else {
            matar(e);
        }
    }
}

function matar(e, chefe) {
    var st = arena.state();
    e.morto = true;
    st.score += e.pontos * st.mult;
    var pf = { x: e.fx * st.cell, y: e.fy * st.cell, kind: e.kind,
               pontos: e.pontos * st.mult };
    st.onFx('bicho', pf);
    st.onSfx(chefe ? 'boomBoss' : 'bicho');
}

// SOBREVIVENCIA: onda nova (chamada pelo main na batida ou quando a arena
// esvazia): +2..4 bichos, mix piorando com o tempo, nunca perto do player
function onda() {
    var st = arena.state();
    if (!st || !st.sobrevivencia || st.fim) return st ? st.onda : 0;
    if (st.enemies.length > 4) return st.onda;
    st.onda++;
    st.ondaAte = 6;
    var n = Math.min(2 + Math.floor(st.onda / 3), 4);
    var mix = st.onda < 2 ? ['balao']
            : st.onda < 4 ? ['balao', 'fantasma']
            : ['balao', 'fantasma', 'cacador'];
    var cel = arena.celulaPlayer();
    for (var i = 0; i < n; i++) {
        var celula = GRID.celulaLivre(st.grid, R, function (c, r, ch) {
            return ch === '.' && Math.abs(c - cel.c) + Math.abs(r - cel.r) >= 6;
        });
        if (celula) colocar(mix[Math.floor(R() * mix.length)], celula.c, celula.r);
    }
    return st.onda;
}

module.exports = { ligar: ligar, colocar: colocar, update: update,
                   ferirNa: ferirNa, onda: onda };
