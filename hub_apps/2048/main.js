// 2048 — deslize, junte iguais, chegue no 2048. ES5 puro (Duktape), toolkit
// UI (API 22). Swipe para mover; recorde em /local/config_2048_hi.txt.

var T = System.theme();
var W = 240, H = 320;

var N = 4;
var CELL = 46, GAP = 4;
var GX = (W - (N * CELL + (N - 1) * GAP)) / 2;   // 22
var GY = 58;

var HI_FILE = "/local/config_2048_hi.txt";
var hi = 0;
var raw = FS.readTextFile(HI_FILE);
if (raw) { var p = parseInt(raw, 10); if (!isNaN(p)) hi = p; }

var board, score, won, over;
var prevBoard = null, prevScore = 0;   // 1 passo de desfazer

function idx(x, y) { return y * N + x; }

function reset() {
    board = [];
    for (var i = 0; i < N * N; i++) board.push(0);
    score = 0;
    won = false;
    over = false;
    spawn();
    spawn();
}

function emptyCells() {
    var out = [];
    for (var i = 0; i < N * N; i++) if (!board[i]) out.push(i);
    return out;
}

function spawn() {
    var e = emptyCells();
    if (!e.length) return;
    var k = e[Math.floor(Math.random() * e.length)];
    board[k] = (Math.random() < 0.9) ? 2 : 4;
}

// extrai a linha (x,y fixo, direcao) como array de valores nao nulos
function line(dir, x, y) {
    var v = [];
    for (var i = 0; i < N; i++) {
        var xx = (dir === "L") ? i : (dir === "R") ? N - 1 - i : x;
        var yy = (dir === "U") ? i : (dir === "D") ? N - 1 - i : y;
        var val = board[idx(xx, yy)];
        if (val) v.push({ x: xx, y: yy, v: val });
    }
    return v;
}

function setLine(dir, x, y, arr) {
    for (var i = 0; i < N; i++) {
        var xx = (dir === "L") ? i : (dir === "R") ? N - 1 - i : x;
        var yy = (dir === "U") ? i : (dir === "D") ? N - 1 - i : y;
        board[idx(xx, yy)] = (i < arr.length) ? arr[i] : 0;
    }
}

// move/merge em uma direcao; true se o tabuleiro mudou
function move(dir) {
    var snapB = board.slice(), snapS = score;
    var changed = false;
    for (var k = 0; k < N; k++) {
        var x = (dir === "L" || dir === "R") ? 0 : k;
        var y = (dir === "U" || dir === "D") ? 0 : k;
        var vals = [];
        var ln = line(dir, x, y);
        for (var i = 0; i < ln.length; i++) vals.push(ln[i].v);
        var merged = [];
        for (var j = 0; j < vals.length; j++) {
            if (j + 1 < vals.length && vals[j] === vals[j + 1]) {
                merged.push(vals[j] * 2);
                score += vals[j] * 2;
                if (vals[j] * 2 === 2048) won = true;
                j++;
            } else merged.push(vals[j]);
        }
        var before = line(dir, x, y);
        setLine(dir, x, y, merged);
        var after = line(dir, x, y);
        if (before.length !== after.length) changed = true;
        else {
            for (var m = 0; m < before.length; m++) {
                if (before[m].v !== after[m].v) changed = true;
            }
        }
    }
    if (changed) {
        prevBoard = snapB;
        prevScore = snapS;
    }
    return changed;
}

function undo() {
    if (!prevBoard) return;
    board = prevBoard;
    prevBoard = null;
    score = prevScore;
    over = false;
    won = false;
    UI.invalidate();
}

function canMove() {
    if (emptyCells().length) return true;
    for (var y = 0; y < N; y++) {
        for (var x = 0; x < N; x++) {
            var v = board[idx(x, y)];
            if (x + 1 < N && board[idx(x + 1, y)] === v) return true;
            if (y + 1 < N && board[idx(x, y + 1)] === v) return true;
        }
    }
    return false;
}

function saveHi() {
    if (score > hi) {
        hi = score;
        FS.writeTextFile(HI_FILE, String(hi));
    }
}

// -------------------------------------------------------------- desenho ----
function tileColors(v) {
    if (v <= 2)   return { bg: T.raised, fg: T.text };
    if (v <= 4)   return { bg: T.stroke, fg: T.text };
    if (v <= 8)   return { bg: T.accentD, fg: T.text };
    if (v <= 16)  return { bg: T.accent, fg: T.onAccent };
    if (v <= 32)  return { bg: T.warn,   fg: T.onAccent };
    if (v <= 64)  return { bg: T.err,    fg: T.onAccent };
    return { bg: T.ok, fg: T.onAccent };              // 128+
}

// Tabuleiro (desenho proprio): so no frame total; jogada chama invalidate
function drawBoard() {
    System.fillSmoothRoundRect(GX - 6, GY - 6, N * CELL + (N - 1) * GAP + 12, N * CELL + (N - 1) * GAP + 12, 10, T.card);
    for (var y = 0; y < N; y++) {
        for (var x = 0; x < N; x++) {
            var v = board[idx(x, y)];
            var px = GX + x * (CELL + GAP), py = GY + y * (CELL + GAP);
            if (!v) {
                System.fillSmoothRoundRect(px, py, CELL, CELL, 8, T.bg);
                continue;
            }
            var c = tileColors(v);
            System.fillSmoothRoundRect(px, py, CELL, CELL, 8, c.bg);
            var s = String(v);
            UI.text(s, px + CELL / 2, py + (CELL - UI.lineHeight(s.length <= 2 ? "title" : "body")) / 2,
                    { role: s.length <= 2 ? "title" : "body", align: "center", color: c.fg, bg: c.bg, id: 100 + idx(x, y) });
        }
    }
}

function drawHeader() {
    UI.text("2048", 12, 10, { role: "title" });
    UI.text("PONTOS " + score, W - 12, 8, { role: "caption", align: "right",
            color: score > 0 && score >= hi ? T.ok : T.text, id: 1 });
    UI.text("Rec " + hi, W - 12, 24, { role: "caption", align: "right", color: T.textDim, id: 2 });
}

// devolve "L" | "R" | "U" | "D" no frame em que um arrasto termina
function swipeDir() {
    var t = UI.touch();
    if (!t.released || !t.moved) return null;
    var dx = t.x - t.sx, dy = t.y - t.sy;
    if (Math.abs(dx) < 15 && Math.abs(dy) < 15) return null;
    if (Math.abs(dx) > Math.abs(dy)) return dx > 0 ? "R" : "L";
    return dy > 0 ? "D" : "U";
}

// ------------------------------------------------------------------ main ---
reset();
while (true) {
    var full = UI.begin(T.bg);
    drawHeader();
    if (full) drawBoard();
    UI.text("deslize para mover", 120, 268, { role: "caption", align: "center", color: T.textDim });
    if (UI.button("Desfazer", 12, 284, 104, 30, { style: "ghost", disabled: !prevBoard })) {
        undo();
        UI.invalidate();
    }
    if (UI.button("Novo jogo", 124, 284, 104, 30, { style: "ghost" })) {
        saveHi();
        reset();
        UI.invalidate();
    }
    var dir = swipeDir();
    UI.end();

    if (dir && move(dir)) {
        spawn();
        saveHi();
        UI.invalidate();
        if (!canMove()) {
            over = true;
            UI.begin(T.bg);  // tabuleiro final por baixo do dialogo
            drawHeader();
            drawBoard();
            UI.end();
            UI.alert("Sem movimentos!", "Pontos: " + score, "Jogar de novo");
            saveHi();
            reset();
        } else if (won) {
            UI.alert("Você chegou ao 2048!", "Pontos: " + score, "Continuar");
            won = false;
        }
    }
}
