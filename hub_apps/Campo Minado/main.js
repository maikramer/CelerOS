// Campo Minado — classico completo no toolkit UI (API 22) e primeiro app
// da loja multi-arquivo (API 23): o motor do tabuleiro vive em board.js
// via require. Duas dificuldades (a primeira jogada sempre abre area
// limpa), bandeiras, atalho de acorde (toque em numero com as bandeiras
// certas abre os vizinhos), cronometro e melhor tempo por dificuldade
// salvos no appData. Sons playTone.
//
// O tabuleiro e incremental (celula a celula); em frame cheio do toolkit
// (dialogo fechou, botao voltou) o UI.begin faz fillScreen — por isso o
// retorno de begin TRUE redesenha o campo todo. O cabecalho nunca pinta
// fundo em frame parcial: os textos UI.text se auto-limpam com bg.

var T = System.theme();
var W = 240, H = 320;

var MODES = [
    { name: "facil", cols: 9,  rows: 11, mines: 10, cell: 22 },
    { name: "duro",  cols: 12, rows: 14, mines: 28, cell: 18 }
];
var modeIdx = 0;

var createBoard = require("board");

var DATA = (FS.appData ? FS.appData() : "/local/");
var hasTone = (typeof System.playTone === "function");
function tone(hz, ms) {
    if (hasTone) { try { System.playTone([[hz, ms]]); } catch (e) {} }
}
function tune(notes) {
    if (hasTone) { try { System.playTone(notes); } catch (e) {} }
}

// cores dos numeros por perigo (tema do sistema)
var NUMCOL = [0, T.accent, T.ok, T.err, T.warn, T.warn, T.text, T.text, T.textDim];

// ---------------------------------------------------------------- estado ---
var state = "menu";         // menu | play | over
var board = null;
var gridX = 0, gridY = 30, CELL = 22;
var flagMode = false;
var startMs = 0, started = false, elapsedMs = 0, lastClock = 0;
var won = false;

function bestFile() { return DATA + "best_" + MODES[modeIdx].name + ".txt"; }
function bestTime() {
    var r = FS.readTextFile(bestFile());
    if (!r) return 0;
    var v = parseInt(r, 10);
    return isNaN(v) ? 0 : v;
}
function saveBest(ms) {
    var b = bestTime();
    if (!b || ms < b) FS.writeTextFile(bestFile(), String(ms));
}

function newGame() {
    var m = MODES[modeIdx];
    CELL = m.cell;
    board = createBoard(m.cols, m.rows, m.mines);
    gridX = Math.floor((W - m.cols * CELL) / 2);
    flagMode = false;
    started = false;
    elapsedMs = 0;
    lastClock = 0;
    won = false;
}

function fmtTime(ms) {
    var s = Math.floor(ms / 1000);
    var m = Math.floor(s / 60);
    s = s % 60;
    function two(n) { return (n < 10 ? "0" : "") + n; }
    return two(m) + ":" + two(s);
}

// ---------------------------------------------------------------- desenho ---
var wellCol = System.mixColor(T.bg, T.card, 60);

function drawCell(x, y) {
    var c = board.cell(x, y);
    if (!c) return;
    var px = gridX + x * CELL, py = gridY + y * CELL;
    if (!c.open) {
        System.fillRoundRect(px + 1, py + 1, CELL - 2, CELL - 2, 4, T.raised);
        System.drawRoundRect(px + 1, py + 1, CELL - 2, CELL - 2, 4, T.stroke);
        if (c.flag) {
            var cx = px + CELL / 2, cy = py + CELL / 2;
            System.fillTriangle(cx - 2, cy - CELL / 3, cx + CELL / 3, cy - 1, cx - 2, cy + CELL / 4, T.err);
            System.drawFastVLine(Math.floor(cx) - 2, Math.floor(cy - CELL / 3), Math.floor(CELL * 0.6), T.text);
        }
        return;
    }
    System.fillRect(px + 1, py + 1, CELL - 2, CELL - 2, wellCol);
    if (c.mine) {
        System.fillSmoothCircle(px + CELL / 2, py + CELL / 2, (CELL - 6) / 2, T.err);
        System.fillSmoothCircle(px + CELL / 2 - 2, py + CELL / 2 - 2, 2, T.text);
    } else if (c.n > 0) {
        System.setTextColor(NUMCOL[c.n], wellCol);
        System.setTextDatum(4);           // centro
        System.drawString(String(c.n), Math.floor(px + CELL / 2), Math.floor(py + CELL / 2), 2);
        System.setTextDatum(0);
    }
}

function drawAll() {
    var m = MODES[modeIdx];
    System.fillRect(0, 26, W, H - 26, T.bg);
    for (var y = 0; y < m.rows; y++) {
        for (var x = 0; x < m.cols; x++) drawCell(x, y);
    }
}

// cabecalho: fundo so em frame CHEIO (quando os textos redesenham juntos);
// em frame parcial cada texto limpa a propria caixa com bg
function drawHeader(full) {
    if (full) {
        System.fillRect(0, 0, W, 26, T.card);
        System.drawFastHLine(0, 25, W, T.stroke);
    }
    var rem = board.remaining();
    UI.text((rem > 0 ? "" : "+") + Math.abs(rem), 10, 3, { role: "title", bg: T.card, id: 1,
            color: rem === 0 ? T.ok : T.text });
    UI.text("minas", 12 + UI.measure(String(rem > 0 ? rem : "+" + Math.abs(rem), "title"), "title") + 6, 8,
            { role: "caption", color: T.textDim, bg: T.card, id: 4 });
    UI.text(fmtTime(elapsedMs), W - 10, 7, { role: "caption", align: "right", bg: T.card, color: T.textDim, id: 2 });
    var bt = bestTime();
    UI.text(bt ? "melhor " + fmtTime(bt) : "", 118, 7, { role: "caption", align: "right", bg: T.card,
            color: T.textDim, id: 3 });
}

// ---------------------------------------------------------------- jogada ---
function finish(win) {
    won = win;
    if (win) {
        saveBest(elapsedMs);
        tune([[660, 90], [880, 90], [1320, 160]]);
    } else {
        drawAll();                 // derrota revela as minas (board abriu)
        tone(120, 400);
    }
    state = "over";
}

function play(x, y) {
    if (board.lost() || board.won()) return;
    if (!started) { started = true; startMs = System.millis(); }

    if (flagMode) {
        if (board.toggleFlag(x, y)) tone(520, 30);
        else tone(300, 20);
        drawCell(x, y);
        drawHeader(false);
        return;
    }
    var c = board.cell(x, y);
    var r = (c && c.open) ? board.chord(x, y) : board.reveal(x, y);
    for (var i = 0; i < r.opened.length; i++) {
        drawCell(r.opened[i].x, r.opened[i].y);
    }
    if (r.hit) { finish(false); return; }
    tone(700, 14);
    if (board.won()) { finish(true); return; }
    drawHeader(false);
}

// ------------------------------------------------------------------ main ---
newGame();
while (true) {
    if (state === "menu") {
        UI.begin(T.bg);
        UI.text("Campo Minado", W / 2, 56, { role: "display", align: "center" });
        UI.text("dificuldade", 16, 112, { role: "title", color: T.textDim });
        modeIdx = UI.tabs(16, 134, W - 32, 36, ["fácil 9x11", "duro 12x14"], modeIdx);
        var b = bestTime();
        UI.text(b ? "melhor tempo: " + fmtTime(b) : "sem tempo nesta dificuldade",
                W / 2, 188, { role: "caption", align: "center", color: T.textDim, id: 5 });
        if (UI.button("Jogar", 40, 214, 160, 48)) { newGame(); drawAll(); drawHeader(true); state = "play"; }
        UI.text("toque abre · bandeira marca · numero com bandeiras certas abre vizinhos",
                W / 2, 282, { role: "caption", align: "center", color: T.textDim, w: W - 24, lines: 2 });
        UI.end();
        System.delay(10);
        continue;
    }

    if (state === "over") {
        var bt2 = bestTime();
        var again = UI.confirm(won ? "Venceu!" : "Boom!",
                               (won ? "Tempo " + fmtTime(elapsedMs) +
                                       (elapsedMs <= bt2 ? "  ·  novo melhor tempo!" : "")
                                    : "As minas venceram esta rodada."),
                               { yes: "De novo", no: "Menu" });
        if (again) { newGame(); drawAll(); drawHeader(true); state = "play"; }
        else { state = "menu"; UI.invalidate(); }
        continue;
    }

    // ------------------------------------------------------------ play ---
    var full = UI.begin(T.bg);
    if (full) { drawAll(); drawHeader(true); }
    var tc = UI.touch();

    if (started) {
        elapsedMs = System.millis() - startMs;
        if (elapsedMs - lastClock >= 500) { lastClock = elapsedMs; drawHeader(false); }
    }

    // celula tocada (os botoes da barra ficam fora do grid e consomem o
    // proprio tap; aqui sobra so o toque no campo)
    if (tc.tap && tc.x >= gridX && tc.y >= gridY) {
        var gx = Math.floor((tc.x - gridX) / CELL);
        var gy = Math.floor((tc.y - gridY) / CELL);
        if (board.cell(gx, gy)) play(gx, gy);
    }
    if (state !== "play") { UI.end(); continue; }

    if (UI.button(flagMode ? "Bandeira ON" : "Modo cavar", 8, 288, 116, 26,
                  { style: flagMode ? "primary" : "ghost" })) {
        flagMode = !flagMode;
    }
    if (UI.button("Novo", 124, 288, 108, 26, { style: "ghost" })) {
        newGame();
        drawAll();
        drawHeader(true);
        UI.invalidate();
    }
    UI.end(30);
}
