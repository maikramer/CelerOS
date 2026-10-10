// Neon Surfer (2D Runner) — corrida infinita em 3 faixas. ES5 puro
// (Duktape), toolkit UI (API 22) nos menus e desenho direto no frame
// automatico durante o jogo (so o dirty box vai ao vidro: zero flicker,
// sem sprite por fatias). Primitivas 22: gradiente no cenario, pecas
// suavizadas (fillSmooth*), drawWideLine nas bordas neon. Recorde no
// appData; sons playTone (API 12).

var T = System.theme();
var W = 240, H = 320;

var ROAD_X = 20, ROAD_W = 200;          // pista com 3 faixas de ~66 px
var LANE_W = ROAD_W / 3;
var LANES = [ROAD_X + LANE_W / 2, ROAD_X + 3 * LANE_W / 2, ROAD_X + 5 * LANE_W / 2];
var HUD_H = 34;

var HI_FILE = (FS.appData ? FS.appData() : "/local/") + "hi.txt";
var hi = 0;
var rawHi = FS.readTextFile(HI_FILE);
if (rawHi) {
    var hv = parseInt(rawHi, 10);
    if (!isNaN(hv)) hi = hv;
}

// Som: System.sfx (API 32) mistura o efeito por cima da trilha e NAO trava
// o loop do jogo; firmware antigo cai no playTone (bloqueante)
var somFn = typeof System.sfx === "function" ? System.sfx :
            (typeof System.playTone === "function" ? System.playTone : null);
var hasTone = somFn !== null;
function tone(hz, ms) {
    if (hasTone) { try { somFn && somFn([[hz, ms]]); } catch (e) {} }
}
function tune(notes) {
    if (hasTone) { try { somFn && somFn(notes); } catch (e) {} }
}

// ---------------------------------------------------------------- estado ---
var state = "menu";              // menu | play | over | ajustes
var showFPS = false;
var showTemp = false;

// troca de tela do chrome: invalida o frame para o toolkit redesenhar tudo
// (os widgets so repintam quando a propria assinatura muda; sem isto a tela
// anterior ficaria "colada" por baixo da nova)
function goState(s) {
    state = s;
    UI.invalidate();
}
// entrada no jogo: o drawScene pinta a tela inteira por cima do chrome
function enterPlay() {
    resetGame();
    state = "play";
}

var playerLane, playerY, playerBob, playerX;   // playerX anima a troca
var score, speed, distance, entities, spawnT, newRecord;
var lastTime = System.millis();
var fps = 0, frames = 0, lastFpsTime = System.millis();

function resetGame() {
    playerLane = 1;
    playerX = LANES[1];
    playerY = 262;
    playerBob = 0;
    score = 0;
    speed = 150;
    distance = 0;
    entities = [];
    spawnT = 0;
    newRecord = false;
}

function saveHi() {
    if (score > hi) {
        hi = score;
        newRecord = true;
        FS.writeTextFile(HI_FILE, String(hi));
        tune([[784, 80], [1047, 110]]);
    }
}

// ------------------------------------------------------------- simulacao ---
function update(dt) {
    var move = speed * dt;
    distance += move;
    playerBob = Math.sin(System.millis() / 150.0) * 5;

    // troca de faixa suave (interp para o centro da faixa alvo)
    var target = LANES[playerLane];
    playerX += (target - playerX) * Math.min(1, dt * 14);
    if (Math.abs(target - playerX) < 0.5) playerX = target;

    spawnT += dt;
    var rate = Math.max(0.2, 1.0 - speed / 1000.0);
    if (spawnT > rate) {
        spawnT = 0;
        var lane = Math.floor(Math.random() * 3);
        if (lane > 2) lane = 2;
        if (Math.random() > 0.3) {
            entities.push({ type: "train", lane: lane, y: -100, h: 80 });
        } else {
            entities.push({ type: "coin", lane: lane, y: -20, h: 20 });
        }
    }

    var px = playerX - 15, pw = 30, py = playerY - 20 + playerBob, ph = 40;
    for (var i = entities.length - 1; i >= 0; i--) {
        var e = entities[i];
        e.y += move;
        if (e.y > H + 20) {
            entities.splice(i, 1);
            if (e.type === "train") score += 5;
            continue;
        }
        // colisao por proximidade de faixa (a troca em curso conta)
        var near = Math.abs(LANES[e.lane] - playerX) < LANE_W / 2;
        if (!near) continue;
        if (e.type === "train") {
            if (py < e.y + e.h && py + ph > e.y) {
                tone(150, 250);
                saveHi();
                goState("over");
                return;
            }
        } else if (py < e.y + 10 && py + ph > e.y - 10) {
            score += 50;
            tone(988, 40);
            entities.splice(i, 1);
        }
    }
    speed += dt * 5.0;
}

// ---------------------------------------------------------------- desenho ---
function drawScene() {
    System.fillRect(0, 0, W, H, T.bg);

    // ceu neon: gradiente escuro a trevas acima da pista
    System.fillGradient(0, 0, W, HUD_H, T.raised, T.bg);
    System.fillRect(ROAD_X - 12, HUD_H, ROAD_W + 24, H - HUD_H, System.mixColor(T.bg, T.raised, 45));

    // bordas neon com brilho (linha grossa + fina clara)
    System.drawWideLine(ROAD_X - 8, HUD_H, ROAD_X - 8, H, 3, T.accentD);
    System.drawWideLine(ROAD_X - 8, HUD_H, ROAD_X - 8, H, 1, T.accent);
    System.drawWideLine(ROAD_X + ROAD_W + 8, HUD_H, ROAD_X + ROAD_W + 8, H, 3, T.accentD);
    System.drawWideLine(ROAD_X + ROAD_W + 8, HUD_H, ROAD_X + ROAD_W + 8, H, 1, T.accent);

    // tracinhos entre faixas rolando com a distancia
    var off = Math.floor(distance) % 44;
    for (var y = -44; y < H; y += 44) {
        var ry = y + off;
        if (ry < HUD_H || ry > H - 18) continue;
        System.fillRoundRect(ROAD_X + LANE_W - 2, ry, 4, 22, 2, T.stroke);
        System.fillRoundRect(ROAD_X + 2 * LANE_W - 2, ry, 4, 22, 2, T.stroke);
    }

    // entidades
    for (var i = 0; i < entities.length; i++) {
        var e = entities[i];
        var ex = LANES[e.lane];
        if (e.type === "train") {
            System.fillSmoothRoundRect(ex - 28, e.y, 56, e.h, 8, T.err);
            System.fillSmoothRoundRect(ex - 22, e.y + 6, 44, 16, 4, T.onAccent);
            System.fillRect(ex - 20, e.y + e.h - 14, 40, 4, T.warn);
        } else {
            System.fillSmoothCircle(ex, e.y, 12, T.warn);
            System.fillSmoothCircle(ex - 3, e.y - 3, 5, System.mixColor(T.warn, T.text, 40));
        }
    }

    // hoverboard do jogador
    var pxx = playerX, pyy = playerY + playerBob;
    System.fillSmoothCircle(pxx, pyy + 22, 10, T.accentD);            // somra/glow
    System.fillSmoothCircle(pxx, pyy + 22, 6, T.accent);
    System.fillSmoothRoundRect(pxx - 15, pyy - 18, 30, 40, 9, T.accent);
    System.fillSmoothRoundRect(pxx - 9, pyy - 12, 18, 12, 5, T.onAccent);
    System.fillSmoothCircle(pxx, pyy + 4, 4, T.text);

    // HUD (drawString direto: muda todo frame, os slots do toolkit so
    // atrapalhariam — o fillRect do fundo apagaria textos imutaveis)
    System.fillRect(0, 0, W, HUD_H, T.raised);
    System.drawFastHLine(0, HUD_H - 1, W, T.stroke);
    System.setTextColor(T.text, T.raised);
    System.drawString("Pontos " + score, 10, 8, 2);
    System.setTextDatum(2);           // TR
    System.setTextColor(score > 0 && score >= hi && newRecord ? T.ok : T.textDim, T.raised);
    System.drawString("Rec " + hi, W - 10, 12, 1);
    System.setTextDatum(0);
    if (showFPS) {
        System.setTextColor(T.textDim, T.raised);
        System.drawString(fps + " fps", 108, 12, 1);
    }
    if (showTemp && typeof System.getTemperature === "function") {
        try {
            System.setTextColor(T.textDim, T.raised);
            System.drawString(System.getTemperature().toFixed(0) + "\u00B0C", 152, 12, 1);
        } catch (err) {}
    }
}

// ----------------------------------------------------------------- input ---
var press = null, lastP = null;
function pollGesture() {
    var t = System.getTouch();
    if (t.touched) {
        if (!press) { press = { x: t.x, y: t.y }; lastP = { x: t.x, y: t.y }; }
        else { lastP.x = t.x; lastP.y = t.y; }
        return false;
    }
    if (press) {
        var dx = lastP.x - press.x, dy = lastP.y - press.y;
        press = null;
        if (Math.abs(dx) < 14 && Math.abs(dy) < 14) {
            // toque seco: lado da tela move a faixa
            if (lastP.x < W / 2) { if (playerLane > 0) { playerLane--; tone(520, 18); } }
            else { if (playerLane < 2) { playerLane++; tone(520, 18); } }
        } else if (Math.abs(dx) > Math.abs(dy)) {
            if (dx > 0) { if (playerLane < 2) { playerLane++; tone(620, 18); } }
            else { if (playerLane > 0) { playerLane--; tone(620, 18); } }
        }
    }
    return false;
}

// ------------------------------------------------------------------ main ---
resetGame();
while (true) {
    var now = System.millis();
    var dt = (now - lastTime) / 1000.0;
    lastTime = now;
    if (dt > 0.1) dt = 0.1;
    frames++;
    if (now - lastFpsTime >= 1000) { fps = frames; frames = 0; lastFpsTime = now; }

    if (state === "menu" || state === "ajustes" || state === "over") {
        UI.begin(T.bg);
        if (state === "menu") {
            UI.text("NÉON", W / 2, 70, { role: "display", align: "center", color: T.accent });
            UI.text("SURFER", W / 2, 112, { role: "display", align: "center", color: T.text });
            UI.badge("recorde " + hi, 12, 150, { color: T.accentD });
            if (UI.button("JOGAR", 40, 190, 160, 48)) enterPlay();
            if (UI.button("Ajustes", 40, 246, 160, 44, { style: "ghost" })) goState("ajustes");
            UI.text("toque ou deslize para trocar de faixa", W / 2, 300,
                    { role: "caption", align: "center", color: T.textDim });
        } else if (state === "ajustes") {
            if (UI.header("Ajustes", { back: true })) goState("menu");
            UI.text("mostrar", 16, 66, { role: "title" });
            UI.text("contador de FPS", 16, 96);
            showFPS = UI.toggle(180, 90, showFPS);
            UI.text("temperatura do chip", 16, 136);
            showTemp = UI.toggle(180, 130, showTemp);
            UI.text("os ajustes não são salvos", W / 2, 300,
                    { role: "caption", align: "center", color: T.textDim });
        } else if (state === "over") {
            UI.text("BATEU!", W / 2, 70, { role: "display", align: "center", color: T.err });
            UI.text("pontos " + score, W / 2, 120, { role: "title", align: "center" });
            if (newRecord) {
                UI.badge("novo recorde!", W / 2 - 40, 145, { color: T.ok, textColor: T.onAccent });
            } else {
                UI.badge("recorde " + hi, W / 2 - 36, 145, { color: T.accentD });
            }
            if (UI.button("De novo", 40, 200, 160, 48)) enterPlay();
            if (UI.button("Menu", 40, 256, 160, 44, { style: "ghost" })) goState("menu");
        }
        UI.end();
        System.delay(10);
        continue;
    }

    // ------------------------------------------------------------- play ---
    pollGesture();
    update(dt);
    if (state === "play") {
        drawScene();
        System.delay(1);
    }
}
