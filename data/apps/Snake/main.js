// CelerOS Snake — classico da cobrinha (API 3)
// ES5 puro (Duktape). Swipe do dedo para virar; a cobra acelera a cada
// fruta. Recorde persistido em /local/config_snake_hi.txt. Desenho por
// celulas alteradas (sem sprite): zero flicker e barato para o heap.

var T = System.theme();
var W = 240, H = 320;

var CELL = 12;
var COLS = 20;                  // 240 px
var ROWS = 23;                  // 276 px
var FX = 0;                     // x fisico do campo
var FY = 32;                    // y do campo (sob o placar fino)
var FW = COLS * CELL;
var FH = ROWS * CELL;
var HI_FILE = "/local/config_snake_hi.txt";

var hi = 0;
var raw = FS.readTextFile(HI_FILE);
if (raw) {
    var p = parseInt(raw, 10);
    if (!isNaN(p)) hi = p;
}

// ------------------------------------------------------------ estado ------
var snake, dir, nextDir, food, score, interval, state, growPending;
var newRecord = false;

function reset() {
    snake = [{ x: 8, y: 11 }, { x: 7, y: 11 }, { x: 6, y: 11 }];
    dir = { dx: 1, dy: 0 };
    nextDir = null;
    score = 0;
    interval = 220;
    growPending = 0;
    newRecord = false;
    state = "count";
    spawnFood();
    drawBoard();
    drawHeader();
    drawFood();
}

function countdown() {
    for (var n = 3; n >= 1; n--) {
        ctext(String(n), 120, 140, 4, T.accent, T.bg);
        System.delay(450);
    }
    // o numero ficaria pintado no campo (redesenho e por celula): limpa
    System.fillRect(FX, FY, FW, FH, T.bg);
    drawBoard();
    drawFood();
    state = "play";
}

function onSnake(x, y) {
    for (var i = 0; i < snake.length; i++) {
        if (snake[i].x === x && snake[i].y === y) return true;
    }
    return false;
}

function spawnFood() {
    var tries = 0;
    while (tries < 300) {
        var x = Math.floor(Math.random() * COLS);
        var y = Math.floor(Math.random() * ROWS);
        if (!onSnake(x, y)) { food = { x: x, y: y }; return; }
        tries++;
    }
    // campo quase cheio: primeira celula livre
    for (var j = 0; j < ROWS; j++) {
        for (var i2 = 0; i2 < COLS; i2++) {
            if (!onSnake(i2, j)) { food = { x: i2, y: j }; return; }
        }
    }
    food = null;  // venceu de verdade
}

// ------------------------------------------------------------- desenho ----
function cellRect(x, y) {
    return { x: FX + x * CELL + 1, y: FY + y * CELL + 1, w: CELL - 2, h: CELL - 2 };
}

function drawCell(x, y, col) {
    var r = cellRect(x, y);
    System.fillRect(r.x, r.y, r.w, r.h, col);
}

function drawBoard() {
    System.fillRect(FX, FY, FW, FH, T.bg);
    System.drawRoundRect(FX, FY, FW, FH, 2, T.stroke);
    for (var i = 0; i < snake.length; i++) {
        drawCell(snake[i].x, snake[i].y, i === 0 ? T.ok : T.accent);
    }
}

function drawFood() {
    if (!food) return;
    var r = cellRect(food.x, food.y);
    System.fillCircle(r.x + r.w / 2, r.y + r.h / 2, (CELL - 4) / 2, T.err);
}

function drawHeader() {
    // o nome vive na faixa do sistema (retratil); aqui e so o placar
    System.fillRoundRect(0, 0, W, 26, 0, T.card);
    System.setTextColor(T.text, T.card);
    System.drawString("Pontos " + score, 10, 8, 2);
    var right = "Rec " + hi;
    var rx = W - 12 - System.textWidth(right, 1);
    // passou o recorde em jogo: numero fica verde
    System.setTextColor(score > 0 && score >= hi && newRecord ? T.ok : T.textDim, T.card);
    System.drawString(right, rx, 10, 1);
    System.fillRect(0, 26, W, 2, T.accent);
}

function ctext(s, cx, cy, f, col, bg) {
    System.setTextColor(col, bg);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), cy - 8, f);
}

function drawGameOver(win) {
    System.fillRoundRect(25, 105, 190, 116, 10, T.card);
    System.drawRoundRect(25, 105, 190, 116, 10, T.accent);
    ctext(win ? "Você venceu!" : "Fim de jogo", 120, 126, 2, T.warn, T.card);
    ctext("Pontos: " + score, 120, 150, 2, T.text, T.card);
    if (newRecord) {
        ctext("Novo recorde!", 120, 170, 1, T.ok, T.card);
    } else {
        ctext("Recorde: " + hi, 120, 170, 1, T.textDim, T.card);
    }
    ctext("toque para jogar de novo", 120, 202, 1, T.accent, T.card);
}

function saveHi() {
    if (score > hi) {
        hi = score;
        newRecord = true;
        FS.writeTextFile(HI_FILE, String(hi));
    }
}

// ---------------------------------------------------------------- passo ---
function step() {
    if (nextDir) {
        // proibe reversao de 180 graus
        if (!(nextDir.dx === -dir.dx && nextDir.dy === -dir.dy)) dir = nextDir;
        nextDir = null;
    }
    var head = { x: snake[0].x + dir.dx, y: snake[0].y + dir.dy };

    if (head.x < 0 || head.x >= COLS || head.y < 0 || head.y >= ROWS) return die();
    // a cauda anda junto: colidir com o corpo e valido (a cauda atual sai
    // exceto se estiver crescendo — checagem simples contra o corpo inteiro)
    if (onSnake(head.x, head.y)) {
        var tail = snake[snake.length - 1];
        if (!(growPending === 0 && tail.x === head.x && tail.y === head.y)) return die();
    }

    snake.unshift(head);
    drawCell(head.x, head.y, T.ok);
    if (snake.length > 1) drawCell(snake[1].x, snake[1].y, T.accent);

    if (food && head.x === food.x && head.y === food.y) {
        score++;
        if (score > hi && !newRecord) {
            newRecord = true;
            drawHeader();
        }
        growPending += 2;
        interval = Math.max(90, 220 - score * 6);
        drawHeader();
        if (score >= COLS * ROWS) { saveHi(); state = "over"; win = true; drawGameOver(true); return; }
        spawnFood();
        drawFood();
    }

    if (growPending > 0) {
        growPending--;
    } else {
        var tail = snake.pop();
        drawCell(tail.x, tail.y, T.bg);
    }
}

function die() {
    saveHi();
    drawHeader();
    state = "over";
    drawGameOver(false);
}

// -------------------------------------------------------------- entrada ---
var press = null, lastP = null;

function pollTouch() {
    var t = System.getTouch();
    if (t.touched) {
        if (!press) { press = { x: t.x, y: t.y }; lastP = { x: t.x, y: t.y }; }
        else { lastP.x = t.x; lastP.y = t.y; }
        return false;
    }
    if (press) {
        var dx = lastP.x - press.x, dy = lastP.y - press.y;
        press = null;
        if (state === "play") {
            if (Math.abs(dx) < 12 && Math.abs(dy) < 12) return false;  // tap: segue
            if (Math.abs(dx) > Math.abs(dy)) nextDir = { dx: dx > 0 ? 1 : -1, dy: 0 };
            else nextDir = { dx: 0, dy: dy > 0 ? 1 : -1 };
        }
        return true;  // gesto completo (usado no game over)
    }
    return false;
}

// ------------------------------------------------------------------ main ---
var win = false;
reset();
countdown();

var lastTick = System.millis();
while (true) {
    var done = pollTouch();

    if (state === "count") {
        countdown();
        lastTick = System.millis();
    } else if (state === "play") {
        var now = System.millis();
        if (now - lastTick >= interval) {
            lastTick = now;
            step();
        }
    } else if (done) {
        // game over: toque completo reinicia
        System.fillScreen(T.bg);
        reset();
        countdown();
        lastTick = System.millis();
    }

    System.delay(10);
}
