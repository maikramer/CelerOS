// Supernova 2.1 — atirador espacial construido sobre a game engine do SDK,
// tela cheia nos PIXELS NATIVOS do vidro (API 28; 480x480 no SmartDisplay
// 4"). A trilha chiptune segue sendo o metronomo (ondas caem na batida via
// beat clock) e a carga dos orbes detona a SUPERNOVA. Novo na 2.0:
// SENTINELA-MOR (chefe a cada 5 ondas, rajadas radiais na batida, barra de
// vida), combo x2..x5 por abates em serie, drones splitter, tiro triplo de
// premiacao e barra de chefe no HUD. A saida e pelo menu (System.exitApp):
// sem topbar.
//
// Modulos: main.js (cenas/visao/HUD) + jogo.js (simulacao na fisica). A
// engine e a fisica sao DEPS do hub (app.json "deps"; API 30): instaladas
// pela loja no cache /local/modules, uma copia por versao no aparelho —
// sem vendorizar dentro do jogo. Arte dos sprites gerada por IA.

var E = require("celeros.engine");
var jogo = require("jogo");

E.init({ dir: "Supernova", fps: 30, native: true, save: "supernova.", particles: 120 });
var W = E.W, H = E.H;
var T = E.theme;

var C = {
    espaco: 0x0000,
    ciano: 0x07FF, cianoD: 0x03EF, magenta: 0xF81F, laranja: 0xFD20,
    ouro: 0xFFE0, branco: 0xFFFF, verde: 0x07E0,
    cinza: System.mixColor(0x0000, 0xFFFF, 18)
};
var HUD_H = 44;
var hi = E.save.num("hi", 0);

// Pega de teste (so existe no harness): deixa o test.js dirigir a sim
// deterministicamente.
if (typeof __harness !== "undefined") {
    __harness.supernova = { jogo: jogo, E: E };
}

// ------------------------------------------------------------- sprites ---
// Pool de 4 slots (PSRAM): cada PNG e decodificado uma unica vez e o blit
// usa cor-chave. Sem assets (ou no harness) a engine cai no painter
// procedural — o jogo roda igual.
var BASES = ["/local/apps/Supernova/assets/", "/sd/apps/Supernova/assets/"];

function paintNave(w, h, x, y) {
    System.fillTriangle(x + w / 2, y + 2, x + 6, y + h - 8, x + w - 6, y + h - 8, C.ciano);
    System.fillTriangle(x + w / 2, y + 10, x + w / 2 - 12, y + h - 14, x + w / 2 + 12, y + h - 14, C.branco);
    System.fillCircle(x + w / 2, y + h / 2 + 2, 5, C.cianoD);
}
function paintDrone(w, h, x, y) {
    System.fillTriangle(x + w / 2, y + 4, x + 2, y + h - 6, x + w - 2, y + h - 6, C.laranja);
    System.fillCircle(x + w / 2, y + h / 2, 6, C.magenta);
}
function paintOlho(w, h, x, y) {
    System.fillCircle(x + w / 2, y + h / 2, w / 2 - 3, C.magenta);
    System.fillCircle(x + w / 2, y + h / 2, w / 3, C.branco);
    System.fillCircle(x + w / 2, y + h / 2, w / 6, 0x0000);
}
function paintOrbe(w, h, x, y) {
    System.fillCircle(x + w / 2, y + h / 2, w / 2 - 2, C.ouro);
    System.fillCircle(x + w / 2, y + h / 2, w / 4, C.branco);
}

E.spr.load([
    { name: "nave", file: "nave", w: 72, h: 72, paint: paintNave },
    { name: "inimigo", file: "inimigo", w: 56, h: 56, paint: paintDrone },
    { name: "olho", file: "olho", w: 56, h: 56, paint: paintOlho },
    { name: "orbe", file: "orbe", w: 44, h: 44, paint: paintOrbe }
], { bases: BASES });

// Campo de estrelas em 2 tons (guardado no E.data: sobrevive as cenas)
E.data.stars = E.fx.stars(Math.round(W / 8), {
    w: W, h: H, vy: 30,
    colors: [0x39E7, 0xC5F9]   // cinza-azulado longe, branco quente perto
});

// A arte do titulo (PNG) e desenhada UMA vez por entrada na cena; por
// frame so a metade de baixo repinta (drawPNG decodifica: nada de decode
// por quadro).
var tituloPath = null;
var TIT_Y0 = 0;
function drawTituloBase() {
    System.fillScreen(C.espaco);
    if (E.caps.png && !tituloPath) {
        for (var i = 0; i < BASES.length && !tituloPath; i++) {
            try { if (System.drawPNG(BASES[i] + "titulo.png", 0, 0)) tituloPath = BASES[i] + "titulo.png"; } catch (e) {}
        }
    } else if (tituloPath) {
        try { System.drawPNG(tituloPath, 0, 0); } catch (e) { tituloPath = null; }
    }
    TIT_Y0 = Math.round(H * 0.42);
    if (!tituloPath) {
        for (var j = 0; j < 60; j++) {
            System.drawPixel(Math.floor(Math.random() * W), Math.floor(Math.random() * H), C.cinza);
        }
    }
}

// fracao alta da batida (1 logo apos o tempo forte, decai ate o proximo)
function beatPulse() {
    var b = E.audio.beat();
    if (b < 0) return 0;
    return Math.max(0, 1 - (b - Math.floor(b)) * 3);
}

// ------------------------------------------------------------- visao -----

function drawOrb(o, shx, shy, pulse) {
    var x = Math.round(o.x + shx), y = Math.round(o.y + shy);
    var pulso = 0.5 + 0.5 * Math.sin(o.ph);
    System.fillSmoothCircle(x, y, 26 + pulso * 5 + pulse * 3,
                            System.mixColor(C.ouro, C.espaco, 55 - pulso * 25));
    E.spr.blit("orbe", x, y, { cx: true, cy: true, key: C.espaco });
}

function drawEnemy(e, shx, shy) {
    var x = Math.round(e.x + shx), y = Math.round(e.y + shy);
    if (e.kind === 2) {
        // SENTINELA-MOR: olho grande com aura, nucleo pela vida e
        // escudos orbitando; na fase rapida a aura fecha
        var fast = e.hp < e.hpMax / 2;
        System.fillSmoothCircle(x, y, 60 + Math.sin(System.millis() / 110) * 5,
                                System.mixColor(C.magenta, C.espaco, fast ? 35 : 55));
        E.spr.blit("olho", x, y, { cx: true, cy: true, key: C.espaco });
        var hpk = e.hp / e.hpMax;
        System.fillCircle(x, y, 8, hpk > 0.5 ? C.verde : (hpk > 0.25 ? C.ouro : 0xF800));
        var a = System.millis() / 300;
        System.fillCircle(x + Math.cos(a) * 52, y + Math.sin(a) * 52, 4, C.magenta);
        System.fillCircle(x - Math.cos(a) * 52, y - Math.sin(a) * 52, 4, C.magenta);
    } else if (e.kind === 4) {
        System.fillTriangle(x, y - 10, x - 9, y + 8, x + 9, y + 8, C.laranja);
    } else {
        E.spr.blit(e.kind === 1 ? "olho" : "inimigo", x, y, { cx: true, cy: true, key: C.espaco });
        // nucleo pulsante POR CIMA do blit: animacao sem slot extra
        if (e.kind === 1) {
            System.fillCircle(x, y, 4 + Math.sin(System.millis() / 130 + e.ph) * 2, C.verde);
        } else if (e.kind === 3) {
            System.fillCircle(x, y + 8, 4 + Math.sin(System.millis() / 70 + e.ph) * 2, C.ouro);
        } else {
            System.fillCircle(x, y + 8, 3 + Math.sin(System.millis() / 90 + e.ph) * 2, C.magenta);
        }
    }
    if (e.flashT > 0) {   // leva tiro: pisca branco
        System.fillSmoothCircle(x, y, e.kind === 2 ? 50 : 26,
                                System.mixColor(C.branco, C.espaco, 45));
    }
}

function drawPlayer(p, shx, shy) {
    if (p.invuln > 0 && Math.floor(System.millis() / 90) % 2 !== 0) return;
    var px = Math.round(p.x + shx), py = Math.round(p.y + shy);
    var fl = 10 + Math.sin(System.millis() / 40) * 5;
    System.fillSmoothCircle(px - 12, py + 40, 5, C.cianoD);
    System.fillSmoothCircle(px + 12, py + 40, 5, C.cianoD);
    System.fillTriangle(px - 9, py + 34, px + 9, py + 34, px, py + 34 + fl, C.ciano);
    E.spr.blit("nave", px, py, { cx: true, cy: true, key: C.espaco });
}

function drawGame(s, pulse) {
    var shx = E.cam.ox, shy = E.cam.oy;   // tremor vem da camera da engine
    var all = s.world.all;
    var i, b;
    for (i = 0; i < all.length; i++) {
        if (all[i].cat === 'orb') drawOrb(all[i], shx, shy, pulse);
    }
    for (i = 0; i < all.length; i++) {
        if (all[i].cat === 'enemy') drawEnemy(all[i], shx, shy);
    }
    if (!s.over || s.overT < 0.4) drawPlayer(s.player, shx, shy);
    for (i = 0; i < all.length; i++) {
        b = all[i];
        if (b.cat === 'pbullet') {
            System.drawFastVLine(Math.round(b.x + shx), Math.round(b.y + shy), 14, C.ciano);
            System.drawPixel(Math.round(b.x + shx), Math.round(b.y + shy + 15), C.branco);
        } else if (b.cat === 'ebullet') {
            System.fillCircle(Math.round(b.x + shx), Math.round(b.y + shy), 4, C.magenta);
            System.drawPixel(Math.round(b.x + shx), Math.round(b.y + shy), C.branco);
        }
    }
}

function drawHUD(s, pulse) {
    var btnBg = System.mixColor(C.ciano, C.espaco, 72);
    E.gfx.gradient(0, 0, W, HUD_H, T ? System.mixColor(T.bg, C.espaco, 45) : 0x0208,
                   C.espaco, { screen: true });
    System.drawFastHLine(0, HUD_H, W, C.cianoD);

    E.gfx.text(String(s.score), 14, 6, { size: 2, font: 4, color: C.branco, screen: true });
    E.gfx.text("rec " + hi, 14, 38, { font: 1, color: C.cinza, screen: true });
    E.gfx.text("onda " + s.wave, W - 14, 8, { align: "right", color: C.cinza, screen: true });
    // combo x2..x5 pulsa quando sobe
    if (s.mult > 1) {
        var pk = s.multPulse > 0 ? 0.5 + 0.5 * Math.sin(System.millis() / 60) : 0;
        E.gfx.text("x" + s.mult, W / 2, 8, { align: "center", size: 2, screen: true,
                   color: pk > 0.2 ? C.ouro : System.mixColor(C.ouro, C.espaco, 30) });
    }
    for (var l = 0; l < s.lives; l++) {
        System.fillTriangle(W - 20 - l * 22, 44, W - 28 - l * 22, 32, W - 12 - l * 22, 32, C.ciano);
    }
    System.fillRect(W - 40, 32, 6, 12, C.cinza);
    System.fillRect(W - 30, 32, 6, 12, C.cinza);

    if (s.boss) {
        E.gfx.bar(W / 2 - 110, HUD_H + 6, 220, 8, s.boss.hp / s.boss.hpMax,
                  { fg: C.magenta, screen: true });
        E.gfx.text("SENTINELA-MOR", W / 2, HUD_H + 17, { align: "center", font: 1,
                   color: C.magenta, screen: true });
    }

    // medidor da supernova no rodape (arco de carga + pulso no beat)
    var gx = W / 2, gy = H - 30;
    var cheio = s.charge >= 100;
    var pulso = cheio ? 0.5 + 0.5 * Math.sin(System.millis() / 110) : 0;
    System.fillSmoothCircle(gx, gy, 20 + pulso * 4 + pulse * 2,
                            System.mixColor(C.ouro, C.espaco, 45 - pulso * 30));
    E.gfx.arc(gx, gy, 12, 16, -90, -90 + Math.round(s.charge * 3.6), C.ouro, { screen: true });
    if (cheio) {
        E.gfx.text("SUPERNOVA", gx, gy - 34, { align: "center", font: 1, color: C.ouro, screen: true });
    }
    if (s.triple > 0) {
        E.gfx.text("TRIPLO " + Math.ceil(s.triple) + "s", gx, gy + 26,
                   { align: "center", font: 1, color: C.verde, screen: true });
    }
}

// ------------------------------------------------------------- cenas -----

E.run({
    titulo: {
        fps: 30,
        enter: function () {
            E.cam.reset();
            this.t = 0;
            drawTituloBase();
        },
        update: function () {
            if (E.hit(this.btnJogar)) {
                E.audio.sfx("ok");
                jogo.reset();
                E.goto("jogando");
            } else if (E.hit(this.btnSair)) {
                System.exitApp();
            }
        },
        draw: function () {
            // por frame so a metade de baixo (a arte do titulo fica viva)
            System.fillRect(0, TIT_Y0, W, H - TIT_Y0, C.espaco);
            E.gfx.gradient(0, TIT_Y0, W, H - TIT_Y0, C.espaco,
                           System.mixColor(C.espaco, C.branco, 4), { screen: true });
            E.gfx.text("SUPERNOVA", W / 2, TIT_Y0 + 30,
                       { size: 4, align: "center", color: C.ciano, screen: true });
            E.gfx.text("arraste para voar; ondas na batida da musica",
                       W / 2, TIT_Y0 + 62, { align: "center", font: 1, color: C.cinza, screen: true });
            E.gfx.text("colete orbes e detone a supernova",
                       W / 2, TIT_Y0 + 76, { align: "center", font: 1, color: C.cinza, screen: true });
            this.btnJogar = E.gfx.button("JOGAR", W / 2 - 90, TIT_Y0 + 96, 180, 52,
                                         { color: C.ciano, r: 12, screen: true });
            this.btnSair = E.gfx.button("SAIR", W / 2 - 90, TIT_Y0 + 160, 180, 44,
                                        { primary: false, bg: System.mixColor(C.ciano, C.espaco, 72),
                                          r: 12, screen: true });
        }
    },

    jogando: {
        fps: 0,   // sem teto: o frame vale o que a placa der
        enter: function () {
            E.cam.reset();
            if (!E.audio.playing() && !jogo.state().resumeAt) E.audio.music(jogo.SONG);
        },
        update: function (dt) {
            var s = jogo.state();
            if (E.input.down) jogo.movePlayer(E.input.dx);
            E.data.stars.update(dt * (1 + s.intensity * 0.12));
            jogo.update(dt);
            // detonar: toque no medidor cheio
            if (E.input.tap && s.charge >= 100 &&
                Math.abs(E.input.tap.x - W / 2) < 48 &&
                Math.abs(E.input.tap.y - (H - 30)) < 48) {
                jogo.detonate();
            }
            // pausa: canto superior direito
            if (E.input.tap && E.input.tap.x > W - 64 && E.input.tap.y < HUD_H + 12) {
                E.audio.sfx("ui");
                E.goto("pausa");
                return;
            }
            if (s.over && s.overT > 0.6) E.goto("fim");
        },
        draw: function () {
            var s = jogo.state();
            var pulse = beatPulse();
            E.data.stars.draw();
            drawGame(s, pulse);
            E.fx.draw();
            drawHUD(s, pulse);
        }
    },

    pausa: {
        fps: 30,
        enter: function () {
            E.audio.stop();
        },
        update: function () {
            if (E.hit(this.btnSeguir)) {
                E.audio.sfx("ui");
                E.goto("jogando");
            } else if (E.hit(this.btnSair)) {
                System.exitApp();
            }
        },
        draw: function () {
            System.fillScreen(System.mixColor(C.espaco, C.branco, 3));
            E.gfx.panel(W / 2 - 110, H / 2 - 130, 220, 300, { screen: true });
            E.gfx.text("PAUSA", W / 2, H / 2 - 96,
                       { size: 4, align: "center", color: C.branco, screen: true });
            this.btnSeguir = E.gfx.button("CONTINUAR", W / 2 - 90, H / 2 - 44, 180, 48,
                                          { color: C.ciano, r: 12, screen: true });
            this.btnSair = E.gfx.button("SAIR", W / 2 - 90, H / 2 + 84, 180, 44,
                                        { primary: false, bg: System.mixColor(C.ciano, C.espaco, 72),
                                          r: 12, screen: true });
        }
    },

    fim: {
        fps: 30,
        enter: function () {
            this.t = 0;
            this.newBest = E.save.best("hi", jogo.state().score);
            if (this.newBest) {
                hi = jogo.state().score;
                E.audio.sfx("record");
            }
        },
        update: function (dt) {
            this.t += dt;
            if (this.t < 0.5) return;   // engole o tap do momento da morte
            if (E.hit(this.btnNovo)) {
                E.audio.sfx("ok");
                jogo.reset();
                E.goto("jogando");
            } else if (E.hit(this.btnMenu)) {
                E.audio.sfx("ui");
                E.goto("titulo");
            }
        },
        draw: function () {
            var s = jogo.state();
            var y0 = Math.round(H * 0.18);
            System.fillScreen(C.espaco);
            E.data.stars.draw();
            E.gfx.panel(W / 2 - 130, y0, 260, Math.round(H * 0.4), { screen: true });
            E.gfx.text("NAVE PERDIDA", W / 2, y0 + 34,
                       { size: 4, align: "center", color: C.magenta, screen: true });
            E.gfx.text("pontuacao " + s.score, W / 2, y0 + 76,
                       { size: 2, align: "center", color: C.branco, screen: true });
            E.gfx.text("onda " + s.wave + "  |  " + s.kills + " abates",
                       W / 2, y0 + 106, { align: "center", font: 1, color: C.cinza, screen: true });
            if (this.newBest) {
                E.gfx.text("NOVO RECORDE!", W / 2, y0 + 126,
                           { align: "center", font: 1, color: C.ouro, screen: true });
            } else {
                E.gfx.text("recorde " + hi, W / 2, y0 + 126,
                           { align: "center", font: 1, color: C.cinza, screen: true });
            }
            this.btnNovo = E.gfx.button("DE NOVO", W / 2 - 90, y0 + 160, 180, 52,
                                        { color: C.ciano, r: 12, screen: true });
            this.btnMenu = E.gfx.button("MENU", W / 2 - 90, y0 + 224, 180, 44,
                                        { primary: false, bg: System.mixColor(C.ciano, C.espaco, 72),
                                          r: 12, screen: true });
        }
    }
}, "titulo");
