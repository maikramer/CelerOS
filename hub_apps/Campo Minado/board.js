// Campo Minado — motor do tabuleiro (modulo require, API 23).
// Puro: nao toca em System/FS/UI (o require do firmware embrulha o
// modulo sem injetar os globals do app — quem desenha e persiste e o
// main.js). Estado por celula: {mine, open, flag, n}.

module.exports = function createBoard(cols, rows, mines) {
    var g = [];            // grid linear: idx = y * cols + x
    var placed = false;    // minas vao ao chao DEPOIS da primeira jogada
    var dead = false;
    var opened = 0;

    function idx(x, y) { return y * cols + x; }
    function inside(x, y) { return x >= 0 && x < cols && y >= 0 && y < rows; }

    for (var i = 0; i < cols * rows; i++) g.push({ mine: false, open: false, flag: false, n: 0 });

    function neighbors(x, y, fn) {
        for (var dy = -1; dy <= 1; dy++) {
            for (var dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                var nx = x + dx, ny = y + dy;
                if (inside(nx, ny)) fn(nx, ny, idx(nx, ny));
            }
        }
    }

    // primeira jogada segura: a celula tocada e as vizinhas ficam livres
    function placeMines(sx, sy) {
        var forbidden = {};
        forbidden[idx(sx, sy)] = true;
        neighbors(sx, sy, function (nx, ny, ni) { forbidden[ni] = true; });
        var free = [];
        for (var i = 0; i < g.length; i++) if (!forbidden[i]) free.push(i);
        // embaralha e ocupa as primeiras `mines` (Fisher-Yates parcial)
        var need = Math.min(mines, free.length);
        for (var m = 0; m < need; m++) {
            var j = m + Math.floor(Math.random() * (free.length - m));
            var t = free[m]; free[m] = free[j]; free[j] = t;
            g[free[m]].mine = true;
        }
        for (var k = 0; k < g.length; k++) {
            if (g[k].mine) continue;
            var c = 0;
            (function (x, y) {
                neighbors(x, y, function (nx, ny, ni) { if (g[ni].mine) c++; });
            })(k % cols, Math.floor(k / cols));
            g[k].n = c;
        }
        placed = true;
    }

    // abre a celula e cascataia as vizinhas zeradas; devolve as abertas
    // nesta jogada [{x, y, n}]
    function flood(x, y) {
        var out = [];
        var stack = [idx(x, y)];
        while (stack.length) {
            var i = stack.pop();
            var c = g[i];
            if (c.open || c.flag || c.mine) continue;
            c.open = true;
            opened++;
            var cx = i % cols, cy = Math.floor(i / cols);
            out.push({ x: cx, y: cy, n: c.n });
            if (c.n === 0) {
                neighbors(cx, cy, function (nx, ny, ni) {
                    if (!g[ni].open && !g[ni].flag && !g[ni].mine) stack.push(ni);
                });
            }
        }
        return out;
    }

    return {
        cols: cols,
        rows: rows,
        mines: mines,

        // revela (x, y): devolve {hit, opened} — hit=true pisou numa mina
        reveal: function (x, y) {
            if (dead || !inside(x, y)) return { hit: false, opened: [] };
            var c = g[idx(x, y)];
            if (c.flag || c.open) return { hit: false, opened: [] };
            if (!placed) placeMines(x, y);
            if (c.mine) {
                dead = true;
                // derrota revela todas as minas (classico)
                for (var k = 0; k < g.length; k++) if (g[k].mine) g[k].open = true;
                return { hit: true, opened: [{ x: x, y: y, n: -1 }] };
            }
            return { hit: false, opened: flood(x, y) };
        },

        // toque em celula ABERTA com numero: se as bandeiras em volta
        // batem, abre o resto dos vizinhos (atalho classico)
        chord: function (x, y) {
            if (dead || !inside(x, y)) return { hit: false, opened: [] };
            var c = g[idx(x, y)];
            if (!c.open || c.n === 0) return { hit: false, opened: [] };
            var fl = 0;
            neighbors(x, y, function (nx, ny, ni) { if (g[ni].flag) fl++; });
            if (fl !== c.n) return { hit: false, opened: [] };
            var out = [], boom = false;
            neighbors(x, y, function (nx, ny, ni) {
                var cc = g[ni];
                if (cc.flag || cc.open) return;
                if (cc.mine) { boom = true; return; }
                out = out.concat(flood(nx, ny));
            });
            if (boom) {
                dead = true;
                for (var k = 0; k < g.length; k++) if (g[k].mine) g[k].open = true;
            }
            return { hit: boom, opened: out };
        },

        toggleFlag: function (x, y) {
            if (dead || !inside(x, y)) return false;
            var c = g[idx(x, y)];
            if (c.open) return false;
            c.flag = !c.flag;
            return c.flag;
        },

        cell: function (x, y) { return inside(x, y) ? g[idx(x, y)] : null; },
        lost: function () { return dead; },
        won: function () { return placed && !dead && opened === cols * rows - mines; },
        flagsPlaced: function () {
            var f = 0;
            for (var i = 0; i < g.length; i++) if (g[i].flag) f++;
            return f;
        },
        // minas restantes = total - bandeiras (pode negar se bandeirar demais)
        remaining: function () { return mines - this.flagsPlaced(); },

        // pos-derrota: todas as minas (a UI desenha com a celula aberta)
        eachMine: function (fn) {
            for (var i = 0; i < g.length; i++) {
                if (g[i].mine) fn(i % cols, Math.floor(i / cols), g[i].open);
            }
        }
    };
};
