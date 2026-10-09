// test.js — smoke do Supernova no harness: exercita titulo -> jogando ->
// arrasto -> MORTE (hit deterministico via pega __harness.supernova) ->
// fim de jogo -> de novo -> SUPERNOVA (detonacao) e para no watchdog.
// No harness roda a via procedural (sem createSprite/drawPNG) no canvas
// nativo do stub (480x480), o que prova justamente o caminho de
// degradacao graciosa sem assets/sprites.
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

    // y0 do menu = 480*0.42 = 202; JOGAR em y 298..350, x 150..330
    at(function () {
        h.pushTouch([{ x: 240, y: 320, touched: 1 }, { x: 240, y: 320, touched: 0 }]);
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

    // morte deterministica: 1 vida, tiro inimigo em cima da nave
    at(function () {
        var m = h.supernova;
        if (!m) return;
        var s = m.engine.state();
        s.lives = 1;
        s.player.invuln = 0;
        s.ebullets.push({ x: s.player.x, y: s.player.y, vx: 0, vy: 120 });
    }, 2200);

    // fim de jogo (overT > 0,6): DE NOVO em y 254..306, x 150..330
    at(function () {
        h.pushTouch([{ x: 240, y: 280, touched: 1 }, { x: 240, y: 280, touched: 0 }]);
    }, 3600);

    // carrega e detona a supernova (toque no medidor do rodape)
    at(function () {
        var m = h.supernova;
        if (m) m.engine.state().charge = 100;
    }, 4600);
    at(function () {
        h.pushTouch([{ x: 240, y: 450, touched: 1 }, { x: 240, y: 450, touched: 0 }]);
    }, 5200);

    // pausa (canto superior direito) e continua — cobre o ultimo estado
    at(function () {
        h.pushTouch([{ x: 440, y: 20, touched: 1 }, { x: 440, y: 20, touched: 0 }]);
    }, 6400);
    at(function () {
        h.pushTouch([{ x: 240, y: 240, touched: 1 }, { x: 240, y: 240, touched: 0 }]);
    }, 7200);
};
