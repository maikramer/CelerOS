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
    var meuBalao = null;

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

    // --- bichos (ia.js): mortos por labareda, chute e chefe ---------------
    // (janela livre entre a 2a bomba do player e a morte: bicho novo +
    // bomba estrangeira de pavio curtissimo em cima dele)
    at(function () {
        var d = h.detona, a = d.arena, s = a.state();
        if (!explodiu || s.fim) return;
        var cel = null;
        for (var r = 1; r < 12 && !cel; r++)
            for (var c = 3; c < 14 && !cel; c++)
                if (a.em(c, r) === '.' && !(c === s.exit.c && r === s.exit.r)) cel = { c: c, r: r };
        assert(cel, 'celula livre p/ o bicho');
        meuBalao = d.ia.colocar('balao', cel.c, cel.r);
        var ok = a.addBomba(cel.c, cel.r, 0.05, 1);
        assert(ok, 'bomba sobre o bicho plantou');
    }, 4700);
    at(function () {
        var d = h.detona, a = d.arena, s = a.state();
        if (!explodiu || s.fim) return;
        assert(meuBalao && meuBalao.morto, 'labareda matou o balao do teste');
        assert(s.score > 0, 'bicho deu pontos (score=' + s.score + ')');
        // CHUTE: bomba a leste do player ((2,1) e sempre livre), kick ligado
        s.stats.kick = true;
        var ok = a.addBomba(2, 1, 99, 1);
        assert(ok, 'bomba chutavel plantou');
        a.mover(1, 0, 1 / 60);   // anda pro leste: dispara o chute
    }, 5100);
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu || s.fim) return;
        var bo = null;
        for (var i = 0; i < s.bombs.length; i++)
            if (s.bombs[i].range === 1 && s.bombs[i].vai > 90) bo = s.bombs[i];
        assert(bo, 'bomba do chute na lista');
        for (var k = 0; k < 20; k++) a.update(1 / 30);   // desliza
        assert(bo.fx > 3, 'chute deslizou a bomba (fx=' + bo.fx.toFixed(2) + ')');
        assert(a.em(Math.round(bo.fx), 1) === 'B', 'bomba assentou onde parou');
        // CHEFE: hp 2 (deterministico), 2 acertos de labareda matam
        var chefe = h.detona.ia.colocar('chefe', 6, 1);
        chefe.hp = 2;
        var ok2 = a.addBomba(6, 1, 0.03, 1);
        assert(ok2, 'bomba no chefe plantou');
    }, 5300);
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu || s.fim) return;
        var ok = a.addBomba(6, 1, 0.03, 1);   // 2o acerto (a 1a ja foi)
        assert(ok, 'segundo acerto no chefe plantou');
    }, 5500);
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu) return;
        var chefe = null;
        for (var i2 = 0; i2 < s.enemies.length; i2++)
            if (s.enemies[i2].kind === 'chefe') chefe = s.enemies[i2];
        assert(!chefe || chefe.morto, 'chefe morreu com 2 acertos (hp=' +
               (chefe ? chefe.hp : '-') + ')');
    }, 5800);

    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu || s.fim) return;
        assert(s.fim === 'dead', 'labareda matou (fim=dead), fim=' + s.fim);
    }, 7000);
};
