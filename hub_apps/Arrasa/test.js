// test.js — bateria do Arrasa! no harness (verlet nativo via stub, mesmo
// determinismo do firmware). Dirige o jogo pelos toques e a pega
// __harness.arrasa.F:
//   1. titulo -> JOGAR
//   2. estabilidade: 10 s de mundo rodando e o castelo NAO pode derreter
//      sozinho (nenhum vinculo rompido, coroa de pe)
//   3. arremesso de verdade: puxao maximo atras e baixo, solta — os
//      elasticos sao cortados no plano da forquilha, a pedra voa forte,
//      o castelo apanha (vinculos rompidos) e a COROA CAIU
//   4. cena de fim (vitoria) + orcamento dentro dos tetos
//   5. tensao direta: puxao lateral num ponto do topo rompe vinculos

module.exports.wire = function (env) {
    var h = env.__harness;

    // fila propria de agendamentos no relogio virtual (padrao Detona:
    // o setTimeout do harness segura so 8 timers e descarta o resto)
    var fila = [];
    function at(fn, ms) {
        fila.push({ t: env.System.millis() + ms, fn: fn });
        fila.sort(function (a, b) { return a.t - b.t; });
    }
    var origDelay = env.System.delay;
    var stopAt = 1000 + 30000;
    env.System.delay = function (ms) {
        while (fila.length && env.System.millis() >= fila[0].t) fila.shift().fn();
        if (env.System.millis() > stopAt) throw { harnessStop: true };
        return origDelay(ms);
    };
    function toque(x, y) {
        h.pushTouch([{ x: x, y: y, touched: 1 }, { x: x, y: y, touched: 0 }]);
    }
    function cena(nome) {
        var E = h.arrasa && h.arrasa.E;
        assert(E && E.sceneName === nome,
               'esperava a cena ' + nome + ', esta em ' + (E && E.sceneName));
    }
    function assert(cond, msg) {
        if (!cond) throw new Error('arrasa: ' + msg);
    }

    var sticks0 = 0, count0 = 0;
    var puxo = { p: -1, x: 0, y: 0 };   // estado do puxao direto (passo 5)

    // 1. titulo -> JOGAR (botao no rodape do 480 nativo)
    at(function () {
        cena('titulo');
        toque(240, 350);
    }, 400);

    // 2. entrou: castelo montado, pedra armada; 10 s de estabilidade.

    // (700 ms depois do toque — 2 quadros: o pushTouch so e lido pelo
    // app no frame SEGUINTE; dois at() no mesmo t executam no mesmo
    // while e o assert via a cena de antes)
    at(function () {
        cena('jogando');
        var F = h.arrasa.F;
        assert(F.mundo && F.count() === 54, 'mundo montado (2 garfos + 42 blocos + 6 coroa + 4 pedra = ' + F.count() + ')');
        assert(F.pedra && F.pedra.estado === 'pronta', 'pedra armada no estilingue');
        assert(F.blocos.length === 8, 'castelo de 7 blocos + coroa (' + F.blocos.length + ')');
        assert(F.coroaB, 'coroa montada');
        sticks0 = F.sticks();
        count0 = F.count();
    }, 700);
    at(function () {
        var F = h.arrasa.F;
        assert(F.rompidos === 0,
               'castelo derreteu sozinho: ' + F.rompidos + ' vinculos em 10 s');
        assert(!F.coroaCaiu, 'coroa caiu sozinha');
        assert(F.pedra && F.pedra.estado === 'pronta', 'pedra continua armada');
    }, 10600);



    // 3. arremesso: pega perto do bercо, estica a tracao maxima quase
    // horizontal com leve componente para baixo (mira balistica no meio da
    // torre: origem ~(35,198), v~(12,2.7) por step, g=0.3) e solta.
    // Tracao SUSTENTADA por 3 frames (o jogo chama arrastando a cada frame)
    at(function () {
        var F = h.arrasa.F;
        assert(F.puxar(20, 190) === true, 'puxao pegou a pedra');
        F.arrastando(9, 187);
    }, 11000);
    at(function () { h.arrasa.F.arrastando(9, 187); }, 11033);
    at(function () { h.arrasa.F.arrastando(9, 187); }, 11066);
    at(function () {
        var F = h.arrasa.F;
        F.arrastando(9, 187);
        F.soltar();
    }, 11099);
    at(function () {
        var F = h.arrasa.F;
        assert(F.lancamento && F.lancamento.vx > 3,
               'pedra transferida pro mundo A com velocidade (vx=' +
               (F.lancamento ? F.lancamento.vx.toFixed(1) : '?') + ')');
        assert(F.pedra.local === 'A', 'pedra voando no mundo A');
    }, 11600);

    // 3b. o castelo apanha e a coroa cai
    at(function () {
        var F = h.arrasa.F;
        assert(F.rompidos > 4, 'castelo apanhou de verdade (' + F.rompidos + ' vinculos)');
        assert(F.coroaCaiu, 'a coroa caiu com 1 pedra forte');
    }, 15600);

    // 4. cena de fim (vitoria: coroa caiu) + orcamento
    at(function () {
        cena('fim');
        var F = h.arrasa.F;
        assert(F.count() <= 220 && F.sticks() <= 640,
               'orcamento: ' + F.count() + ' pts / ' + F.sticks() + ' vinculos');
    }, 19000);

    // 5. orcamento: consome as 5 pedras e confere os tetos do mundo A
    at(function () {
        var F = h.arrasa.F;
        F.init(null, 5);
        F.steps = 200;
        for (var k = 0; k < 5; k++) {
            F.tracao = { vx: 5, vy: 0 };
            F.pedra.estado = 'lancando';
            F.pedra.steps = 999;            // forca o corte por timeout
            F.step();                        // transfere pro mundo A voando
            assert(F.pedra.local === 'A' && F.pedra.estado === 'voando',
                   'pedra ' + (k + 1) + ' transferida pro mundo A');
            var guard = 600;                 // deixa a fisica parar a pedra
            while (F.pedra.estado === 'voando' && guard-- > 0) F.step();
            assert(F.pedra.estado === 'entulho', 'pedra ' + (k + 1) + ' virou entulho (guard ' + guard + ')');
            if (k < 4) assert(F.arMar() === true, 'armou a pedra ' + (k + 2) + ' de 5');
            else assert(F.arMar() === false, 'sem pedras alem da 5');
        }
        assert(F.mundo.count() <= 220 && F.mundoB.count() <= 220, 'teto de pontos: A ' +
               F.mundo.count() + ' / B ' + F.mundoB.count());
        assert(F.sticks() <= 640, 'teto de vinculos: ' + F.sticks());
    }, 19800);

    at(function () {
        env.System.print('[arrasa] bateria completa');
        throw { harnessStop: true };
    }, 21500);
};
