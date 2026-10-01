// CelerOS Tic Tac Toe — jogo da velha no tema do OS.
// 1 jogador (3 niveis de bot, minimax no impossivel) ou 2 jogadores no mesmo
// aparelho. Placar persistido no FS e linha de vitoria destacada. ES5.

var T = System.theme();
var W = 240;

var SCORE_FILE = (FS.appData ? FS.appData() : "/local/") + "score.txt";
var score = { v: 0, d: 0, e: 0 };   // vitorias, derrotas, empates
(function () {
    var raw = FS.readTextFile(SCORE_FILE);
    if (!raw) return;
    var p = raw.split(",");
    if (p.length === 3) {
        score.v = parseInt(p[0], 10) || 0;
        score.d = parseInt(p[1], 10) || 0;
        score.e = parseInt(p[2], 10) || 0;
    }
})();
function saveScore() {
    FS.writeTextFile(SCORE_FILE, score.v + "," + score.d + "," + score.e);
}

var hasTone = (typeof System.playTone === "function");
function tone(hz, ms) {
    if (hasTone) { try { System.playTone([[hz, ms]]); } catch (e) {} }
}

var STATE_MENU = 0, STATE_PLAYING = 1, STATE_OVER = 2;
var state = STATE_MENU;
var board = [0, 0, 0, 0, 0, 0, 0, 0, 0];   // 0 vazio | 1 X | 2 O
var turn = 1;
var winner = 0;                             // 0 ninguem | 1 X | 2 O | 3 empate
var winLine = -1;                           // indice do trio vencedor
var twoPlayers = false;
var difficulty = 1;                         // 0 facil | 1 medio | 2 impossivel
var diffNames = ["facil", "medio", "impossivel"];

var CELL = 62, OX = 27, OY = 88;

var WINS = [
    0, 1, 2, 3, 4, 5, 6, 7, 8,
    0, 3, 6, 1, 4, 7, 2, 5, 8,
    0, 4, 8, 2, 4, 6
];

function ctext(s, cx, y, f, col, bg) {
    System.setTextColor(col, bg || T.bg);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), y, f);
}

function checkBoard(b) {
    for (var m = 0; m < 24; m += 3) {
        var v = b[WINS[m]];
        if (v !== 0 && v === b[WINS[m + 1]] && v === b[WINS[m + 2]]) {
            return { who: v, line: m / 3 };
        }
    }
    for (var j = 0; j < 9; j++) if (b[j] === 0) return null;
    return { who: 3, line: -1 };
}

function minimax(b, depth, bot) {
    if (depth === 1) System.delay(1);       // cede ao watchdog nos ramos fundos
    var r = checkBoard(b);
    if (r) {
        if (r.who === 2) return 10 - depth;
        if (r.who === 1) return depth - 10;
        return 0;
    }
    if (depth > 7) return 0;                // teto de profundidade pro ESP32
    var best = bot ? -99 : 99;
    for (var i = 0; i < 9; i++) {
        if (b[i] !== 0) continue;
        b[i] = bot ? 2 : 1;
        var s = minimax(b, depth + 1, !bot);
        b[i] = 0;
        if (bot ? s > best : s < best) best = s;
    }
    return best;
}

function botMove() {
    System.delay(250);                      // "pensando" (nao pisca: draw ja foi)
    var empty = [];
    for (var e = 0; e < 9; e++) if (board[e] === 0) empty.push(e);

    var rnd = false;
    if (difficulty === 0) rnd = true;
    else if (difficulty === 1) rnd = Math.random() > 0.6;
    var choice = -1;

    if (empty.length === 0) return;
    if (rnd) {
        choice = empty[Math.floor(Math.random() * empty.length)];
    } else if (empty.length === 9) {
        choice = Math.random() > 0.5 ? 4 : [0, 2, 6, 8][Math.floor(Math.random() * 4)];
    } else {
        var bestScore = -99;
        for (var i = 0; i < empty.length; i++) {
            var spot = empty[i];
            board[spot] = 2;
            var s = minimax(board, 0, false);
            board[spot] = 0;
            if (s > bestScore || (s === bestScore && Math.random() > 0.5)) {
                bestScore = s;
                choice = spot;
            }
        }
    }
    if (choice >= 0) board[choice] = 2;
    tone(392, 40);
}

// ---------------------------------------------------------------- desenho ----
function cellCenter(idx) {
    return {
        x: OX + (idx % 3) * CELL + CELL / 2,
        y: OY + Math.floor(idx / 3) * CELL + CELL / 2
    };
}

function drawMark(idx, ghost) {
    var c = cellCenter(idx);
    var v = board[idx];
    var col = ghost ? T.textDim : (v === 1 ? T.accent : T.warn);
    if (v === 1) {
        var r = 16;
        System.drawLine(c.x - r, c.y - r, c.x + r, c.y + r, col);
        System.drawLine(c.x - r + 1, c.y - r, c.x + r + 1, c.y + r, col);
        System.drawLine(c.x - r, c.y + r, c.x + r, c.y - r, col);
        System.drawLine(c.x - r + 1, c.y + r, c.x + r + 1, c.y - r, col);
    } else if (v === 2) {
        System.drawCircle(c.x, c.y, 17, col);
        System.drawCircle(c.x, c.y, 16, col);
    }
}

function drawBoard() {
    System.fillScreen(T.bg);

    System.fillRoundRect(0, 0, W, 26, 0, T.card);
    System.setTextColor(turn === 1 ? T.accent : T.warn, T.card);
    var vez = twoPlayers ? ("vez: " + (turn === 1 ? "X" : "O"))
                         : (turn === 1 ? "sua vez (X)" : "bot pensando...");
    System.drawString(vez, 10, 8, 2);
    System.setTextColor(T.textDim, T.card);
    var sc = "V" + score.v + " D" + score.d + " E" + score.e;
    System.drawString(sc, 228 - System.textWidth(sc, 1), 10, 1);
    System.fillRect(0, 26, W, 2, T.accent);

    System.fillRoundRect(OX - 6, OY - 6, CELL * 3 + 12, CELL * 3 + 12, 10, T.card);
    for (var k = 1; k < 3; k++) {
        System.fillRect(OX + k * CELL - 1, OY, 2, CELL * 3, T.stroke);
        System.fillRect(OX, OY + k * CELL - 1, CELL * 3, 2, T.stroke);
    }
    for (var idx = 0; idx < 9; idx++) drawMark(idx, false);

    // linha de vitoria riscando o trio
    if (winLine >= 0) {
        var base = winLine * 3;
        var a = cellCenter(WINS[base]);
        var b = cellCenter(WINS[base + 2]);
        System.drawLine(a.x, a.y, b.x, b.y, T.ok);
        System.drawLine(a.x + 1, a.y, b.x + 1, b.y, T.ok);
    }
}

function drawMenu() {
    System.fillScreen(T.bg);
    ctext("Jogo da Velha", 120, 44, 3, T.text);

    // placar
    System.fillRoundRect(28, 76, 184, 40, 10, T.card);
    System.drawRoundRect(28, 76, 184, 40, 10, T.stroke);
    System.setTextColor(T.textDim, T.card);
    System.drawString("voce", 48, 88, 1);
    System.drawString("empates", 120, 88, 1);
    System.drawString("bot", 192, 88, 1);
    System.setTextColor(T.ok, T.card);
    System.drawString(String(score.v), 48, 100, 2);
    System.setTextColor(T.text, T.card);
    System.drawString(String(score.e), 120, 100, 2);
    System.setTextColor(T.err, T.card);
    System.drawString(String(score.d), 192, 100, 2);

    // alternador de modo
    System.fillRoundRect(16, 136, 208, 34, 8, T.card);
    System.drawRoundRect(16, 136, 208, 34, 8, T.stroke);
    ctext(twoPlayers ? "2 jogadores (mesmo aparelho)" :
                       "1 jogador  -  nivel: " + diffNames[difficulty],
          120, 145, 1, T.accent, T.card);

    System.fillRoundRect(16, 186, 208, 44, 10, T.accent);
    ctext("Jogar", 120, 201, 3, T.onAccent, T.accent);

    ctext("toque no seletor para mudar o modo", 120, 252, 1, T.textDim);
    ctext("X comeca; placar fica salvo no aparelho", 120, 268, 1, T.textDim);
}

function drawOver() {
    var msg, col;
    if (winner === 3) { msg = "Empate!"; col = T.textDim; }
    else if (twoPlayers) { msg = (winner === 1 ? "X" : "O") + " venceu!"; col = T.accent; }
    else if (winner === 1) { msg = "Voce venceu!"; col = T.ok; }
    else { msg = "Bot venceu!"; col = T.err; }

    System.fillRoundRect(20, 96, 200, 128, 10, T.card);
    System.drawRoundRect(20, 96, 200, 128, 10, col);
    ctext(msg, 120, 118, 3, col, T.card);
    ctext("V " + score.v + "  E " + score.e + "  D " + score.d, 120, 150, 2, T.text, T.card);
    System.fillRoundRect(45, 176, 150, 34, 8, T.accent);
    ctext("Jogar de novo", 120, 188, 2, T.onAccent, T.accent);
}

// ------------------------------------------------------------------ fluxo ----
function finishTurn() {
    var r = checkBoard(board);
    if (!r) return false;
    winner = r.who;
    winLine = r.line;
    state = STATE_OVER;
    if (winner === 3) {
        score.e++;
        tone(330, 120);
    } else if (twoPlayers || winner === 1) {
        if (!twoPlayers) score.v++;
        tone(784, 80);
        tone(1047, 110);
    } else {
        score.d++;
        tone(196, 200);
    }
    saveScore();
    drawBoard();
    drawOver();
    return true;
}

function resetGame() {
    for (var c = 0; c < 9; c++) board[c] = 0;
    turn = 1;
    winner = 0;
    winLine = -1;
    state = STATE_PLAYING;
    drawBoard();
}

drawMenu();

var lastTouch = false;
while (true) {
    var t = System.getTouch();
    var tap = t.touched && !lastTouch;

    if (tap && t.x < 210) {                 // X da faixa fecha o app sozinho
        if (state === STATE_MENU) {
            if (t.y >= 136 && t.y <= 170) {          // seletor de modo/nivel
                if (twoPlayers) twoPlayers = false;
                else if (difficulty < 2) difficulty++;
                else { difficulty = 0; twoPlayers = true; }
                drawMenu();
            } else if (t.y >= 186 && t.y <= 230) {   // jogar
                resetGame();
            }
        } else if (state === STATE_PLAYING && (turn === 1 || twoPlayers)) {
            if (t.x >= OX && t.x <= OX + CELL * 3 && t.y >= OY && t.y <= OY + CELL * 3) {
                var col2 = Math.floor((t.x - OX) / CELL);
                var row2 = Math.floor((t.y - OY) / CELL);
                var idx = row2 * 3 + col2;
                if (board[idx] === 0) {
                    board[idx] = turn;
                    drawBoard();
                    if (!finishTurn()) {
                        tone(523, 35);
                        turn = turn === 1 ? 2 : 1;
                        drawBoard();
                    }
                }
            }
        } else if (state === STATE_OVER) {
            if (t.y >= 176 && t.y <= 210 && t.x >= 45 && t.x <= 195) {
                resetGame();
            }
        }
    }
    lastTouch = t.touched;

    if (state === STATE_PLAYING && !twoPlayers && turn === 2) {
        botMove();
        if (!finishTurn()) {
            turn = 1;
            drawBoard();
        }
    }

    System.delay(10);
}
