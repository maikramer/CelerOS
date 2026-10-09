// Quica — jogo de exemplo da engine CelerOS (criado com `celer.js new --game`).
// Segure as bolas no ar com a raquete (arraste o dedo): cada rebatida vale
// 1 ponto, bolas novas entram a cada poucos segundos e 3 perdidas encerram.
//
// E um tour pela engine: cenas (titulo/jogo/fim), input (tap/drag), fisica
// (physics.js: gravidade, bounce, bordas), particulas, audio e recorde no
// NVS. Use como base do seu jogo — sprites PNG entram por E.spr.load().

var E = require("engine");
var P = require("physics");

E.init({ dir: "{{APP_NAME}}", fps: 30, save: "quica." });
var W = E.W, H = E.H;
var T = E.theme;

// --------------------------------------------- estado do jogo corrente ---
var g = {
    world: null, paddle: null, balls: [],
    score: 0, lives: 3, spawnIn: 0, newBest: false
};

function novaBola() {
    var b = g.world.add({
        x: E.m.rand(40, W - 40), y: 24, r: 7,
        vx: E.m.rand(-70, 70), vy: 60,
        bounce: 1,
        onCollide: function (me, other) {
            if (other !== g.paddle) return;
            g.score++;
            me.vy = -Math.abs(me.vy) - 40;   // sempre sai pra cima
            E.fx.burst(me.x, me.y, { n: 10, color: T.accent, speed: 90, life: 0.5 });
            E.fx.popText(me.x, me.y - 14, "+1", { color: T.accent });
            E.audio.sfx("coin");
        }
    });
    g.balls.push(b);
    return b;
}

function resetJogo() {
    g.world = P.world({ gravity: { x: 0, y: 220 },
                        bounds: { x: 0, y: 0, w: W, h: H + 40 },
                        walls: "contain" });
    g.paddle = g.world.add({ x: W / 2, y: H - 22, w: 64, h: 10, static: true });
    g.balls = [];
    g.score = 0;
    g.lives = 3;
    g.spawnIn = 4;
    g.newBest = false;
    novaBola();
}

// ------------------------------------------------------------- cenas -----
E.run({
    titulo: {
        enter: function () {
            E.cam.reset();
            this.t = 0;
        },
        update: function (dt) {
            this.t += dt;
            if (E.hit(this.btnJogar)) { E.audio.sfx("ok"); E.goto("jogo"); }
            else if (E.hit(this.btnSair)) System.exitApp();
        },
        draw: function () {
            System.fillScreen(T.bg);
            E.gfx.text("QUICA", W / 2, 64 + Math.sin(this.t * 2) * 6,
                       { size: 3, align: "center", color: T.accent });
            E.gfx.text("segure as bolas com a raquete", W / 2, 112,
                       { align: "center", color: T.textDim, font: 1 });
            this.btnJogar = E.gfx.button("JOGAR", W / 2 - 70, 156, 140, 44);
            this.btnSair = E.gfx.button("SAIR", W / 2 - 70, 212, 140, 36,
                                        { primary: false });
        }
    },

    jogo: {
        enter: function () {
            resetJogo();
        },
        update: function (dt) {
            // raquete segue o dedo (arraste em qualquer lugar da tela)
            if (E.input.down) {
                g.paddle.x = E.m.clamp(E.input.x, 36, W - 36);
            }
            // bolas novas entram cada vez mais rapido
            g.spawnIn -= dt;
            if (g.spawnIn <= 0 && g.balls.length < 4) {
                novaBola();
                g.spawnIn = Math.max(2.5, 6 - g.score * 0.15);
            }
            g.world.step(dt);
            // bola perdida: caiu alem da raquete
            for (var k = g.balls.length - 1; k >= 0; k--) {
                var b = g.balls[k];
                if (b.y <= H + 20) continue;
                g.world.remove(b);
                g.balls.splice(k, 1);
                g.lives--;
                E.audio.sfx("bad");
                E.cam.shake(4, 0.25);
                E.fx.flash(T.err, 120);
                if (g.lives <= 0) {
                    g.newBest = E.save.best("recorde", g.score);
                    E.goto("fim");
                    return;
                }
            }
        },
        draw: function () {
            System.fillScreen(T.bg);
            for (var k = 0; k < g.balls.length; k++) {
                var b = g.balls[k];
                E.gfx.circle(b.x, b.y, b.r, T.accent);
                E.gfx.circle(b.x - 2, b.y - 2, 2, T.onAccent);
            }
            E.gfx.rect(g.paddle.x - 32, g.paddle.y - 5, 64, 10, T.text, { r: 5 });
            E.gfx.text(String(g.score), 8, 6, { size: 2, color: T.text });
            for (var l = 0; l < g.lives; l++) {
                E.gfx.circle(W - 14 - l * 16, 14, 5, T.err);
            }
            E.fx.draw();
        }
    },

    fim: {
        enter: function () {
            this.t = 0;
        },
        update: function (dt) {
            this.t += dt;
            if (this.t < 0.4) return;   // engole o tap fantasma do fim
            if (E.hit(this.btnNovo)) { E.audio.sfx("ok"); E.goto("jogo"); }
            else if (E.hit(this.btnSair)) System.exitApp();
        },
        draw: function () {
            System.fillScreen(T.bg);
            E.gfx.panel(W / 2 - 90, 56, 180, 168);
            E.gfx.text("FIM DE JOGO", W / 2, 80,
                       { size: 2, align: "center", color: T.text });
            E.gfx.text(g.score + " rebatidas", W / 2, 112,
                       { align: "center", color: T.text });
            if (g.newBest) {
                E.gfx.text("NOVO RECORDE!", W / 2, 134,
                           { align: "center", color: T.warn, font: 1 });
            } else {
                E.gfx.text("recorde " + E.save.num("recorde", 0), W / 2, 134,
                           { align: "center", color: T.textDim, font: 1 });
            }
            this.btnNovo = E.gfx.button("DE NOVO", W / 2 - 70, 156, 140, 40);
            this.btnSair = E.gfx.button("SAIR", W / 2 - 70, 204, 140, 34,
                                        { primary: false });
        }
    }
}, "titulo");
