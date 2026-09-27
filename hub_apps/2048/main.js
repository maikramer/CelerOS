// KryonOS 2048 — deslize, junte iguais, chegue no 2048 (API 3)
// ES5 puro (Duktape). Swipe para mover; recorde em /local/config_2048_hi.txt.

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
    return changed;
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
    if (v <= 2)   return { bg: T.card,   fg: T.text };
    if (v <= 4)   return { bg: T.raised, fg: T.text };
    if (v <= 8)   return { bg: T.accentD, fg: T.text };
    if (v <= 16)  return { bg: T.accent, fg: T.onAccent };
    if (v <= 32)  return { bg: T.warn,   fg: T.onAccent };
    if (v <= 64)  return { bg: T.err,    fg: T.onAccent };
    return { bg: T.ok, fg: T.onAccent };              // 128+
}

function drawTile(x, y, v) {
    var px = GX + x * (CELL + GAP), py = GY + y * (CELL + GAP);
    if (!v) {
        System.fillRoundRect(px, py, CELL, CELL, 6, T.bg);
        return;
    }
    var c = tileColors(v);
    System.fillRoundRect(px, py, CELL, CELL, 6, c.bg);
    var s = String(v);
    var font = s.length <= 2 ? 2 : 1;
    System.setTextColor(c.fg, c.bg);
    System.drawString(s, px + (CELL - System.textWidth(s, font)) / 2,
                      py + (CELL - (font === 2 ? 16 : 8)) / 2 + 1, font);
}

function drawBoard() {
    for (var y = 0; y < N; y++) {
        for (var x = 0; x < N; x++) drawTile(x, y, board[idx(x, y)]);
    }
}

function ctext(s, cx, cy, f, col, bg) {
    System.setTextColor(col, bg);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), cy - 8, f);
}

function drawHeader() {
    System.fillRoundRect(0, 0, W, 40, 0, T.card);
    System.setTextColor(T.text, T.card);
    System.drawString("2048", 12, 12, 2);
    var right = "PONTOS " + score + "  Rec " + hi;
    System.setTextColor(T.textDim, T.card);
    System.drawString(right, W - 12 - System.textWidth(right, 1), 15, 1);
    System.fillRect(0, 40, W, 3, T.accent);
}

function drawHint() {
    System.fillRect(0, 266, W, H - 266, T.bg);
    ctext("deslize para mover", 120, 278, 1, T.textDim, T.bg);
    var label = "Novo jogo";
    var bw = 110, bx = (W - bw) / 2, by = 288;
    System.fillRoundRect(bx, by, bw, 24, 8, T.raised);
    System.drawRoundRect(bx, by, bw, 24, 8, T.stroke);
    ctext(label, 120, by + 12, 1, T.text, T.raised);
}

function drawOver() {
    System.fillRoundRect(25, 110, 190, 96, 10, T.card);
    System.drawRoundRect(25, 110, 190, 96, 10, T.err);
    ctext("Sem movimentos!", 120, 130, 2, T.warn, T.card);
    ctext("Pontos: " + score, 120, 154, 2, T.text, T.card);
    ctext("toque para jogar de novo", 120, 184, 1, T.accent, T.card);
}

function drawWin() {
    System.fillRoundRect(25, 110, 190, 96, 10, T.card);
    System.drawRoundRect(25, 110, 190, 96, 10, T.ok);
    ctext("Voce chegou ao 2048!", 120, 130, 2, T.ok, T.card);
    ctext("Pontos: " + score, 120, 154, 2, T.text, T.card);
    ctext("toque para continuar", 120, 184, 1, T.accent, T.card);
}

function drawAll() {
    System.fillScreen(T.bg);
    drawHeader();
    drawBoard();
    drawHint();
    if (won) drawWin();
    if (over) drawOver();
}

// -------------------------------------------------------------- entrada ----
var press = null, lastP = null;

// devolve null, {kind:"tap",x,y} ou {kind:"swipe",dir:"L"|"R"|"U"|"D"}
function pollGesture() {
    var t = System.getTouch();
    if (t.touched) {
        if (!press) { press = { x: t.x, y: t.y }; lastP = { x: t.x, y: t.y }; }
        else { lastP.x = t.x; lastP.y = t.y; }
        return null;
    }
    if (press) {
        var dx = lastP.x - press.x, dy = lastP.y - press.y;
        var pt = { x: lastP.x, y: lastP.y };
        press = null;
        if (Math.abs(dx) < 15 && Math.abs(dy) < 15) return { kind: "tap", x: pt.x, y: pt.y };
        if (Math.abs(dx) > Math.abs(dy)) return { kind: "swipe", dir: dx > 0 ? "R" : "L" };
        return { kind: "swipe", dir: dy > 0 ? "D" : "U" };
    }
    return null;
}

function inNewGame(x, y) {
    return y >= 288 && y <= 312 && x >= 65 && x <= 175;
}

// ------------------------------------------------------------------ main ---
reset();
drawAll();

while (true) {
    var g = pollGesture();

    if (g && g.kind === "tap") {
        if (over || inNewGame(g.x, g.y)) {
            saveHi();
            reset();
            drawAll();
        } else if (won) {
            won = false;               // fecha o aviso e segue jogando
            drawAll();
        }
    } else if (g && g.kind === "swipe" && !over && !won) {
        if (move(g.dir)) {
            spawn();
            saveHi();
            drawHeader();
            drawBoard();
            if (!canMove()) { over = true; drawOver(); }
            else if (won) drawWin();
        }
    }

    System.delay(20);
}
