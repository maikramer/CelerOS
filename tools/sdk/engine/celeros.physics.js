// physics.js — fisica 2D arcade para apps CelerOS (ES5 puro, zero
// dependencia). Modulo OPCIONAL da engine: funciona sozinho (mat pura, nao
// toca no System) ou junto com engine.js.
//
//   var P = require("physics");
//   var w = P.world({ gravity: {x:0, y:900},
//                     bounds: {x:0, y:0, w:240, h:320}, walls: "contain" });
//   var bola = w.add({ x:120, y:40, r:8, bounce:0.8 });    // circulo
//   var chao = w.add({ x:120, y:310, w:240, h:20, static:true }); // caixa
//   w.step(dt);   // 1x por frame
//
// Coordenadas: y cresce para BAIXO (tela); x,y do corpo e o CENTRO. Corpo
// circular tem r; caixa tem w/h. Demais campos opcionais: vx,vy (px/s),
// ax,ay (px/s2), gravity (mult, 1), bounce (0..1), friction (0..1), drag
// (0..1/s), mass (1; static vale infinito), static, sensor (so evento),
// group/mask (bitmask: colide se a.mask&b.group E b.mask&a.group),
// tiles (false ignora tilemap), drop (atravessa one-way), onCollide(me,
// other, info{nx,ny,overlap}) e grounded (apoiado). Top-down: gravity 0 +
// addTiles (P.flow faz o campo de perseguicao). Veja o guia.
//
// 1.5: P.rigid — corpo rigido NATIVO (API 33, System.rigid*): caixas que
// giram e empilham, circulos que rolam, impulsos com atrito e sono. Sem
// fallback JS (um castelo de 30 pecas nao fecha o quadro no interpretado):
// em firmware sem o binding P.rigid devolve null e o app avisa.
//
// 1.4: o SAP do step ganhou CORTE EM Y antes do resolvePair e um
// parBudget (testes por passo, default 3000; 0 = sem teto) — na rajada do
// chefe do Supernova o passo passava de 1 s no Duktape e morria de
// execution timeout; agora o excesso degrada pro proximo step.

var P = { version: '1.5.0' };

function isCircle(b) { return b.r !== undefined && b.r !== null; }
function halfW(b) { return isCircle(b) ? b.r : b.w / 2; }
function halfH(b) { return isCircle(b) ? b.r : b.h / 2; }
// atrito de chao/parede escalado no passo (deterministico e suave)
function grip(mu, sdt) { var f = 1 - mu * sdt * 10; return f < 0 ? 0 : f; }

// teste estatico de sobreposicao, qualquer par de formas
P.hit = function (a, b) {
    var ac = isCircle(a), bc = isCircle(b);
    if (ac && bc) {
        var dx = a.x - b.x, dy = a.y - b.y, r = a.r + b.r;
        return dx * dx + dy * dy <= r * r;
    }
    if (!ac && !bc) {
        return Math.abs(a.x - b.x) * 2 <= a.w + b.w &&
               Math.abs(a.y - b.y) * 2 <= a.h + b.h;
    }
    var c = ac ? a : b, q = ac ? b : a;
    var cx = Math.max(q.x - q.w / 2, Math.min(c.x, q.x + q.w / 2));
    var cy = Math.max(q.y - q.h / 2, Math.min(c.y, q.y + q.h / 2));
    var ddx = c.x - cx, ddy = c.y - cy;
    return ddx * ddx + ddy * ddy <= c.r * c.r;
};

P.world = function (opts) {
    opts = opts || {};
    var bodies = [];
    var stamp = 0;   // dedupe de onCollide: 1 tiro por par por step

    var w = {
        gravity: opts.gravity || { x: 0, y: 0 },
        bounds: opts.bounds || null,     // {x,y,w,h}
        walls: opts.walls || 'none',     // 'contain' | 'wrap' | 'none'
        maxSub: opts.maxSub || 8,
        count: 0,
        all: bodies,                     // referencia viva p/ desenhar
        tiles: null
    };

    w.add = function (b) {
        if (b.vx === undefined) b.vx = 0;
        if (b.vy === undefined) b.vy = 0;
        if (b.mass === undefined) b.mass = 1;
        if (b.gravity === undefined) b.gravity = 1;
        if (b.bounce === undefined) b.bounce = 0;
        if (b.friction === undefined) b.friction = 0;
        if (b.drag === undefined) b.drag = 0;
        if (b.group === undefined) b.group = 1;
        if (b.mask === undefined) b.mask = 0xffff;
        b.grounded = false;
        bodies.push(b);
        w.count = bodies.length;
        return b;
    };
    w.remove = function (b) {
        for (var i = 0; i < bodies.length; i++) {
            if (bodies[i] === b) {
                bodies[i] = bodies[bodies.length - 1];
                bodies.pop();
                break;
            }
        }
        w.count = bodies.length;
    };
    w.clear = function () { bodies.length = 0; w.count = 0; };
    w.addTiles = function (t) { w.tiles = t; return t; };

    // sub-passos automaticos: ninguem anda, por sub-passo, mais que metade
    // da menor dimensao — corpo rapido nao atravessa parede fina
    w.step = function (dt) {
        if (dt <= 0) return;
        var i, b;
        var minHalf = 1e9, maxDisp = 0, maxExt = 0;
        for (i = 0; i < bodies.length; i++) {
            b = bodies[i];
            var hw = halfW(b), hh = halfH(b);
            if (hw > maxExt) maxExt = hw;
            if (hh > maxExt) maxExt = hh;
            if (b.static) continue;
            var h = hw < hh ? hw : hh;
            if (h < minHalf) minHalf = h;
            var sp = Math.sqrt(b.vx * b.vx + b.vy * b.vy);
            if (sp * dt > maxDisp) maxDisp = sp * dt;
        }
        if (minHalf > 1e8) minHalf = 4;
        var sub = 1;
        if (maxDisp > minHalf * 0.5) sub = Math.ceil(maxDisp / (minHalf * 0.5));
        if (sub > w.maxSub) sub = w.maxSub;
        var sdt = dt / sub;

        stamp++;
        for (var s = 0; s < sub; s++) {
            for (i = 0; i < bodies.length; i++) {
                b = bodies[i];
                b.grounded = false;
                if (b.static) continue;
                b.vx += (w.gravity.x * b.gravity + (b.ax || 0)) * sdt;
                b.vy += (w.gravity.y * b.gravity + (b.ay || 0)) * sdt;
                if (b.drag > 0) {
                    var d = 1 - b.drag * sdt;
                    if (d < 0) d = 0;
                    b.vx *= d;
                    b.vy *= d;
                }
                b.x += b.vx * sdt;
                b.y += b.vy * sdt;
            }
            if (w.tiles) tilesCollide(w, sdt);
            if (w.bounds && w.walls !== 'none') wallsCollide(w, sdt);
            // sweep-and-prune por X (insertion sort: entre sub-passos o
            // array ja esta quase ordenado) + CORTE EM Y antes do
            // resolvePair + ORCAMENTO de testes por passo. A rajada do
            // chefe do Supernova morria de execution timeout AQUI: o SAP
            // so cortava em X (balas da mesma coluna a centenas de px em Y
            // entravam no resolve) e nao havia teto — um cluster de
            // corpos sensor levava o passo a segundos. Testado em bancada:
            // grade espacial (hash de celulas) curava o cluster mas custava
            // ~3x MAIS no caso comum (chave string por corpo e caro no
            // Duktape); SAP + corte Y + teto resolve os dois lados. O
            // budget (pairBudget, default 3000; 0 = sem teto) devolve o
            // resto dos pares ao proximo step — degrada em vez de
            // congelar.
            for (i = 1; i < bodies.length; i++) {
                var key = bodies[i];
                var j = i - 1;
                while (j >= 0 && bodies[j].x > key.x) {
                    bodies[j + 1] = bodies[j];
                    j--;
                }
                bodies[j + 1] = key;
            }
            var cut = maxExt * 2 + 1;
            var cutY = maxExt * 2 + 1;
            var budget = w.pairBudget === undefined || w.pairBudget <= 0 ?
                         Infinity : w.pairBudget;
            var tests = 0;
            for (i = 0; i < bodies.length && tests < budget; i++) {
                var ai = bodies[i];
                for (var j2 = i + 1; j2 < bodies.length; j2++) {
                    var cj = bodies[j2];
                    var dx = cj.x - ai.x;
                    if (dx > cut) break;
                    if (tests >= budget) break;
                    tests++;
                    if (ai.static && cj.static) continue;
                    if (!(ai.mask & cj.group) || !(cj.mask & ai.group)) continue;
                    if (cj.y - ai.y > cutY || ai.y - cj.y > cutY) continue;
                    resolvePair(ai, cj);
                }
            }
        }
    };

    // dispara onCollide 1x por par por step (__st guarda o stamp do tiro)
    function fire(a, b, nx, ny, over) {
        if (a.onCollide && a.__st !== stamp) {
            a.__st = stamp;
            a.onCollide(a, b, { nx: nx, ny: ny, overlap: over });
        }
        if (b.onCollide && b.__st !== stamp) {
            b.__st = stamp;
            b.onCollide(b, a, { nx: -nx, ny: -ny, overlap: over });
        }
    }

    function resolvePair(a, b) {
        var ac = isCircle(a), bc = isCircle(b);
        var nx, ny, over;   // normal de a p/ b + penetracao
        if (ac && bc) {
            var dx = b.x - a.x, dy = b.y - a.y;
            var d = Math.sqrt(dx * dx + dy * dy);
            var rr = a.r + b.r;
            if (d > rr) return;
            if (d < 1e-6) { nx = 0; ny = -1; over = rr; }
            else { nx = dx / d; ny = dy / d; over = rr - d; }
        } else if (!ac && !bc) {
            var dx2 = b.x - a.x;
            var px = a.w / 2 + b.w / 2 - Math.abs(dx2);
            if (px <= 0) return;
            var dy2 = b.y - a.y;
            var py = a.h / 2 + b.h / 2 - Math.abs(dy2);
            if (py <= 0) return;
            if (px < py) { nx = dx2 < 0 ? -1 : 1; ny = 0; over = px; }
            else { nx = 0; ny = dy2 < 0 ? -1 : 1; over = py; }
        } else {
            var cc = ac ? a : b, qq = ac ? b : a;
            var cx = Math.max(qq.x - qq.w / 2, Math.min(cc.x, qq.x + qq.w / 2));
            var cy = Math.max(qq.y - qq.h / 2, Math.min(cc.y, qq.y + qq.h / 2));
            var ddx = cc.x - cx, ddy = cc.y - cy;
            var dd = Math.sqrt(ddx * ddx + ddy * ddy);
            if (dd > cc.r) return;
            if (dd < 1e-6) {
                // centro dentro da caixa: empurra pela face mais proxima
                var lx = cc.x - qq.x, ly = cc.y - qq.y;
                if (Math.abs(lx) / (qq.w / 2) > Math.abs(ly) / (qq.h / 2)) {
                    nx = lx < 0 ? -1 : 1; ny = 0;
                    over = cc.r + qq.w / 2 - Math.abs(lx);
                } else {
                    nx = 0; ny = ly < 0 ? -1 : 1;
                    over = cc.r + qq.h / 2 - Math.abs(ly);
                }
            } else { nx = ddx / dd; ny = ddy / dd; over = cc.r - dd; }
            // normal calculada = caixa -> circulo; vira de a p/ b
            if (ac) { nx = -nx; ny = -ny; }
        }
        if (a.sensor || b.sensor) { fire(a, b, nx, ny, over); return; }

        // separacao posicional ponderada pela massa inversa
        var ima = a.static ? 0 : 1 / a.mass, imb = b.static ? 0 : 1 / b.mass;
        var imSum = ima + imb;
        if (imSum === 0) return;
        a.x -= nx * over * (ima / imSum);
        a.y -= ny * over * (ima / imSum);
        b.x += nx * over * (imb / imSum);
        b.y += ny * over * (imb / imSum);

        // impulso normal com restituicao (impacto fraco nao quica)
        var rvx = b.vx - a.vx, rvy = b.vy - a.vy;
        var vn = rvx * nx + rvy * ny;
        if (vn < 0) {
            var e = a.bounce > b.bounce ? a.bounce : b.bounce;
            if (-vn < 20) e = 0;
            var jn = -(1 + e) * vn / imSum;
            a.vx -= jn * nx * ima;
            a.vy -= jn * ny * ima;
            b.vx += jn * nx * imb;
            b.vy += jn * ny * imb;
            // atrito tangencial (Coulomb, clampado pelo impulso normal)
            var mu = (a.friction + b.friction) / 2;
            if (mu > 0) {
                var tx = -ny, ty = nx;
                var vt = (b.vx - a.vx) * tx + (b.vy - a.vy) * ty;
                var jt = -vt / imSum;
                var lim = mu * jn;
                if (jt > lim) jt = lim;
                if (jt < -lim) jt = -lim;
                a.vx -= jt * tx * ima;
                a.vy -= jt * ty * ima;
                b.vx += jt * tx * imb;
                b.vy += jt * ty * imb;
            }
        }
        if (!a.static && ny > 0.5) a.grounded = true;
        if (!b.static && ny < -0.5) b.grounded = true;
        fire(a, b, nx, ny, over);
    }

    function wallsCollide(w, sdt) {
        var B = w.bounds, mode = w.walls;
        for (var i = 0; i < bodies.length; i++) {
            var b = bodies[i];
            if (b.static) continue;
            var hw = halfW(b), hh = halfH(b);
            if (mode === 'wrap') {
                if (b.x < B.x - hw) b.x = B.x + B.w + hw;
                else if (b.x > B.x + B.w + hw) b.x = B.x - hw;
                if (b.y < B.y - hh) b.y = B.y + B.h + hh;
                else if (b.y > B.y + B.h + hh) b.y = B.y - hh;
                continue;
            }
            if (b.x - hw < B.x) { b.x = B.x + hw; if (b.vx < 0) b.vx = -b.vx * b.bounce; }
            if (b.x + hw > B.x + B.w) { b.x = B.x + B.w - hw; if (b.vx > 0) b.vx = -b.vx * b.bounce; }
            if (b.y - hh < B.y) { b.y = B.y + hh; if (b.vy < 0) b.vy = -b.vy * b.bounce; }
            if (b.y + hh > B.y + B.h) {
                b.y = B.y + B.h - hh;
                if (b.vy > 0) b.vy = -b.vy * b.bounce;
                b.grounded = true;
                if (b.friction > 0) b.vx *= grip(b.friction, sdt);
            }
        }
    }

    // resolucao por eixo (X depois Y), padrao platformer; '=' one-way so
    // colide caindo e com o pe vindo de cima (drop atravessa)
    function tilesCollide(w, sdt) {
        var t = w.tiles;
        for (var i = 0; i < bodies.length; i++) {
            var b = bodies[i];
            if (b.static || b.tiles === false) continue;
            var hw = halfW(b), hh = halfH(b);
            var r, c;
            if (b.vx !== 0) {
                var col = Math.floor((b.x + (b.vx > 0 ? hw : -hw)) / t.tw);
                var r0 = Math.floor((b.y - hh + 2) / t.th);
                var r1 = Math.floor((b.y + hh - 2) / t.th);
                for (r = r0; r <= r1; r++) {
                    if (!t.solidAt(col, r)) continue;
                    if (b.vx > 0) b.x = col * t.tw - hw;
                    else b.x = (col + 1) * t.tw + hw;
                    b.vx = -b.vx * b.bounce;
                    break;
                }
            }
            if (b.vy !== 0) {
                var row = Math.floor((b.y + (b.vy > 0 ? hh : -hh)) / t.th);
                var c0 = Math.floor((b.x - hw + 2) / t.tw);
                var c1 = Math.floor((b.x + hw - 2) / t.tw);
                for (c = c0; c <= c1; c++) {
                    if (b.vy > 0) {
                        var oneway = t.onewayAt(c, row) && !b.drop &&
                                     (b.y + hh - b.vy * sdt) <= row * t.th + 2;
                        if (!t.solidAt(c, row) && !oneway) continue;
                        b.y = row * t.th - hh;
                        var nv = -b.vy * b.bounce;
                        b.vy = nv < 30 ? 0 : nv;
                        b.grounded = true;
                        if (b.friction > 0) b.vx *= grip(b.friction, sdt);
                    } else {
                        if (!t.solidAt(c, row)) continue;   // one-way nao cobre
                        b.y = (row + 1) * t.th + hh;
                        b.vy = -b.vy * b.bounce;
                    }
                    break;
                }
            }
        }
    }

    return w;
};

// grid: array de linhas (string ou array); '#' solido e '=' one-way por
// default (troque com opts.solid(ch)/opts.oneway(ch)). Mute com arrays.
P.tiles = function (grid, tw, th, opts) {
    opts = opts || {};
    var solidFn = opts.solid || function (ch) { return ch === '#'; };
    var onewayFn = opts.oneway || function (ch) { return ch === '='; };
    var rows = grid.length;
    var cols = rows ? grid[0].length : 0;
    function ch(c, r) {
        if (r < 0 || r >= rows || c < 0) return null;
        var line = grid[r];
        if (c >= line.length) return null;
        return line[c];
    }
    return {
        tw: tw, th: th, cols: cols, rows: rows, grid: grid,
        tileAt: function (px, py) { return ch(Math.floor(px / tw), Math.floor(py / th)); },
        solidAt: function (c, r) { var v = ch(c, r); return v != null && solidFn(v); },
        onewayAt: function (c, r) { var v = ch(c, r); return v != null && onewayFn(v); },
        setTile: function (c, r, v) {
            if (r < 0 || r >= rows || c < 0 || c >= grid[r].length) return;
            grid[r][c] = v;
        }
    };
};

// campo de fluxo (BFS 4-direcoes) a partir da celula (cx, cy) — o alvo e
// tipicamente o jogador: perseguidores descem o gradiente com next() sem
// A* por corpo. Custo O(celulas) por computo; o retorno e um retrato do
// grid — recompute quando o labirinto mudar (setTile) ou de meio em meio
// segundo, nunca por frame. opts.passable(ch) troca o criterio de celula
// livre (default: nao solida do tilemap — um fantasma que atravessa bloco
// macio passa o proprio passable).
P.flow = function (t, cx, cy, opts) {
    opts = opts || {};
    var passCh = opts.passable || null;
    var rows = t.rows, cols = t.cols;
    var INF = Infinity;
    var dist = new Array(rows * cols);
    for (var i = 0; i < dist.length; i++) dist[i] = INF;

    function chAt(c, r) {
        if (r < 0 || r >= rows || c < 0 || c >= cols) return null;
        return t.grid[r][c];
    }
    function walkable(c, r) {
        if (passCh) { var v = chAt(c, r); return v != null && passCh(v); }
        return !t.solidAt(c, r);
    }
    function distAt(c, r) {
        if (r < 0 || r >= rows || c < 0 || c >= cols) return INF;
        return dist[r * cols + c];
    }

    var q = [], head = 0;
    if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) {
        dist[cy * cols + cx] = 0;
        q.push(cy * cols + cx);
    }
    while (head < q.length) {
        var at = q[head++];
        var c = at % cols;
        var r = (at - c) / cols;
        var d = dist[at] + 1;
        // ordem fixa (dir, esq, baixo, cima): next() e deterministico
        if (c + 1 < cols && dist[at + 1] === INF && walkable(c + 1, r)) { dist[at + 1] = d; q.push(at + 1); }
        if (c - 1 >= 0 && dist[at - 1] === INF && walkable(c - 1, r)) { dist[at - 1] = d; q.push(at - 1); }
        if (r + 1 < rows && dist[at + cols] === INF && walkable(c, r + 1)) { dist[at + cols] = d; q.push(at + cols); }
        if (r - 1 >= 0 && dist[at - cols] === INF && walkable(c, r - 1)) { dist[at - cols] = d; q.push(at - cols); }
    }

    return {
        cols: cols, rows: rows,
        dist: distAt,
        // vizinho um passo mais perto do alvo (null na origem, em parede
        // ou quando nao ha caminho)
        next: function (c, r) {
            var d = distAt(c, r);
            if (d === INF || d === 0) return null;
            if (distAt(c + 1, r) === d - 1) return { c: c + 1, r: r };
            if (distAt(c - 1, r) === d - 1) return { c: c - 1, r: r };
            if (distAt(c, r + 1) === d - 1) return { c: c, r: r + 1 };
            if (distAt(c, r - 1) === d - 1) return { c: c, r: r - 1 };
            return null;
        }
    };
};

// corda/pano/softbody (Verlet, pontos + hastes) — o esquema do Physics Drop
P.verlet = function (opts) {
    opts = opts || {};
    var pts = opts.points || [];
    var sticks = opts.sticks || [];
    var v = { points: pts, sticks: sticks, iterations: opts.iterations || 4 };
    var i, p;
    for (i = 0; i < pts.length; i++) {
        p = pts[i];
        if (p.px === undefined) { p.px = p.x; p.py = p.y; }
        if (p.pin === undefined) p.pin = false;
    }
    v.stickLen = function (s) {
        var A = pts[s.a], B = pts[s.b];
        s.len = Math.sqrt((A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y));
        return s.len;
    };
    for (i = 0; i < sticks.length; i++) {
        if (sticks[i].len === undefined) v.stickLen(sticks[i]);
    }
    v.stick = function (a, b, len) {
        var s = { a: a, b: b, len: len };
        if (len === undefined) v.stickLen(s);
        sticks.push(s);
        return s;
    };
    v.pin = function (i, on) { pts[i].pin = on === undefined ? true : !!on; };

    v.step = function (dt, o) {
        o = o || {};
        var g = o.gravity || { x: 0, y: 900 };
        var damp = o.damp === undefined ? 1 : o.damp;
        var dt2 = dt * dt;
        var i, p;
        for (i = 0; i < pts.length; i++) {
            p = pts[i];
            if (p.pin) { p.px = p.x; p.py = p.y; continue; }
            var vx = (p.x - p.px) * damp, vy = (p.y - p.py) * damp;
            p.px = p.x;
            p.py = p.y;
            p.x += vx + (g.x || 0) * dt2;
            p.y += vy + (g.y || 0) * dt2;
        }
        for (var k = 0; k < v.iterations; k++) {
            for (i = 0; i < sticks.length; i++) {
                var s = sticks[i];
                var A = pts[s.a], B = pts[s.b];
                var dx = B.x - A.x, dy = B.y - A.y;
                var d = Math.sqrt(dx * dx + dy * dy);
                if (d < 1e-6) continue;
                var ma = A.pin ? 0 : 1, mb = B.pin ? 0 : 1;
                var tot = ma + mb;
                if (!tot) continue;
                var f = (d - s.len) / d / tot;
                A.x += dx * f * ma;
                A.y += dy * f * ma;
                B.x -= dx * f * mb;
                B.y -= dy * f * mb;
            }
        }
        if (o.bounds) {
            var B2 = o.bounds, bb = o.bounce === undefined ? 0.5 : o.bounce;
            for (i = 0; i < pts.length; i++) {
                p = pts[i];
                if (p.pin) continue;
                if (p.x < B2.x) { var vx2 = p.x - p.px; p.x = B2.x; p.px = p.x + vx2 * bb; }
                if (p.x > B2.x + B2.w) { var vx3 = p.x - p.px; p.x = B2.x + B2.w; p.px = p.x + vx3 * bb; }
                if (p.y < B2.y) { var vy2 = p.y - p.py; p.y = B2.y; p.py = p.y + vy2 * bb; }
                if (p.y > B2.y + B2.h) { var vy3 = p.y - p.py; p.y = B2.y + B2.h; p.py = p.y + vy3 * bb; }
            }
        }
    };
    return v;
};

// P.verletFast — o mesmo verlet, acelerado: quando o firmware tem o step
// nativo (API 31, System.verletNew — JsPhysics.cpp, mundo em buffer C++ em
// float), a integracao/relaxacao/bounds roda fora do interpretador; sem o
// binding (firmware antigo, harness Node), cai para o P.verlet JS com a
// MESMA cara de uso — so que acessando por indice (xy()[2i], xy()[2i+1])
// em vez de array de objetos.
//
//   var v = P.verletFast({ iterations: 4 });
//   var a = v.add(20, 20), b = v.add(20, 80);
//   v.stick(a, b);            // len = distancia atual
//   v.pin(a);
//   v.step(1/60, { gravity: {x:0, y:900}, damp: 1,
//                  bounds: {x:0, y:0, w:240, h:320}, bounce: 0.5 });
//   var xy = v.xy();          // [xa, ya, xb, yb, ...] plano
//   var st = v.sticks();      // [a0, b0, a1, b1, ...]
//   v.set(b, 60, 40);         // move (o dedo puxando)
//   v.free();                 // devolve o mundo (Limpar/Refazer)
// 1.3.0: opts.radius liga a colisao ponto-ponto (nativo; O(n^2) em C++ —
// o fallback JS nao tem colisao), delStick/delPoint/pins completam o
// manuseio (tesoura/borracha/pinos) nos dois caminhos.
P.verletFast = function (opts) {
    opts = opts || {};
    if (typeof System === "undefined" || !System.verletNew) return verletFastJS(opts);
    var id = System.verletNew(opts.iterations || 4, opts.radius || 0);
    var v = {
        native: true, id: id,
        step: function (dt, o) {
            o = o || {};
            var g = o.gravity || { x: 0, y: 900 };
            var b = o.bounds;
            System.verletStep(id, dt, g.x || 0, g.y || 0,
                              o.damp === undefined ? 1 : o.damp,
                              b ? b.x : 0, b ? b.y : 0,
                              b ? b.x + b.w : 0, b ? b.y + b.h : 0,
                              o.bounce === undefined ? 0.5 : o.bounce);
        },
        xy: function () { return System.verletXY(id); },
        sticks: function () { return System.verletSticks(id); },
        add: function (x, y) { return System.verletAddPoint(id, x, y); },
        stick: function (a, b, len) { System.verletStick(id, a, b, len); },
        pin: function (i, on) { System.verletPin(id, i, on === undefined ? true : !!on); },
        set: function (i, x, y) { System.verletSet(id, i, x, y); },
        count: function () { return System.verletCount(id); },
        delStick: function (i) { return System.verletDelStick(id, i); },
        delPoint: function (i) { return System.verletDelPoint(id, i); },
        pins: function () { return System.verletPins(id); },
        free: function () {
            if (id > 0) System.verletFree(id);
            id = -1;
        }
    };
    return v;
};

// fallback JS: o P.verlet classico embrulhado na MESMA interface do
// verletFast (indices + arrays planos) — sem o binding nativo o app nao
// muda; so a colisao ponto-ponto nao existe (O(n^2) interpretado nao cabe)
function verletFastJS(opts) {
    var v = P.verlet(opts);
    return {
        native: false,
        step: function (dt, o) { v.step(dt, o); },
        xy: function () {
            var out = [];
            for (var i = 0; i < v.points.length; i++) out.push(v.points[i].x, v.points[i].y);
            return out;
        },
        sticks: function () {
            var out = [];
            for (var i = 0; i < v.sticks.length; i++) out.push(v.sticks[i].a, v.sticks[i].b);
            return out;
        },
        add: function (x, y) {
            v.points.push({ x: x, y: y, px: x, py: y, pin: false });
            return v.points.length - 1;
        },
        stick: function (a, b, len) { v.stick(a, b, len); },
        pin: function (i, on) { v.pin(i, on); },
        set: function (i, x, y) {
            var p = v.points[i];
            if (p) { p.x = x; p.y = y; }
        },
        count: function () { return v.points.length; },
        delStick: function (i) {
            if (i >= 0 && i < v.sticks.length) { v.sticks.splice(i, 1); return true; }
            return false;
        },
        delPoint: function (idx) {
            if (idx < 0 || idx >= v.points.length) return false;
            v.points.splice(idx, 1);
            var keep = [];
            for (var k = 0; k < v.sticks.length; k++) {
                var s = v.sticks[k];
                if (s.a === idx || s.b === idx) continue;
                keep.push({ a: s.a > idx ? s.a - 1 : s.a,
                            b: s.b > idx ? s.b - 1 : s.b, len: s.len });
            }
            v.sticks = keep;
            return true;
        },
        pins: function () {
            var out = [];
            for (var i = 0; i < v.points.length; i++) out.push(v.points[i].pin ? 1 : 0);
            return out;
        },
        free: function () { v.points = []; v.sticks = []; }
    };
};

// ------------------------------------------------------------ rigid -------
// Corpo rigido nativo (API 33): o solver roda em C++ (main/Utils/Rigid2D.h)
// e o JS conversa por INDICE estavel — nada de objeto por corpo no heap.
//
//   var w = P.rigid({ iterations: 10 });     // null sem o binding
//   var chao = w.box(160, 270, 400, 20, { static: true });
//   var tabua = w.box(200, 240, 48, 6, { density: 0.6, friction: 0.7 });
//   var pedra = w.circle(40, 200, 6, { density: 3, bounce: 0.3 });
//   w.set(pedra, 40, 200, 0, 420, -40, 0);  // teleporte + velocidade
//   w.step(dt, { gravity: { x: 0, y: 400 } });
//   var s = w.state();      // [x, y, ang, hit, rapidez, flags] por indice
//   w.x(tabua, s) ...       // leitores sobre o array do quadro
//
// Unidades livres (as do app), angulo em radianos, y para baixo. hit = o
// maior impulso de impacto do ultimo step (o app tira dano disso); flags:
// 1 vivo, 2 acordado (dormindo = parado ha 0,5 s, vira estatico ate levar
// pancada). remove() libera o indice para o proximo add.
P.rigid = function (opts) {
    opts = opts || {};
    if (typeof System === "undefined" || !System.rigidNew) return null;
    var id = System.rigidNew(opts.iterations || 10);
    if (id < 0) return null;
    var N = 6;
    function o3(o) { return o || {}; }
    return {
        id: id,
        N: N,
        box: function (x, y, w, h, o) {
            o = o3(o);
            return System.rigidBox(id, x, y, w, h, o.angle || 0,
                                   o.static ? 0 : (o.density === undefined ? 1 : o.density),
                                   o.friction === undefined ? 0.6 : o.friction,
                                   o.bounce === undefined ? 0.1 : o.bounce);
        },
        circle: function (x, y, r, o) {
            o = o3(o);
            return System.rigidCircle(id, x, y, r,
                                      o.static ? 0 : (o.density === undefined ? 1 : o.density),
                                      o.friction === undefined ? 0.6 : o.friction,
                                      o.bounce === undefined ? 0.1 : o.bounce);
        },
        remove: function (i) { return System.rigidRemove(id, i); },
        set: function (i, x, y, a, vx, vy, w) {
            return System.rigidSet(id, i, x, y, a || 0, vx || 0, vy || 0, w || 0);
        },
        impulse: function (i, jx, jy) { return System.rigidImpulse(id, i, jx, jy); },
        // devolve quantos sub-passos rodou (o anti-tunel sobe quando ha
        // corpo rapido; maxSub limita o custo)
        step: function (dt, o) {
            o = o3(o);
            var g = o.gravity || { x: 0, y: 400 };
            return System.rigidStep(id, dt, g.x || 0, g.y || 0, o.maxSub || 12);
        },
        state: function () { return System.rigidState(id); },
        count: function () { return System.rigidCount(id); },
        x: function (i, s) { return s[i * N]; },
        y: function (i, s) { return s[i * N + 1]; },
        angle: function (i, s) { return s[i * N + 2]; },
        hit: function (i, s) { return s[i * N + 3]; },
        speed: function (i, s) { return s[i * N + 4]; },
        alive: function (i, s) { return (s[i * N + 5] & 1) !== 0; },
        awake: function (i, s) { return (s[i * N + 5] & 2) !== 0; },
        free: function () {
            if (id > 0) System.rigidFree(id);
            id = -1;
        }
    };
};

module.exports = P;
