// test.js — smoke do Supernova 2.0 no harness: titulo -> jogando -> arrasto
// -> MORTE (hit deterministico via __harness.supernova.jogo) -> fim ->
// de novo -> SUPERNOVA (detonacao com carga furada) -> pausa -> continuar,
// parando no watchdog. No harness roda a via procedural (sem
// createSprite/drawPNG) no canvas nativo do stub (480x480), provando a
// degradacao graciosa sem assets/sprites — e a fisica da engine (corpos
// sensor) faz a deteccao de acertos.
//
// ATENCAO: os agendamentos usam env.setTimeout (relogio virtual do harness,
// disparado no delay/getTouch) — o setTimeout do Node nunca rodaria: a
// rodada inteira e sincrona.

module.exports.wire = function (env) {
    var h = env.__harness;
    var at = env.setTimeout;

    // watchdog do proprio teste (o runner so instala um com --frames/render)
    var origDelay = env.System.delay;
    var stopAt = 1000 + 9000;   // clock do harness comeca em 1000
    env.System.delay = function (ms) {
        if (env.System.millis() > stopAt) throw { harnessStop: true };
        return origDelay(ms);
    };

    // TIT_Y0 = 480*0.42 = 202; JOGAR em y 350..402, x 150..330
    at(function () {
        h.pushTouch([{ x: 240, y: 375, touched: 1 }, { x: 240, y: 375, touched: 0 }]);
    }, 400);

    // arrasto na jogatina (a nave desvia e esquenta o motor)
    at(function () {
        h.pushTouch([
            { x: 180, y: 400, touched: 1 },
            { x: 220, y: 400, touched: 1 },
            { x: 260, y: 400, touched: 1 },
            { x: 300, y: 402, touched: 1 },
            { x: 300, y: 402, touched: 0 }
        ]);
    }, 800);

    // morte deterministica: 1 vida + tiro inimigo em cima da nave
    at(function () {
        var m = h.supernova;
        if (!m) return;
        var s = m.jogo.state();
        s.lives = 1;
        s.player.invuln = 0;
        m.jogo.debugHit();
    }, 2200);

    // fim de jogo (t > 0,5): DE NOVO em y 246..298, x 150..330
    at(function () {
        h.pushTouch([{ x: 240, y: 272, touched: 1 }, { x: 240, y: 272, touched: 0 }]);
    }, 3600);

    // carrega e detona a supernova (toque no medidor do rodape)
    at(function () {
        var m = h.supernova;
        if (m) m.jogo.state().charge = 100;
    }, 4600);
    at(function () {
        h.pushTouch([{ x: 240, y: 450, touched: 1 }, { x: 240, y: 450, touched: 0 }]);
    }, 5200);

    // pausa (canto superior direito) e continua — cobre o ultimo estado
    at(function () {
        h.pushTouch([{ x: 440, y: 20, touched: 1 }, { x: 440, y: 20, touched: 0 }]);
    }, 6400);
    at(function () {
        h.pushTouch([{ x: 240, y: 220, touched: 1 }, { x: 240, y: 220, touched: 0 }]);
    }, 7200);
};
