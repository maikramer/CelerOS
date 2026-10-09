// arena.js — a simulacao do Detona!: grade, jogador (fisica top-down da
// engine com lane assist), bombas com pavio NO BEAT, labaredas em cruz,
// correntes em dominó de meia batida, powerups e saida. Inimigos entram
// pela ia.js; aqui fica o estado e os ganchos que eles usam.
//
// A musica e o relogio: beatNow() devolve a batida FRACIONARIA sincrona
// ao musicPos (com guarda monotonia no restart do loop) ou, sem musica,
// deriva do millis — o jogo pulsa igual no emulador e no alto-falante.

var E = require("engine");
var P = require("physics");
var NV = require("niveis");
var S = System;

var COLS = 15, ROWS = 13;
var FUSE_BEATS = 4;       // pavio da bomba em batidas
var CHAIN_BEATS = 0.5;    // corrente: a proxima bomba explode meio tempo depois
var FLAME_BEATS = 0.6;    // labareda dura um pouco mais de meio tempo

var st = null;            // estado da fase
var _beatOff = 0, _beatLast = 0;

function beatLen() { return 60 / (st && st.bpm || 128); }

// batida continua (segundos em fracao de beat), sincronizada a trilha
function beatNow() {
    var b = -1;
    try { b = E.audio.beat(); } catch (e) {}
    if (b >= 0) {
        if (b < _beatLast - 1) _beatOff += _beatLast;   // loop voltou
        _beatLast = b;
        return b + _beatOff;
    }
    return (S.millis() - st.t0) / 1000 / beatLen();
}

function cellOf(px) { return Math.floor(px / st.cell); }
function centerOf(i) { return (i + 0.5) * st.cell; }

// ------------------------------------------------------------------ fase --

function iniciar(mundo, nivel, plano) {
    var tema = plano.tema;
    st = {
        mundo: mundo, nivel: nivel, tema: tema, bpm: 128, t0: S.millis(),
        cell: 0, ox: 0, oy: 0, hud: 0,
        grid: plano.grid, exit: { c: plano.exit.c, r: plano.exit.r, achada: false, aberta: false },
        powerups: plano.powerups, spawns: plano.spawns || [],
        sobrevivencia: !!plano.sobrevivencia, onda: 0, ondaAte: 4,
        world: P.world({ gravity: { x: 0, y: 0 }, maxSub: 4 }),
        tiles: null, variacao: [],
        player: null, bombs: [], flames: [], enemies: [],
        tLeft: plano.tempo, score: 0, combo: 0, comboAte: 0,
        fim: null,            // 'win' | 'dead' | 'tempo'
        onSfx: function () {}, onFx: function () {}, onFim: function () {}
    };
    // jogador: canto classico; o r real (~35% da celula) e o tilemap em
    // pixels sao fixados no layout(), quando a tela e conhecida
    st.player = st.world.add({ x: 1.5, y: 1.5, r: 0.35, bounce: 0 });
    st.stats = { bombs: 1, flame: 2, vel: 1, kick: false, remote: false,
                 shield: false, vidas: 2, inv: 0 };
    st.bases = { bombs: 1, flame: 2, vel: 1 };
    for (var i = 0; i < COLS * ROWS; i++) st.variacao.push(0);
    var r = NV.rng(mundo * 1000 + nivel * 7);
    for (var v = 0; v < st.variacao.length; v++) st.variacao[v] = r();
    _beatOff = 0; _beatLast = 0;
}

// chamado pelo main quando a tela/celula final e conhecida: fixa as
// coordenadas em pixels e remonta o tilemap no tamanho certo
function layout(cell, ox, oy) {
    st.cell = cell; st.ox = ox; st.oy = oy;
    st.player.x = centerOf(1); st.player.y = centerOf(1);
    st.player.r = cell * 0.35;
    st.world.tiles = P.tiles(st.grid, cell, cell, {
        solid: function (ch) { return ch === '#' || ch === '%' || ch === 'B'; }
    });
}

function parar() { st = null; }

function state() { return st; }

// ------------------------------------------------------------- jogador ----

// input: eixo dominante [-1|0|1, -1|0|1] (drag). Lane assist: andando no
// eixo X, o eixo Y converge sozinho para o centro da linha (e vice-versa)
// — raspar na quina conduz para a lane livre, o feel classico.
function mover(ax, ay, dt) {
    if (!st || st.fim) return;
    var b = st.player;
    var speed = (st.bases.vel + st.stats.vel * 0.5) * st.cell * 3.2;
    if (ax !== 0 && ay === 0) {
        b.vx = ax * speed;
        var cy = centerOf(cellOf(b.y));
        b.vy = Math.abs(cy - b.y) < st.cell * 0.45 ?
               Math.max(-speed, Math.min(speed, (cy - b.y) * 12)) : 0;
    } else if (ay !== 0 && ax === 0) {
        b.vy = ay * speed;
        var cx = centerOf(cellOf(b.x));
        b.vx = Math.abs(cx - b.x) < st.cell * 0.45 ?
               Math.max(-speed, Math.min(speed, (cx - b.x) * 12)) : 0;
    } else { b.vx = 0; b.vy = 0; }
    // CHUTE: andando contra uma bomba com o powerup, ela desliza
    if (st.stats.kick && (ax !== 0 || ay !== 0)) {
        var ac = cellOf(b.x + ax * (b.r + st.cell * 0.51));
        var ar = cellOf(b.y + ay * (b.r + st.cell * 0.51));
        if (em(ac, ar) === 'B') {
            for (var i = 0; i < st.bombs.length; i++) {
                var bo = st.bombs[i];
                if (Math.round(bo.fx) === ac && Math.round(bo.fy) === ar && !bo.desliza) {
                    bo.desliza = { dx: ax, dy: ay };
                    st.onSfx('chute');
                }
            }
        }
    }
}

function celulaPlayer() {
    return { c: cellOf(st.player.x), r: cellOf(st.player.y) };
}

function plantar() {
    if (!st || st.fim) return false;
    var cel = celulaPlayer();
    if (vivas() >= st.stats.bombs) return false;
    if (em(cel.c, cel.r) === 'B') return false;
    if (st.grid[cel.r][cel.c] !== '.') return false;
    return addBomba(cel.c, cel.r, FUSE_BEATS, st.stats.flame, true);
}

// bomba estrangeira (chefe joga; plantar do player passa sfx)
function addBomba(c, r, fuseBeats, range, tocaSom) {
    if (!st || em(c, r) !== '.') return false;
    st.grid[r][c] = 'B';
    st.bombs.push({ fx: c, fy: r, vai: beatNow() + fuseBeats,
                    range: range, desliza: null });
    if (tocaSom) st.onSfx('planta');
    return true;
}

// detonador remoto: a bomba mais antiga explode quase agora
function detonar() {
    if (!st || st.fim || !st.stats.remote || !st.bombs.length) return false;
    st.bombs[0].vai = Math.min(st.bombs[0].vai, beatNow() + 0.25);
    return true;
}

function vivas() { return st.bombs.length; }

function em(c, r) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return '#';
    return st.grid[r][c];
}

// ---------------------------------------------------------------- ciclo ---

function update(dt) {
    if (!st || st.fim) return;
    st.world.step(dt);
    if (st.stats.inv > 0) st.stats.inv -= dt;
    if (!st.sobrevivencia) {
        st.tLeft -= dt;
        if (st.tLeft <= 0) { matar('tempo'); return; }
    } else if (st.ondaAte > 0) {
        st.ondaAte -= dt;   // relogio da proxima onda (main cria os bichos)
    }

    var b = beatNow();
    // bombas: pavio no beat + deslizamento do chute
    for (var i = st.bombs.length - 1; i >= 0; i--) {
        var bo = st.bombs[i];
        if (bo.desliza) desliza(bo, dt);
        if (b >= bo.vai) explodir(bo);
    }
    // labaredas: expiram e mordem
    for (var f = st.flames.length - 1; f >= 0; f--) {
        var fl = st.flames[f];
        if (b >= fl.ate) { st.flames.splice(f, 1); continue; }
        var cel = celulaPlayer();
        if (fl.c === cel.c && fl.r === cel.r) ferir();
        ferirInimigosNa(fl.c, fl.r);
    }
    // inimigos (ia.js instala e roda o proprio update por aqui)
    if (st._ia) st._ia(dt, beatNow());
    // porta abre com a arena limpa (a achada + sem bichos vivos)
    st.exit.aberta = st.exit.achada && st.enemies.length === 0;

    var cel2 = celulaPlayer();
    // powerup na celula
    var key = cel2.c + ',' + cel2.r;
    if (st.powerups[key] && em(cel2.c, cel2.r) === '.') {
        pegar(st.powerups[key]);
        delete st.powerups[key];
    }
    // saida
    if (cel2.c === st.exit.c && cel2.r === st.exit.r && st.exit.aberta) {
        vencer();
    }
}

// bomba chutada: desliza celula a celula ate a proxima ocupada
function desliza(bo, dt) {
    var passos = 8 * dt;   // 8 celulas/s
    var nx = bo.fx + bo.desliza.dx * passos;
    var ny = bo.fy + bo.desliza.dy * passos;
    var ac = Math.floor(nx + 0.5 + bo.desliza.dx * 0.51);
    var ar = Math.floor(ny + 0.5 + bo.desliza.dy * 0.51);
    if (em(ac, ar) !== '.') {
        // bateu: assenta na celula atual do centro
        bo.fx = Math.round(bo.fx); bo.fy = Math.round(bo.fy);
        bo.desliza = null;
        st.grid[bo.fy][bo.fx] = 'B';
        return;
    }
    // arrasta a marca 'B' junto (corrente/visual miram a celula do centro)
    st.grid[Math.round(bo.fy)][Math.round(bo.fx)] = '.';
    bo.fx = nx; bo.fy = ny;
    st.grid[Math.round(bo.fy)][Math.round(bo.fx)] = 'B';
}

function explodir(bo) {
    // tira da lista antes (a corrente pode reentrar)
    for (var i = 0; i < st.bombs.length; i++) {
        if (st.bombs[i] === bo) { st.bombs.splice(i, 1); break; }
    }
    var bc = Math.round(bo.fx), br = Math.round(bo.fy);
    if (st.grid[br] && st.grid[br][bc] === 'B') st.grid[br][bc] = '.';
    var b = beatNow();
    var ate = b + FLAME_BEATS;
    var cells = [{ c: bc, r: br, tipo: 'nucleo' }];
    var dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
    for (var d = 0; d < 4; d++) {
        for (var passos = 1; passos <= bo.range; passos++) {
            var c = bc + dirs[d][0] * passos;
            var r = br + dirs[d][1] * passos;
            var ch = em(c, r);
            if (ch === '#') break;
            if (ch === '%') { destruirMacio(c, r); break; }
            // bomba no caminho: dominó de meia batida
            if (ch === 'B') {
                for (var k = 0; k < st.bombs.length; k++) {
                    var outra = st.bombs[k];
                    if (Math.round(outra.fx) === c && Math.round(outra.fy) === r) {
                        outra.vai = Math.min(outra.vai, b + CHAIN_BEATS);
                    }
                }
                break;
            }
            cells.push({ c: c, r: r, tipo: passos === bo.range ? 'ponta' : 'braco',
                         dx: dirs[d][0], dy: dirs[d][1] });
        }
    }
    for (var j = 0; j < cells.length; j++) {
        st.flames.push({ c: cells[j].c, r: cells[j].r, tipo: cells[j].tipo,
                         de: b, ate: ate, dx: cells[j].dx || 0, dy: cells[j].dy || 0 });
    }
    st.onFx('bum', { c: bc, r: br }, cells);
    st.onSfx('bum');
}

function destruirMacio(c, r) {
    st.grid[r][c] = '.';
    st.score += 10;
    var key = c + ',' + r;
    if (c === st.exit.c && r === st.exit.r) st.exit.achada = true;
    st.onFx('macio', { c: c, r: r }, st.powerups[key] || null);
}

function ferir() {
    if (st.stats.inv > 0 || st.fim) return;
    if (st.stats.shield) {
        st.stats.shield = false;
        st.stats.inv = 2;
        st.onSfx('escudo');
        return;
    }
    matar('dead');
}

function matar(motivo) {
    if (st.fim) return;
    st.stats.vidas--;
    st.onSfx('morte');
    st.onFx('morte', st.player);
    if (st.stats.vidas < 0) {
        st.fim = motivo;
        st.onFim(st.fim);
    } else {
        // respawn no canto com folga
        st.stats.inv = 2.5;
        st.player.x = centerOf(1); st.player.y = centerOf(1);
        st.player.vx = 0; st.player.vy = 0;
        st.tLeft = Math.max(st.tLeft, 30);
    }
}

function vencer() {
    if (st.fim) return;
    st.fim = 'win';
    st.score += Math.floor(st.tLeft) * 10 + 100;
    st.onSfx('venceu');
    st.onFim('win');
}

function pegar(kind) {
    var s = st.stats;
    if (kind === 'B') s.bombs = Math.min(s.bombs + 1, 6);
    else if (kind === 'C') { s.flame = Math.min(s.flame + 1, 8); st.bases.flame = s.flame; }
    else if (kind === 'V') s.vel = Math.min(s.vel + 1, 4);
    else if (kind === 'K') s.kick = true;
    else if (kind === 'R') s.remote = true;
    else if (kind === 'E') s.shield = true;
    else if (kind === 'X') s.vidas = Math.min(s.vidas + 1, 5);
    st.score += 50;
    st.onSfx('power');
    st.onFx('power', kind);
}

// gancho dos inimigos (ia.js instala; chamado por update)
function setIA(fn) { st._ia = fn; }

function ferirInimigosNa(c, r) {
    if (!st._ferirNa) return;
    st._ferirNa(c, r);
}

// contato com inimigo (ia chama)
function tocarPlayer() { ferir(); }

module.exports = {
    COLS: COLS, ROWS: ROWS,
    iniciar: iniciar, layout: layout, parar: parar, state: state,
    update: update, mover: mover, plantar: plantar, detonar: detonar,
    addBomba: addBomba, tocarPlayer: tocarPlayer,
    beatNow: beatNow, beatLen: beatLen, celulaPlayer: celulaPlayer,
    em: em, setIA: setIA
};
