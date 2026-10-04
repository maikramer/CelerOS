// Breakout — quebre os tijolos, arraste a raquete. ES5 puro (Duktape);
// placar e dialogos do toolkit UI (API 22). Fisica delta-time com sub-passos; recorde em
// /local/config_breakout_hi.txt.

var T = System.theme();
var W = 240, H = 320;

var FX = 4, FY = 44;                 // canto do campo
var FW = W - 2 * FX, FH = H - FY - 2;
var BRICK_COLS = 8, BRICK_ROWS = 5;
var BRICK_W = (FW - 16) / BRICK_COLS;   // 27
var BRICK_H = 12;
var BRICK_TOP = FY + 10;
var PADDLE_W = 48, PADDLE_H = 8, PADDLE_Y = H - 22;
var BALL_R = 3;
var HI_FILE = "/local/config_breakout_hi.txt";

var ROW_COLORS = [T.err, T.warn, T.ok, T.accent, T.accentD];
var ROW_POINTS = [50, 40, 30, 20, 10];

var hi = 0;
var raw = FS.readTextFile(HI_FILE);
if (raw) { var hp = parseInt(raw, 10); if (!isNaN(hp)) hi = hp; }

// ------------------------------------------------------------- estado ------
var bricks, score, lives, level, state;   // state: serve|play|over
var paddleX, ball, speed;
var pbx, pby, ppx;                        // posicao anterior (p/ apagar rastro)
var nextLife = 500;                       // vida extra a cada 500 pts (max 5)

function buildBricks() {
    bricks = [];
    for (var r = 0; r < BRICK_ROWS; r++) {
        for (var c = 0; c < BRICK_COLS; c++) {
            bricks.push({ x: FX + 8 + c * BRICK_W, y: BRICK_TOP + r * (BRICK_H + 3),
                          r: r, alive: true });
        }
    }
}

function resetBall() {
    state = "serve";
    ball = { x: paddleX + PADDLE_W / 2, y: PADDLE_Y - BALL_R - 1, dx: 0, dy: 0 };
    pbx = undefined;
    pby = undefined;
}

function newGame() {
    score = 0;
    lives = 3;
    level = 1;
    speed = 130;
    nextLife = 500;
    paddleX = (W - PADDLE_W) / 2;
    ppx = undefined;
    buildBricks();
    resetBall();
    drawStatic();
    drawHeader();
}

function launch() {
    // angulo inicial levemente aleatorio para a direita/esquerda
    var ang = (-60 - Math.random() * 60) * Math.PI / 180;   // para cima
    if (Math.random() < 0.5) ang = -ang + Math.PI;
    ball.dx = Math.cos(ang);
    ball.dy = Math.sin(ang);
    state = "play";
}

// ---------------------------------------------------------- fisica (dt ms) -
function step(dt) {
    if (state !== "play") return;
    var dist = (speed * dt) / 1000;
    var n = Math.ceil(dist / 3);            // sub-passos anti-tunel
    var sd = dist / n;
    for (var i = 0; i < n; i++) {
        ball.x += ball.dx * sd;
        ball.y += ball.dy * sd;
        collide();
        if (state !== "play") return;
    }
}

function reflectX() { ball.dx = -ball.dx; }
function reflectY() { ball.dy = -ball.dy; }

function collide() {
    // paredes
    if (ball.x < FX + BALL_R) { ball.x = FX + BALL_R; reflectX(); }
    if (ball.x > FX + FW - BALL_R) { ball.x = FX + FW - BALL_R; reflectX(); }
    if (ball.y < FY + BALL_R) { ball.y = FY + BALL_R; reflectY(); }

    // raquete: rebote depende do ponto de contato
    if (ball.dy > 0 && ball.y + BALL_R >= PADDLE_Y &&
        ball.y + BALL_R <= PADDLE_Y + PADDLE_H + 6 &&
        ball.x >= paddleX - BALL_R && ball.x <= paddleX + PADDLE_W + BALL_R) {
        ball.y = PADDLE_Y - BALL_R;
        var rel = (ball.x - (paddleX + PADDLE_W / 2)) / (PADDLE_W / 2);  // -1..1
        var ang = rel * 1.1;                                             // rad p/ vertical
        var sp = Math.sqrt(ball.dx * ball.dx + ball.dy * ball.dy);
        ball.dx = sp * Math.sin(ang);
        ball.dy = -Math.abs(sp * Math.cos(ang));
    }

    // tijolos
    for (var i = 0; i < bricks.length; i++) {
        var b = bricks[i];
        if (!b.alive) continue;
        if (ball.x + BALL_R < b.x || ball.x - BALL_R > b.x + BRICK_W ||
            ball.y + BALL_R < b.y || ball.y - BALL_R > b.y + BRICK_H) continue;
        b.alive = false;
        // sai da tela na hora: antes ficava pintado (so "atravessavel") e a
        // bola ia carimbando o rastro de fundo nele
        System.fillRect(b.x, b.y, BRICK_W - 2, BRICK_H, T.bg);
        score += ROW_POINTS[b.r];
        if (score >= nextLife && lives < 5) {
            lives++;
            nextLife += 500;
        }
        drawHeader();
        // reflete no eixo de menor penetracao
        var cx = (b.x + BRICK_W / 2) - ball.x;
        var cy = (b.y + BRICK_H / 2) - ball.y;
        var ox = (BRICK_W / 2 + BALL_R) - Math.abs(cx);
        var oy = (BRICK_H / 2 + BALL_R) - Math.abs(cy);
        if (ox < oy) reflectX(); else reflectY();
        if (levelCleared()) {
            level++;
            speed += 22;
            score += 100;
            buildBricks();
            resetBall();
            drawStatic();
            drawHeader();
            UI.text("Nível " + level + "!", 120, 152, { role: "title", align: "center", color: T.warn });
            System.delay(900);
            drawStatic();
        }
        break;
    }

    // fundo: perde vida
    if (ball.y > H + 8) {
        lives--;
        if (lives <= 0) {
            state = "over";
            if (score > hi) { hi = score; FS.writeTextFile(HI_FILE, String(hi)); }
        } else resetBall();
        drawHeader();
    }
}

function levelCleared() {
    for (var i = 0; i < bricks.length; i++) if (bricks[i].alive) return false;
    return true;
}

// ------------------------------------------------------------- desenho -----
function drawStatic() {
    System.fillRect(FX, FY, FW, FH, T.bg);
    System.drawRect(FX, FY, FW, FH, T.stroke);
    for (var i = 0; i < bricks.length; i++) {
        var b = bricks[i];
        if (!b.alive) continue;
        System.fillGradient(b.x, b.y, BRICK_W - 2, BRICK_H, ROW_COLORS[b.r], System.mixColor(ROW_COLORS[b.r], 0, 35), 3);
    }
}

function drawBall() {
    // apaga a regiao antiga da bola com o fundo (barato e sem fantasma)
    if (pbx !== undefined) {
        System.fillCircle(pbx, pby, BALL_R + 1, T.bg);
    }
    if (state !== "over") System.fillSmoothCircle(ball.x, ball.y, BALL_R, T.text);
    pbx = ball.x;
    pby = ball.y;
}

function drawPaddle() {
    if (ppx !== undefined) {
        System.fillRect(ppx - 2, PADDLE_Y, PADDLE_W + 4, PADDLE_H, T.bg);
    }
    System.fillGradient(paddleX, PADDLE_Y, PADDLE_W, PADDLE_H, T.accent, T.accentD, 4);
    ppx = paddleX;
}

function drawHeader() {
    // o nome vive na faixa do sistema (retratil); aqui e so o placar
    System.fillRect(0, 0, W, 40, T.card);
    UI.text("Pontos " + score, 10, 4, { role: "title", bg: T.card });
    UI.text("Vidas " + lives + "  ·  Rec " + hi, W - 10, 6, { role: "caption", align: "right", color: T.textDim, bg: T.card });
    UI.text("Nível " + level + (state === "serve" ? "  ·  toque para lançar" : ""), 10, 24,
            { role: "caption", color: T.accent, bg: T.card });
    System.fillRect(0, 40, W, 2, T.accent);
}

// -------------------------------------------------------------- entrada ----
var touched = false;

function pollInput() {
    var t = System.getTouch();
    if (t.touched) {
        touched = true;
        var nx = t.x - PADDLE_W / 2;
        if (nx < FX + 1) nx = FX + 1;
        if (nx > FX + FW - PADDLE_W - 1) nx = FX + FW - PADDLE_W - 1;
        paddleX = nx;
        if (state === "serve") {
            ball.x = paddleX + PADDLE_W / 2;
        }
    } else if (touched) {
        touched = false;
        if (state === "serve") launch();

    }
}

// ------------------------------------------------------------------ main ---
var lastTick = System.millis();
newGame();

while (true) {
    pollInput();
    var now = System.millis();
    var dt = now - lastTick;
    lastTick = now;
    if (dt > 100) dt = 100;
    step(dt);
    drawPaddle();
    drawBall();
    if (state === "over") {
        // dialogo nativo sobre o campo; ao voltar, partida nova
        UI.alert("Fim de jogo", "Pontos: " + score + "   Recorde: " + hi, "Jogar de novo");
        newGame();
        lastTick = System.millis();
    }
    System.delay(10);
}
