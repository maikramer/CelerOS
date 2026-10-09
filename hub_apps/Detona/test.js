// test.js — smoke do Detona! no harness: titulo -> JOGAR -> bomba no beat
// -> explosao abre bloco macio -> labareda mata (fim de jogo) -> tela fim.
// Deterministico: a pega __harness.detona dirige a simulacao direto (a
// arena e gerada por seed) e os agendamentos usam o relogio virtual —
// a rodada inteira e sincrona dentro do E.run, o watchdog do delay
// encerra. No harness roda a via painter/pool de stub no canvas nativo
// 480x480, provando a degradacao graciosa sem assets reais.

module.exports.wire = function (env) {
    var h = env.__harness;
    var at = env.setTimeout;

    var origDelay = env.System.delay;
    var stopAt = 1000 + 16000;
    env.System.delay = function (ms) {
        if (env.System.millis() > stopAt) throw { harnessStop: true };
        return origDelay(ms);
    };

    function assert(cond, msg) {
        if (!cond) throw new Error('detona smoke: ' + msg);
    }

    function macios() {
        var s = h.detona.arena.state();
        var n = 0;
        for (var r = 0; r < 13; r++)
            for (var c = 0; c < 15; c++)
                if (s.grid[r][c] === '%') n++;
        return n;
    }

    var macios0 = 0, bombasAtivas = 0, explodiu = false, maciosDepois = -1;

    // titulo -> JOGAR (botao em H*0.62 + meia altura; 480 nativo)
    at(function () {
        h.pushTouch([{ x: 240, y: Math.floor(480 * 0.62 + 26), touched: 1 },
                     { x: 240, y: Math.floor(480 * 0.62 + 26), touched: 0 }]);
    }, 400);

    // entrou na fase: player no canto, grid com macios
    at(function () {
        var a = h.detona.arena, s = a.state();
        assert(s, 'estado da arena vivo');
        var cel = a.celulaPlayer();
        assert(cel.c === 1 && cel.r === 1, 'player nasce no canto (1,1)');
        macios0 = macios();
        assert(macios0 >= 8, 'arena com macios (' + macios0 + ')');
        assert(s.tLeft > 0 && s.fim === null, 'fase rodando');
        // planta a primeira bomba do canto
        bombasAtivas = a.plantar();
        assert(bombasAtivas === true, 'bomba plantada');
        assert(s.bombs.length === 1, 'bomba na lista');
    }, 1200);

    // ~3 batidas depois: bomba ainda viva, piscando
    at(function () {
        var a = h.detona.arena, s = a.state();
        assert(s.bombs.length === 1, 'pavio de 4 batidas ainda queimando');
    }, 2200);

    // depois do pavio (4 beats @128bpm = ~1.9s): explodiu, abriu macio
    // (labareda vive 0.6 beat ~ 280 ms — janela justa)
    at(function () {
        var a = h.detona.arena, s = a.state();
        assert(s.bombs.length === 0, 'bomba explodiu no fim do pavio');
        maciosDepois = macios();
        assert(maciosDepois < macios0, 'explosao abriu macio (' + macios0 + ' -> ' + maciosDepois + ')');
        assert(s.flames.length > 0, 'labaredas ativas');
        explodiu = true;
    }, 3220);

    // morte deterministica: player em cima da labareda da proxima bomba
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu) return;
        assert(s.stats.vidas >= 1, 'ainda vivo antes da labareda');
        s.stats.vidas = 0;              // deterministico: 1 toque mata
        s.stats.inv = 0;
        a.plantar();                    // segunda bomba no mesmo canto
    }, 4200);
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu) return;
        // espera o pavio + um pouco: a labareda do proprio canto pega o player
    }, 6400);
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu) return;
        // o onFim agenda parar()+goto fim 900 ms apos a morte: o estado
        // pode ja ter sido parado — o veredito vive em E.data.fimInfo
        var fim = s ? s.fim : (h.detona.E.data.fimInfo && h.detona.E.data.fimInfo.fim);
        assert(fim === 'dead', 'labareda matou (fim=dead), fim=' + fim);
    }, 7000);
};
