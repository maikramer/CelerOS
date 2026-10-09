// niveis.js — geracao de arena por seed, temas por mundo e as trilhas.
//
// Grade CLASSICA de bomberman: borda de bloco duro + pilares nas
// intersecoes pares; macios por densidade; o canto do spawn (1,1) com as
// 3 celulas livres; saida e powerups escondidos SOB macios. seed =
// mundo*100 + nivel — deterministica (testes e, no futuro, o versus
// pela malha gera a mesma arena nos dois aparelhos).

var COLS = 15, ROWS = 13;
var NIVEIS_POR_MUNDO = 8;

// xorshift32 (mesmo esquema do E.rng da engine — modulo independente)
function rng(seed) {
    var s = seed >>> 0;
    if (!s) s = 0x9e3779b9;
    return function () {
        s ^= s << 13; s >>>= 0;
        s ^= s >>> 17;
        s ^= s << 5; s >>>= 0;
        return s / 4294967296;
    };
}

// paletas do chao procedural (lajota 2 tons + junta) por mundo
var TEMAS = [
    {   // 1: Jardim de Pedra
        nome: "Jardim de Pedra",
        a: 0x1820, b: 0x1008, junta: 0x0800,
        hud: 0x0406, hudTxt: 0xC618, detalhe: 0x07E0
    },
    {   // 2: Forno
        nome: "Forno",
        a: 0x4000, b: 0x2A00, junta: 0x1000,
        hud: 0x2000, hudTxt: 0xFFE0, detalhe: 0xFD20
    },
    {   // 3: Nucleo
        nome: "Nucleo",
        a: 0x1008, b: 0x0804, junta: 0x0000,
        hud: 0x0204, hudTxt: 0xBDF7, detalhe: 0xF81F
    }
];

// trilha de batalha (128 BPM, 4 trilhas, 8 loops) — o pavio das bombas
// e quantizado nesta batida: a explosao CAI no tempo. Meio segundo por
// batida: fuse de 4 beats = 2 s classico.
var SONG_BATALHA = {
    bpm: 128, loops: 8,
    tracks: [
        {   // bateria GM: bumbo 36 / caixa 38 / chimbal 42
            drum: true, vol: 70,
            notes: [[36, 2], [42, 2], [38, 2], [42, 2],
                    [36, 2], [42, 2], [38, 2], [42, 2],
                    [36, 2], [42, 2], [38, 2], [42, 2],
                    [36, 2], [36, 2], [38, 2], [42, 2]]
        },
        {   // baixo (lam: A2/A2/C3/D3)
            wave: "tri", vol: 60,
            notes: [[45, 4], [45, 2], [48, 2], [50, 4], [45, 4],
                    [45, 4], [45, 2], [48, 2], [52, 4], [50, 4],
                    [45, 4], [45, 2], [48, 2], [50, 4], [43, 4],
                    [41, 4], [43, 2], [45, 2], [40, 4], [40, 4]]
        },
        {   // melodia (mi menor relativo, frase curta e ritmica)
            wave: "sq", vol: 45,
            notes: [[64, 2], [67, 2], [69, 4], [67, 2], [64, 2],
                    [62, 4], [64, 8],
                    [64, 2], [67, 2], [69, 4], [72, 2], [71, 2],
                    [69, 4], [67, 8],
                    [71, 2], [69, 2], [67, 4], [64, 2], [62, 2],
                    [60, 4], [62, 8]]
        },
        {   // arpejo no contratempo
            wave: "sq25", vol: 30,
            notes: [[52, 2], [55, 2], [59, 2], [55, 2],
                    [52, 2], [55, 2], [59, 2], [55, 2],
                    [50, 2], [53, 2], [57, 2], [53, 2],
                    [50, 2], [53, 2], [57, 2], [53, 2],
                    [48, 2], [52, 2], [55, 2], [52, 2],
                    [48, 2], [52, 2], [55, 2], [52, 2],
                    [50, 2], [53, 2], [57, 2], [53, 2],
                    [43, 2], [47, 2], [50, 2], [47, 2]]
        }
    ]
};

var SONG_MENU = {
    bpm: 96, loops: 8,
    tracks: [
        { drum: true, vol: 40,
          notes: [[36, 4], [42, 4], [38, 4], [42, 4]] },
        { wave: "tri", vol: 55,
          notes: [[45, 8], [52, 8], [50, 8], [48, 8],
                  [45, 8], [52, 8], [55, 8], [52, 8]] }
    ]
};

// dificuldade por nivel absoluto (1..24): densidade de macios, bichos e
// mix — os chefes fecham cada mundo (nivel % 8 == 0)
function params(mundo, nivel) {
    var abs = (mundo - 1) * NIVEIS_POR_MUNDO + nivel;
    return {
        abs: abs,
        dens: Math.min(0.50 + abs * 0.008, 0.68),
        inimigos: Math.min(2 + Math.floor(abs / 2), 8),
        tempo: 200 - Math.min(abs * 2, 60),
        chefe: nivel % NIVEIS_POR_MUNDO === 0
    };
}

// gera {grid, exit, powerups, spawns} para mundo/nivel (1-based)
function gerar(mundo, nivel) {
    var p = params(mundo, nivel);
    var r = rng(mundo * 100 + nivel);
    var grid = [], macios = [];
    for (var row = 0; row < ROWS; row++) {
        var linha = [];
        for (var col = 0; col < COLS; col++) {
            var ch = '.';
            if (row === 0 || row === ROWS - 1 || col === 0 || col === COLS - 1) ch = '#';
            else if (row % 2 === 0 && col % 2 === 0) ch = '#';
            linha.push(ch);
        }
        grid.push(linha);
    }
    // macios (pilares e borda intocaveis); spawn (1,1) respira
    for (var rr = 1; rr < ROWS - 1; rr++) {
        for (var cc = 1; cc < COLS - 1; cc++) {
            if (grid[rr][cc] !== '.') continue;
            if ((cc <= 2 && rr <= 2)) continue;   // canto do spawn
            if (r() < p.dens) { grid[rr][cc] = '%'; macios.push([cc, rr]); }
        }
    }
    // garante macios minimos p/ saida + powerups
    while (macios.length < 8) {
        var cc2 = 1 + Math.floor(r() * (COLS - 2));
        var rr2 = 1 + Math.floor(r() * (ROWS - 2));
        if (grid[rr2][cc2] === '.') {
            grid[rr2][cc2] = '%';
            macios.push([cc2, rr2]);
        }
    }
    // chefe no fim do mundo: a area 2x2 de spawn dele respira (e sai da
    // lista de macios — saida/powerup nunca escondem la)
    if (p.chefe) {
        for (var br = 1; br <= 2; br++)
            for (var bc = (COLS >> 1) - 1; bc <= (COLS >> 1); bc++)
                grid[br][bc] = '.';
        var vivos = [];
        for (var mf = 0; mf < macios.length; mf++)
            if (grid[macios[mf][1]][macios[mf][0]] === '%') vivos.push(macios[mf]);
        macios = vivos;
    }
    // saida sob um macio longe do spawn
    var exit = null;
    for (var tries = 0; tries < 60 && !exit; tries++) {
        var m = macios[Math.floor(r() * macios.length)];
        if (m[0] + m[1] > 12) exit = { c: m[0], r: m[1] };
    }
    if (!exit) exit = { c: macios[macios.length - 1][0], r: macios[macios.length - 1][1] };
    // powerups sob macios distintos da saida (kinds basicos aqui; o
    // restante do catalogo entra conforme o mundo avanca)
    var catalogo = ['B', 'C', 'V'];
    if (p.abs >= 5) catalogo.push('B', 'C');
    if (p.abs >= 9) catalogo.push('K', 'R');       // chute, detonador
    if (p.abs >= 13) catalogo.push('E', 'X');      // escudo, vida
    var powerups = {};
    var nPow = 2 + Math.floor(p.abs / 6);
    for (var i = 0; i < nPow && macios.length; i++) {
        for (var t2 = 0; t2 < 40; t2++) {
            var m2 = macios[Math.floor(r() * macios.length)];
            var key = m2[0] + ',' + m2[1];
            if (grid[m2[1]][m2[0]] !== '%') continue;
            if (m2[0] === exit.c && m2[1] === exit.r) continue;
            if (powerups[key]) continue;
            powerups[key] = catalogo[Math.floor(r() * catalogo.length)];
            break;
        }
    }
    // spawns de inimigos: celulas livres longe do canto do spawn
    var spawns = [];
    var kinds = p.abs < 3 ? ['balao']
              : p.abs < 9 ? ['balao', 'fantasma']
              : ['balao', 'fantasma', 'cacador'];
    for (var e = 0; e < p.inimigos; e++) {
        for (var t3 = 0; t3 < 80; t3++) {
            var c3 = 1 + Math.floor(r() * (COLS - 2));
            var r3 = 1 + Math.floor(r() * (ROWS - 2));
            if (grid[r3][c3] !== '.') continue;
            if (c3 + r3 < 10) continue;            // longe do player
            spawns.push({ kind: kinds[Math.floor(r() * kinds.length)], c: c3, r: r3 });
            break;
        }
    }
    if (p.chefe) spawns.push({ kind: 'chefe', c: (COLS >> 1) - 1, r: 1 });
    return { grid: grid, exit: exit, powerups: powerups, spawns: spawns,
             tempo: p.tempo, tema: TEMAS[Math.min(mundo - 1, TEMAS.length - 1)] };
}

module.exports = {
    COLS: COLS, ROWS: ROWS, NIVEIS_POR_MUNDO: NIVEIS_POR_MUNDO,
    TEMAS: TEMAS, SONG_BATALHA: SONG_BATALHA, SONG_MENU: SONG_MENU,
    params: params, gerar: gerar, rng: rng
};
