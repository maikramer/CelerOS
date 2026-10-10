// CelerOS Tic Tac Toe — jogo da velha no tema do OS.
// 1 jogador (3 niveis de bot, minimax no impossivel) ou 2 jogadores no mesmo
// aparelho. Placar persistido no FS e linha de vitoria destacada. ES5,
// toolkit UI (API 22): menu, placar e fim de jogo nativos; tabuleiro com
// marcas suavizadas (drawWideLine / fillSmoothCircle).

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

// Som: System.sfx (API 32) mistura o efeito por cima da trilha e NAO trava
// o loop do jogo; firmware antigo cai no playTone (bloqueante)
var somFn = typeof System.sfx === "function" ? System.sfx :
            (typeof System.playTone === "function" ? System.playTone : null);
var hasTone = somFn !== null;
function tone(hz, ms) {
    if (hasTone) { try { somFn && somFn([[hz, ms]]); } catch (e) {} }
}

var STATE_MENU = 0, STATE_PLAYING = 1, STATE_OVER = 2;
var state = STATE_MENU;
var board = [0, 0, 0, 0, 0, 0, 0, 0, 0];   // 0 vazio | 1 X | 2 O
var turn = 1;
var winner = 0;                             // 0 ninguem | 1 X | 2 O | 3 empate
var winLine = -1;                           // indice do trio vencedor
var twoPlayers = false;
var difficulty = 1;                         // 0 facil | 1 medio | 2 impossivel
var diffNames = ["Fácil", "Médio", "Impossível"];

var CELL = 62, OX = 27, OY = 88;

var WINS = [
    0, 1, 2, 3, 4, 5, 6, 7, 8,
    0, 3, 6, 1, 4, 7, 2, 5, 8,
    0, 4, 8, 2, 4, 6
];

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

function drawMark(idx) {
    var c = cellCenter(idx);
    var v = board[idx];
    if (v === 1) {
        var r = 15;
        System.drawWideLine(c.x - r, c.y - r, c.x + r, c.y + r, 5, T.accent);
        System.drawWideLine(c.x - r, c.y + r, c.x + r, c.y - r, 5, T.accent);
    } else if (v === 2) {
        System.fillSmoothCircle(c.x, c.y, 18, T.warn);
        System.fillSmoothCircle(c.x, c.y, 13, T.card);
    }
}

// Tabuleiro (desenho proprio): so no frame total — jogada/fim de jogo
// chamam UI.invalidate()
function drawBoard(full) {
    var vez = twoPlayers ? ("Vez do " + (turn === 1 ? "X" : "O"))
                         : (turn === 1 ? "Sua vez (X)" : "Bot pensando...");
    UI.text(vez, 12, 12, { role: "title", color: turn === 1 ? T.accent : T.warn, id: 1 });
    UI.badge("V" + score.v + " D" + score.d + " E" + score.e, 168, 14);
    if (!full) return;
    System.fillSmoothRoundRect(OX - 8, OY - 8, CELL * 3 + 16, CELL * 3 + 16, 14, T.card);
    for (var k = 1; k < 3; k++) {
        System.fillRoundRect(OX + k * CELL - 2, OY + 4, 4, CELL * 3 - 8, 2, T.stroke);
        System.fillRoundRect(OX + 4, OY + k * CELL - 2, CELL * 3 - 8, 4, 2, T.stroke);
    }
    for (var idx = 0; idx < 9; idx++) drawMark(idx);
    // linha de vitoria riscando o trio
    if (winLine >= 0) {
        var base = winLine * 3;
        var a = cellCenter(WINS[base]);
        var b = cellCenter(WINS[base + 2]);
        System.drawWideLine(a.x, a.y, b.x, b.y, 6, T.ok);
    }
}

function menu() {
    UI.text("Jogo da Velha", 120, 28, { role: "title", align: "center" });
    // placar
    UI.card(16, 66, 208, 64);
    var cols = [[52, "você", score.v, T.ok], [120, "empates", score.e, T.text], [188, "bot", score.d, T.err]];
    for (var i = 0; i < 3; i++) {
        UI.text(cols[i][1], cols[i][0], 74, { role: "caption", align: "center", color: T.textDim });
        UI.text(String(cols[i][2]), cols[i][0], 92, { role: "title", align: "center", color: cols[i][3] });
    }
    UI.cardEnd();
    // modo: 1 jogador (3 niveis) ou 2 jogadores
    var m = UI.tabs(16, 144, 208, 32, ["1 jogador", "2 jogadores"], twoPlayers ? 1 : 0);
    twoPlayers = m === 1;
    if (!twoPlayers) difficulty = UI.tabs(16, 186, 208, 32, diffNames, difficulty);
    else UI.text("X e O no mesmo aparelho", 120, 194, { role: "caption", align: "center", color: T.textDim });
    if (UI.button("Jogar", 16, 236, 208, 48, { role: "title" })) resetGame();
    UI.text("X começa; o placar fica salvo no aparelho", 120, 296, { role: "caption", align: "center", color: T.textDim, w: 224 });
}

function over() {
    var msg;
    if (winner === 3) msg = "Empate!";
    else if (twoPlayers) msg = (winner === 1 ? "X" : "O") + " venceu!";
    else if (winner === 1) msg = "Você venceu!";
    else msg = "Bot venceu!";
    // o dialogo nativo vai por cima do tabuleiro escurecido
    var again = UI.confirm(msg, "V " + score.v + "  E " + score.e + "  D " + score.d,
                           { yes: "Jogar de novo", no: "Menu" });
    if (again) resetGame();
    else {
        state = STATE_MENU;
        UI.invalidate();
    }
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
    UI.invalidate();
    return true;
}

function resetGame() {
    for (var c = 0; c < 9; c++) board[c] = 0;
    turn = 1;
    winner = 0;
    winLine = -1;
    state = STATE_PLAYING;
    UI.invalidate();
}

var overShown = false;
while (true) {
    var full = UI.begin(T.bg);
    if (state === STATE_MENU) {
        menu();
    } else {
        drawBoard(full);
        var t = UI.touch();
        if (state === STATE_PLAYING && (turn === 1 || twoPlayers) && t.tap &&
            t.x >= OX && t.x < OX + CELL * 3 && t.y >= OY && t.y < OY + CELL * 3) {
            var idx = Math.floor((t.y - OY) / CELL) * 3 + Math.floor((t.x - OX) / CELL);
            if (board[idx] === 0) {
                board[idx] = turn;
                UI.invalidate();
                if (!finishTurn()) {
                    tone(523, 35);
                    turn = turn === 1 ? 2 : 1;
                }
            }
        }
    }
    UI.end();

    if (state === STATE_PLAYING && !twoPlayers && turn === 2) {
        botMove();
        if (!finishTurn()) turn = 1;
        UI.invalidate();
    }
    // fim de jogo: mostra o tabuleiro final num frame e entao o dialogo
    if (state === STATE_OVER) {
        if (overShown) { overShown = false; over(); }
        else overShown = true;
    }
}
