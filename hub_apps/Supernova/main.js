// Supernova — atirador espacial em tela cheia nos PIXELS NATIVOS do vidro
// (API 28: 480x480 no SmartDisplay 4", sem a esticada do canvas virtual).
// O que este app mostra de "poder de ESP32":
//   - canvas nativo: coordenadas/toque em pixels fisicos 1:1;
//   - sprites PNG gerados por IA (text2d) decodificados UMA vez em sprites
//     PSRAM e blit por quadro (pushSprite), com glow por cima do blit;
//   - trilha chiptune de 4 canais (playMusic) que e o metronomo do jogo:
//     ondas de inimigos spawnam na batida (musicPos);
//   - modulos require (engine/fx/audio), recorde no Storage NVS,
//     particularias/ondas de choque/tremor em primitivas anti-aliasadas.
// Sem topbar: saida pelo proprio menu (System.exitApp).

var T = System.theme();
if (typeof System.setNativeCanvas === "function") System.setNativeCanvas(true);
var W = System.screenWidth(), H = System.screenHeight();

var engine = require("engine");
var fx = require("fx");
var audio = require("audio");

var C = {
    espaco: 0x0000,
    ciano: 0x07FF, cianoD: 0x03EF, magenta: 0xF81F, laranja: 0xFD20,
    ouro: 0xFFE0, branco: 0xFFFF, verde: 0x07E0,
    cinza: System.mixColor(0x0000, 0xFFFF, 18)
};

// ------------------------------------------------------------- sprites ---
// Pool de 4 slots (PSRAM): cada PNG e decodificado uma unica vez para
// dentro do sprite; no jogo e so blit. Sem assets (ou no emulador) cai na
// via procedural — o jogo roda igual.
var SPR = { nave: 0, inimigo: 0, olho: 0, orbe: 0 };
var hasSpr = false;

var BASES = ["/local/apps/Supernova/assets/", "/sd/apps/Supernova/assets/"];
var hasPNG = (typeof System.drawPNG === "function");

// drawPNG e a unica sonda de existencia de asset: no boot nada foi
// apresentado ainda, e nos sprites o alvo e o proprio sprite — em ambos
// a sonda desenhou exatamente o que queriamos.
function loadSprite(w, h, file, fallbackPaint) {
    if (typeof System.createSprite !== "function") return 0;
    var id = System.createSprite(w, h);
    if (!id) return 0;
    System.useSprite(id);
    System.fillScreen(C.espaco);
    var ok = false;
    if (hasPNG) {
        for (var i = 0; i < BASES.length && !ok; i++) {
            try { ok = System.drawPNG(BASES[i] + file + ".png", 0, 0); } catch (e) { ok = false; }
        }
    }
    if (!ok) fallbackPaint(w, h);
    System.useSprite(0);
    return id;
}

function paintNave(w, h) {
    System.fillTriangle(w / 2, 2, 6, h - 8, w - 6, h - 8, C.ciano);
    System.fillTriangle(w / 2, 10, w / 2 - 12, h - 14, w / 2 + 12, h - 14, C.branco);
    System.fillCircle(w / 2, h / 2 + 2, 5, C.cianoD);
}
function paintDrone(w, h) {
    System.fillTriangle(w / 2, 4, 2, h - 6, w - 2, h - 6, C.laranja);
    System.fillCircle(w / 2, h / 2, 6, C.magenta);
}
function paintOlho(w, h) {
    System.fillCircle(w / 2, h / 2, w / 2 - 3, C.magenta);
    System.fillCircle(w / 2, h / 2, w / 3, C.branco);
    System.fillCircle(w / 2, h / 2, w / 6, 0x0000);
}
function paintOrbe(w, h) {
    System.fillCircle(w / 2, h / 2, w / 2 - 2, C.ouro);
    System.fillCircle(w / 2, h / 2, w / 4, C.branco);
}

// blit com cor-chave: o fundo preto do sprite (unico 0x0000 do PNG — os
// pretos internos viraram quase-preto na masterizacao) nao transfere, e a
// arte passa sobre estrelas/glow/outros sprites sem o quadrado.
function blit(id, cx, cy, size) {
    System.useSprite(id);
    System.pushSprite(Math.round(cx - size / 2), Math.round(cy - size / 2), 0x0000);
    System.useSprite(0);
}

// ---------------------------------------------------------------- estado ---
var MODE = "titulo";   // titulo | jogando | pausa | fim
var hi = 0;
try { hi = parseInt(Storage.get("hi", "0"), 10) || 0; } catch (e) { hi = 0; }

System.keepAwake(true);
engine.init(W, H);
fx.init(W, H, T);

// Pega de teste (so existe no harness; no device __harness e indefinido):
// deixa o test.js dirigir engine/fx/audio deterministicamente — cobrir
// hit/fim-de-jogo/supernova sem depender de sorte do spawn.
if (typeof __harness !== "undefined") {
    __harness.supernova = { engine: engine, fx: fx, audio: audio };
}

SPR.nave = loadSprite(engine.NAVE, engine.NAVE, "nave", paintNave);
SPR.inimigo = loadSprite(engine.INIMIGO, engine.INIMIGO, "inimigo", paintDrone);
SPR.olho = loadSprite(engine.OLHO, engine.OLHO, "olho", paintOlho);
SPR.orbe = loadSprite(engine.ORBE, engine.ORBE, "orbe", paintOrbe);
hasSpr = (SPR.nave !== 0 && typeof System.pushSprite === "function");

var tituloPath = null;   // resolvido no drawTituloBase (sonda invisivel pre-loop)
var last = System.millis();

// ------------------------------------------------------------- entrada ---
var touch = { down: false, x: 0, y: 0, sx: 0, sy: 0, t0: 0, moved: false };
var tap = null;   // {x, y} do toque seco (so quando solta sem arrastar)

function pollInput() {
    tap = null;
    var t = System.getTouch();
    var now = System.millis();
    if (t.touched) {
        if (!touch.down) { touch.down = true; touch.sx = t.x; touch.sy = t.y; touch.t0 = now; touch.moved = false; }
        touch.x = t.x; touch.y = t.y;
        if (Math.abs(t.x - touch.sx) > 12 || Math.abs(t.y - touch.sy) > 12) touch.moved = true;
    } else if (touch.down) {
        touch.down = false;
        if (!touch.moved && now - touch.t0 < 300) tap = { x: touch.sx, y: touch.sy };
    }
    return t;
}

function hit(b) {
    return !!tap && tap.x >= b.x && tap.x <= b.x + b.w && tap.y >= b.y && tap.y <= b.y + b.h;
}

// arrasto horizontal move a nave (relativo: o dedo nao cobre a nave)
function drivePlayer(t) {
    var p = engine.state().player;
    if (!p.alive) return;
    if (t.touched && touch.moved) {
        var nx = p.x + t.x - touch.sx;
        if (nx < 30) nx = 30;
        if (nx > W - 30) nx = W - 30;
        p.x = nx;
        touch.sx = t.x; touch.sy = t.y;
    }
}

// --------------------------------------------------------------- desenho ---
var HUD_H = 44;

function drawGame(now, beatF) {
    var s = engine.state();
    fx.roll();
    var shx = fx.shakeX(), shy = fx.shakeY();
    var pulse = beatF >= 0 ? Math.max(0, 1 - (beatF - Math.floor(beatF)) * 3) : 0;

    System.fillScreen(C.espaco);
    fx.drawStars(now, pulse);

    // orbes (halo pulsante + blit)
    var i;
    for (i = 0; i < s.orbs.length; i++) {
        var o = s.orbs[i];
        var pulso = 0.5 + 0.5 * Math.sin(o.ph);
        System.fillSmoothCircle(Math.round(o.x + shx), Math.round(o.y + shy), 26 + pulso * 5,
                                System.mixColor(C.ouro, C.espaco, 55 - pulso * 25));
        if (hasSpr && SPR.orbe) blit(SPR.orbe, o.x + shx, o.y + shy, engine.ORBE);
        else { System.fillCircle(Math.round(o.x + shx), Math.round(o.y + shy), 14, C.ouro);
               System.fillCircle(Math.round(o.x + shx), Math.round(o.y + shy), 6, C.branco); }
    }

    // inimigos
    for (i = 0; i < s.enemies.length; i++) {
        var e = s.enemies[i];
        var sz = e.kind === 1 ? engine.OLHO : engine.INIMIGO;
        var sid = e.kind === 1 ? SPR.olho : SPR.inimigo;
        var x = Math.round(e.x + shx), y = Math.round(e.y + shy);
        if (hasSpr && sid) {
            blit(sid, x, y, sz);
            // nucleo pulsante POR CIMA do blit: animacao sem slot extra
            if (e.kind === 1) System.fillCircle(x, y, 4 + Math.sin(now / 130 + e.ph) * 2, C.verde);
            else System.fillCircle(x, y + 8, 3 + Math.sin(now / 90 + e.ph) * 2, C.magenta);
        } else {
            var col = e.kind === 1 ? C.magenta : C.laranja;
            System.fillSmoothCircle(x, y, sz / 2 - 2, col);
            System.fillCircle(x, y, 6, C.branco);
            if (e.kind === 0) System.fillTriangle(x, y - 6, x - 14, y + 10, x + 14, y + 10, col);
        }
    }

    // nave (pisca invulneravel)
    var p = s.player;
    if (p.alive && (p.invuln <= 0 || Math.floor(now / 90) % 2 === 0)) {
        var px = Math.round(p.x + shx), py = Math.round(p.y + shy);
        var fl = 10 + Math.sin(now / 40) * 5;
        System.fillSmoothCircle(px - 12, py + 40, 5, C.cianoD);
        System.fillSmoothCircle(px + 12, py + 40, 5, C.cianoD);
        System.fillTriangle(px - 9, py + 34, px + 9, py + 34, px, py + 34 + fl, C.ciano);
        if (hasSpr && SPR.nave) blit(SPR.nave, px, py, engine.NAVE);
        else {
            System.fillTriangle(px, py - 30, px - 20, py + 26, px + 20, py + 26, C.ciano);
            System.fillTriangle(px, py - 20, px - 9, py + 18, px + 9, py + 18, C.branco);
        }
    }

    // tiros
    for (i = 0; i < s.bullets.length; i++) {
        var b = s.bullets[i];
        System.drawFastVLine(Math.round(b.x + shx), Math.round(b.y + shy), 14, C.ciano);
        System.drawPixel(Math.round(b.x + shx), Math.round(b.y + shy + 15), C.branco);
    }
    for (i = 0; i < s.ebullets.length; i++) {
        var eb = s.ebullets[i];
        System.fillCircle(Math.round(eb.x + shx), Math.round(eb.y + shy), 4, C.magenta);
        System.drawPixel(Math.round(eb.x + shx), Math.round(eb.y + shy), C.branco);
    }

    fx.draw();
    drawHUD(now, s);
    fx.drawFlash();
}

function drawHUD(now, s) {
    System.fillGradient(0, 0, W, HUD_H, System.mixColor(T.bg, 0x0000, 45), 0x0000, 0);
    System.drawFastHLine(0, HUD_H, W, C.cianoD);

    System.setTextColor(C.branco, 0x0000);
    System.setTextDatum(0);
    System.drawString(String(s.score), 14, 6, 4);
    System.setTextColor(C.cinza, 0x0000);
    System.drawString("rec " + hi, 14, 36, 1);
    System.setTextDatum(2);
    System.drawString("onda " + s.wave, W - 14, 8, 2);
    // pausa: duas barras no canto
    System.fillRect(W - 40, 32, 6, 12, C.cinza);
    System.fillRect(W - 30, 32, 6, 12, C.cinza);
    // vidas: mini naves
    for (var i = 0; i < s.lives; i++) {
        System.fillTriangle(W - 20 - i * 22, 44, W - 28 - i * 22, 32, W - 12 - i * 22, 32, C.ciano);
    }
    System.setTextDatum(0);

    // medidor supernova: orbe no rodape (arco de carga)
    var gx = W / 2, gy = H - 30;
    var cheio = s.charge >= 100;
    var pulso = cheio ? 0.5 + 0.5 * Math.sin(now / 110) : 0;
    System.fillSmoothCircle(gx, gy, 20 + pulso * 4,
                            System.mixColor(C.ouro, 0x0000, 45 - pulso * 30));
    System.fillArc(gx, gy, 12, 16, -90, -90 + Math.round(s.charge * 3.6), C.ouro);
    if (cheio) {
        System.setTextColor(C.ouro, 0x0000);
        System.setTextDatum(5);
        System.drawString("SUPERNOVA", gx, gy - 34, 1);
        System.setTextDatum(0);
    }
}

// ------------------------------------------------------------ telas/menu ---
function btn(label, x, y, w, h, primary) {
    var fill = primary ? C.ciano : System.mixColor(C.ciano, 0x0000, 72);
    System.fillSmoothRoundRect(x, y, w, h, 12, fill);
    System.setTextColor(primary ? 0x0000 : C.branco, fill);
    System.setTextDatum(5);
    System.drawString(label, x + w / 2, y + h / 2, 2);
    System.setTextDatum(0);
    return { x: x, y: y, w: w, h: h };
}

// A arte do titulo (PNG 480x230) e desenhada UMA vez por entrada no
// estado; por frame so a metade de baixo (titulo + botoes) repinta.
var TIT_Y0 = 0;
function drawTituloBase() {
    System.fillScreen(C.espaco);
    if (hasPNG && !tituloPath) {
        for (var i = 0; i < BASES.length && !tituloPath; i++) {
            var p = BASES[i] + "titulo.png";
            try { if (System.drawPNG(p, 0, 0)) tituloPath = p; } catch (e) {}
        }
    } else if (tituloPath) {
        try { System.drawPNG(tituloPath, 0, 0); } catch (e) { tituloPath = null; }
    }
    TIT_Y0 = Math.round(H * 0.42);
    if (!tituloPath) {   // sem arte: campo de estrelas serve de fundo
        for (var j = 0; j < 60; j++) {
            System.drawPixel(Math.floor(Math.random() * W), Math.floor(Math.random() * H), C.cinza);
        }
    }
}

function drawTitulo(now) {
    var y0 = TIT_Y0;
    System.fillGradient(0, y0, W, H - y0, 0x0000, System.mixColor(T.bg, 0x0000, 35), 0);
    System.setTextColor(C.ciano, 0x0000);
    System.setTextDatum(5);
    System.drawString("SUPERNOVA", W / 2, y0 + 36, 4);
    System.setTextColor(C.cinza, 0x0000);
    System.drawString("arraste para voar, ondas na batida da música", W / 2, y0 + 66, 1);
    System.setTextDatum(0);
    return {
        jogar: btn("JOGAR", W / 2 - 90, y0 + 96, 180, 52, true),
        sair: btn("SAIR", W / 2 - 90, y0 + 160, 180, 44, false)
    };
}

function drawPausa() {
    System.fillScreen(System.mixColor(T.bg, 0x0000, 12));
    System.fillSmoothRoundRect(W / 2 - 110, H / 2 - 130, 220, 300, 16, System.mixColor(T.bg, 0x0000, 25));
    System.setTextColor(C.branco, 0x0000);
    System.setTextDatum(5);
    System.drawString("PAUSA", W / 2, H / 2 - 96, 4);
    System.setTextDatum(0);
    return {
        seguir: btn("CONTINUAR", W / 2 - 90, H / 2 - 44, 180, 48, true),
        sair: btn("SAIR", W / 2 - 90, H / 2 + 84, 180, 44, false)
    };
}

function drawFim() {
    var s = engine.state();
    System.fillScreen(C.espaco);
    fx.drawStars(System.millis(), 0);
    fx.draw();
    System.setTextColor(C.magenta, 0x0000);
    System.setTextDatum(5);
    System.drawString("NAVE PERDIDA", W / 2, H / 2 - 116, 4);
    System.setTextColor(C.branco, 0x0000);
    System.drawString("pontuação " + s.score, W / 2, H / 2 - 58, 2);
    if (s.newRecord) {
        System.setTextColor(C.ouro, 0x0000);
        System.drawString("NOVO RECORDE!", W / 2, H / 2 - 26, 2);
    } else {
        System.setTextColor(C.cinza, 0x0000);
        System.drawString("recorde " + hi, W / 2, H / 2 - 26, 1);
    }
    System.setTextDatum(0);
    return {
        deNovo: btn("DE NOVO", W / 2 - 90, H / 2 + 14, 180, 52, true),
        sair: btn("SAIR", W / 2 - 90, H / 2 + 78, 180, 44, false)
    };
}

// ------------------------------------------------------------ transicoes ---
function enterJogo() {
    engine.reset();
    fx.init(W, H, T);
    MODE = "jogando";
    audio.start(0);
    audio.sfx("ui");
}

function enterTitulo() {
    MODE = "titulo";
    audio.stop();
    drawTituloBase();
}

function salvarFim() {
    var s = engine.state();
    if (s.score > hi) {
        hi = s.score;
        s.newRecord = true;
        try { Storage.set("hi", String(hi)); } catch (e) {}
        audio.sfx("record");
    }
}

// ------------------------------------------------------------------ loop ---
drawTituloBase();
while (true) {
    var now = System.millis();
    var dt = (now - last) / 1000.0;
    last = now;
    if (dt > 0.1) dt = 0.1;

    var beatF = audio.beat();
    var t = pollInput();

    if (MODE === "titulo") {
        var bu = drawTitulo(now);
        if (hit(bu.jogar)) enterJogo();
        else if (hit(bu.sair)) { audio.sfx("ui"); System.exitApp(); }
        System.delay(16);
        continue;
    }

    if (MODE === "pausa") {
        var bp = drawPausa();
        if (hit(bp.seguir)) { MODE = "jogando"; last = System.millis(); audio.sfx("ui"); }
        else if (hit(bp.sair)) { audio.stop(); System.exitApp(); }
        System.delay(16);
        continue;
    }

    if (MODE === "fim") {
        engine.update(dt, -1, fx, audio);   // so adianta overT/efeitos
        fx.update(dt, 0, 0);
        var bf = drawFim();
        if (engine.state().overT > 0.6) {
            if (hit(bf.deNovo)) enterJogo();
            else if (hit(bf.sair)) { enterTitulo(); audio.sfx("ui"); }
        }
        System.delay(16);
        continue;
    }

    // ------------------------------------------------------------ jogando
    audio.keepAlive(now);
    engine.musicPump(audio, now);
    drivePlayer(t);
    engine.update(dt, beatF, fx, audio);
    fx.update(dt, engine.state().intensity,
              beatF >= 0 ? Math.max(0, 1 - (beatF - Math.floor(beatF)) * 2.5) : 0);

    var s = engine.state();

    // detonar supernova: toque no medidor cheio
    if (tap && s.charge >= 100 &&
        Math.abs(tap.x - W / 2) < 48 && Math.abs(tap.y - (H - 30)) < 48) {
        engine.detonate(fx, audio);
    }

    // pausa: canto superior direito
    if (tap && tap.x > W - 64 && tap.y < HUD_H + 12) {
        MODE = "pausa";
        audio.stop();
        audio.sfx("ui");
        continue;
    }

    if (s.over) {
        salvarFim();
        MODE = "fim";
        continue;
    }

    drawGame(now, beatF);
    System.delay(1);
}
