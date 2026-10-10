// test.js — smoke do Detona! no harness: titulo -> JOGAR -> bomba no beat
// -> explosao abre bloco macio -> labareda mata (fim de jogo) -> tela fim.
// Deterministico: a pega __harness.detona dirige a simulacao direto (a
// arena e gerada por seed) e os agendamentos usam o relogio virtual —
// a rodada inteira e sincrona dentro do E.run, o watchdog do delay
// encerra. No harness roda a via painter/pool de stub no canvas nativo
// 480x480, provando a degradacao graciosa sem assets reais. No fim, o
// protocolo do DUELO pela malha sem radio: a pega injeta as mensagens
// (netIn) e a fila tx guarda o que iria ao ar.

module.exports.wire = function (env) {
    var h = env.__harness;

    // fila propria de agendamentos no relogio virtual: o setTimeout do
    // harness segura so 8 timers pendentes e DESCARTAVA o resto calado (os
    // passos de chefe/morte/sobrevivencia nunca rodavam). Roda no delay,
    // contexto do app: um assert que falha vira r.err.
    var fila = [];
    function at(fn, ms) {
        fila.push({ t: env.System.millis() + ms, fn: fn });
        fila.sort(function (a, b) { return a.t - b.t; });
    }
    var origDelay = env.System.delay;
    var stopAt = 1000 + 21000;
    env.System.delay = function (ms) {
        while (fila.length && env.System.millis() >= fila[0].t) fila.shift().fn();
        if (env.System.millis() > stopAt) throw { harnessStop: true };
        return origDelay(ms);
    };
    function toque(x, y) {
        h.pushTouch([{ x: x, y: y, touched: 1 }, { x: x, y: y, touched: 0 }]);
    }
    function cena(nome) {
        var E = h.detona && h.detona.E;
        assert(E && E.sceneName === nome, 'esperava a cena ' + nome + ', esta em ' + (E && E.sceneName));
    }

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
    var meuBalao = null, viuChamas = false, chefeCel = null, bomba2Cel = null;

    // titulo -> JOGAR (menu de 3 botoes no rodape, E.u 2x no 480: JOGAR em
    // y 238..292, DUELO em 312..366, SOBREVIVENCIA em 386..440)
    at(function () { cena('titulo'); toque(240, 265); }, 400);

    // entrou na fase: player no canto, grid com macios
    at(function () {
        cena('jogando');
        var a = h.detona.arena, s = a.state();
        assert(s, 'estado da arena vivo');
        assert(s.intro > 0, 'cartao da fase segura o inicio');
        assert(a.plantar() === false, 'durante o cartao nao planta');
        s.intro = 0;   // o teste nao espera o cartao
        var cel = a.celulaPlayer();
        assert(cel.c === 1 && cel.r === 1, 'player nasce no canto (1,1)');
        macios0 = macios();
        assert(macios0 >= 8, 'arena com macios (' + macios0 + ')');
        assert(s.tLeft > 0 && s.fim === null, 'fase rodando');
        // solvabilidade: a saida nasce alcancavel explodindo macios
        var fl = h.detona.GRID.flood(s.grid, 1, 1, function (ch) { return ch !== '#'; });
        assert(fl.ok[s.exit.r][s.exit.c] === 1, 'saida alcancavel pelo flood');
        // planta a primeira bomba do canto
        bombasAtivas = a.plantar();
        assert(bombasAtivas === true, 'bomba plantada');
        assert(s.bombs.length === 1, 'bomba na lista');
        // crava o pavio numa BATIDA INTEIRA: a explosao cai no tempo forte.
        // O relogio virtual do harness so anda nos timers — agendo a sonda
        // EXATAMENTE na batida alvo p/ o salto do clock cair dentro da
        // janela do tempo forte (no device o clock e fino e isso e gratis)
        s.bombs[0].vai = Math.floor(a.beatNow()) + 4;
        // tira o player da propria rajada (a bomba esta no pe dele): senao
        // o ferir() zera o combo — no jogo de verdade ninguem fica parado
        var fugir = null;
        for (var fc = 5; fc < 14 && !fugir; fc++)
            if (a.em(fc, 1) === '.') fugir = { c: fc, r: 1 };
        for (var fr = 3; fr < 12 && !fugir; fr++)
            if (a.em(1, fr) === '.') fugir = { c: 1, r: fr };
        assert(fugir, 'celula de fuga livre');
        s.player.x = (fugir.c + 0.5) * s.cell;
        s.player.y = (fugir.r + 0.5) * s.cell;
        var faltaMs = Math.round((s.bombs[0].vai - a.beatNow()) * (60000 / 128));
        at(function () {
            var st2 = h.detona.arena.state();
            if (st2 && st2.flames.length > 0) viuChamas = true;
        }, Math.max(60, faltaMs + 40));
    }, 1200);

    // ~3 batidas depois: bomba ainda viva, piscando
    at(function () {
        var a = h.detona.arena, s = a.state();
        assert(s.bombs.length === 1, 'pavio de 4 batidas ainda queimando');
    }, 2200);

    // depois do pavio cravado na batida inteira: explodiu, abriu macio,
    // labareda (0.6 beat ~ 280 ms — a origem do beat varia com o enter,
    // entao sondo dois instantes) e o COMBO do tempo forte
    at(function () {
        var a = h.detona.arena, s = a.state();
        assert(s.bombs.length === 0, 'bomba explodiu no fim do pavio');
        if (s.flames.length > 0) viuChamas = true;
        maciosDepois = macios();
        assert(maciosDepois < macios0, 'explosao abriu macio (' + macios0 + ' -> ' + maciosDepois + ')');
        assert(viuChamas, 'labaredas vistas na janela (sonda 2800/3000)');
        assert(s.combo >= 1 && s.mult >= 2, 'explosao no tempo forte encadeou combo (' +
               s.combo + 'x, mult ' + s.mult + ')');
        explodiu = true;
    }, 3000);

    // morte deterministica: player em cima da labareda da proxima bomba
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu) return;
        assert(s.stats.vidas >= 1, 'ainda vivo antes da labareda');
        s.stats.vidas = 0;              // deterministico: 1 toque mata
        s.stats.inv = 0;
        bomba2Cel = a.celulaPlayer();
        a.plantar();                    // segunda bomba no pe do player
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
        var ok = a.addBomba(cel.c, cel.r, 0.05, 1, false, 'p');
        assert(ok, 'bomba sobre o bicho plantou');
    }, 4700);
    at(function () {
        var d = h.detona, a = d.arena, s = a.state();
        if (!explodiu || s.fim) return;
        assert(meuBalao && meuBalao.morto, 'labareda matou o balao do teste');
        assert(s.score > 0, 'bicho deu pontos (score=' + s.score + ')');
        // CHUTE: bomba a leste do player ((2,1) e sempre livre), kick ligado
        // — o player volta ao canto (1,1): ele tinha fugido da 1a rajada
        s.stats.kick = true;
        s.player.x = 1.5 * s.cell;
        s.player.y = 1.5 * s.cell;
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
        // desliza ate a celula ANTES da proxima ocupada (a 1a rajada, alcance
        // 2 a partir de (1,1), so abriu ate (3,1))
        assert(bo.fx >= 3 && a.em(Math.round(bo.fx) + 1, 1) !== '.',
               'chute deslizou a bomba ate o obstaculo (fx=' + bo.fx.toFixed(2) + ')');
        assert(a.em(Math.round(bo.fx), 1) === 'B', 'bomba assentou onde parou');
        // o player volta para cima da 2a bomba: a labareda dela fecha a fase
        s.player.x = (bomba2Cel.c + 0.5) * s.cell;
        s.player.y = (bomba2Cel.r + 0.5) * s.cell;
        // CHEFE: hp 2 (deterministico), 2 acertos de labareda matam — numa
        // celula livre de verdade (as bombas anteriores ocupam a linha 1)
        chefeCel = null;
        for (var cr = 1; cr < 12 && !chefeCel; cr++)
            for (var cc = 3; cc < 14 && !chefeCel; cc++)
                if (a.em(cc, cr) === '.' && !(cc === s.exit.c && cr === s.exit.r)) chefeCel = { c: cc, r: cr };
        assert(chefeCel, 'celula livre p/ o chefe');
        var chefe = h.detona.ia.colocar('chefe', chefeCel.c, chefeCel.r);
        chefe.hp = 2;
        chefe.vel = 0;   // parado: os acertos do teste caem sempre no corpo
        var ok2 = a.addBomba(chefeCel.c, chefeCel.r, 0.03, 1, false, 'p');
        assert(ok2, 'bomba no chefe plantou');
    }, 5300);
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu || s.fim) return;
        // invencivel logo apos o golpe: a 2a labareda colada nao conta
        var ok = a.addBomba(chefeCel.c, chefeCel.r, 0.03, 1, false, 'p');
        assert(ok, 'bomba colada no chefe plantou');
    }, 5450);
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu || s.fim) return;
        var chefe = null;
        for (var i3 = 0; i3 < s.enemies.length; i3++)
            if (s.enemies[i3].kind === 'chefe') chefe = s.enemies[i3];
        assert(chefe && !chefe.morto && chefe.hp === 1,
               'chefe perdeu 1 hp so (i-frames; hp=' + (chefe ? chefe.hp : '-') + ')');
        var ok = a.addBomba(chefeCel.c, chefeCel.r, 0.03, 1, false, 'p');   // 2o acerto valido
        assert(ok, 'segundo acerto no chefe plantou');
    }, 6200);
    at(function () {
        var a = h.detona.arena, s = a.state();
        if (!explodiu) return;
        var chefe = null;
        for (var i2 = 0; i2 < s.enemies.length; i2++)
            if (s.enemies[i2].kind === 'chefe') chefe = s.enemies[i2];
        assert(!chefe || chefe.morto, 'chefe morreu com 2 acertos (hp=' +
               (chefe ? chefe.hp : '-') + ')');
    }, 6500);

    // a labareda da 2a bomba fechou a fase: arena parada e tela de fim
    at(function () {
        if (!explodiu) return;
        var info = h.detona.E.data.fimInfo;
        assert(info && info.fim === 'dead', 'labareda matou (fim=dead), fim=' + (info && info.fim));
        cena('fim');
    }, 8000);

    // --- sobrevivencia: MENU no fim -> SOBREVIVENCIA no titulo -> onda 1 --
    // (fim: botoes em y 316..370 e 390..444)
    at(function () { cena('fim'); toque(240, 417); }, 8600);
    at(function () { cena('titulo'); toque(240, 413); }, 9400);
    at(function () {
        cena('jogando');
        var a = h.detona.arena, s = a.state();
        assert(s && s.sobrevivencia, 'modo sobrevivencia ativo');
        assert(s.onda >= 1, 'onda inicial spawned (' + s.onda + ')');
        assert(s.enemies.length >= 2, 'bichos na arena (' + s.enemies.length + ')');
        assert(s.exit.c < 0, 'sobrevivencia sem saida');
    }, 10200);

    // --- duelo: o protocolo da malha SEM radio — a pega injeta as msgs ---
    at(function () {
        var d = h.detona, NET = d.net;
        NET.rival = { id: 'BEEF', nome: 'Celer-B' };
        NET.host = false;
        NET.round = 0; NET.ganhos = 0; NET.perdidos = 0; NET.res = null;
        NET.fase = "aguarda";
        NET.tx.length = 0;
        // host inicia o round 1 com a seed
        d.netIn({ from: 'BEEF', fromName: 'Celer-B', msg: 'ds1234,1' });
        assert(NET.round === 1 && NET.seed === 1234, 'round 1 com a seed do host');
        var temDy = false;
        for (var i = 0; i < NET.tx.length; i++) if (NET.tx[i] === 'dy1') temDy = true;
        assert(temDy, 'ack dy1 do inicio foi ao ar');
    }, 11000);
    at(function () {
        var d = h.detona, a = d.arena, s = a.state(), NET = d.net;
        cena('jogando');
        assert(s && s.duelo, 'arena em modo duelo');
        assert(NET.fase === 'jogando', 'fase jogando');
        var cel = a.celulaPlayer();
        assert(cel.c === 13 && cel.r === 11, 'convidado nasce no canto oposto (13,11)');
        assert(s.rival && s.rival.tc === 1 && s.rival.tr === 1, 'rival espelhado no (1,1)');
        assert(s.enemies.length === 0 && s.exit.c < 0, 'duelo sem bichos e sem saida');
        s.intro = 0;
        // posicao do rival + bomba remota + powerup pego + duplicata de seq
        d.netIn({ from: 'BEEF', msg: 'dp1,12,10' });
        assert(s.rival.tc === 12 && s.rival.tr === 10, 'rival andou para (12,10)');
        var livre = null;
        for (var r2 = 4; r2 < 10 && !livre; r2++)
            for (var c2 = 4; c2 < 11 && !livre; c2++)
                if (a.em(c2, r2) === '.') livre = { c: c2, r: r2 };
        assert(livre, 'celula livre p/ a bomba do rival');
        d.netIn({ from: 'BEEF', msg: 'db2,' + livre.c + ',' + livre.r + ',52,3' });
        var bombaRival = null;
        for (var b2 = 0; b2 < s.bombs.length; b2++)
            if (s.bombs[b2].dono === 'r') bombaRival = s.bombs[b2];
        assert(bombaRival && a.em(livre.c, livre.r) === 'B', 'bomba do rival na grade');
        assert(bombaRival.range === 3, 'alcance da bomba remota veio na msg');
        d.netIn({ from: 'BEEF', msg: 'dp1,1,1' });   // seq 1 <= 2: barrado
        assert(s.rival.tc === 12, 'posicao velha (seq) barrada');
        var chave = null;
        for (var k in s.powerups) { chave = k; break; }
        if (chave) {
            var pc = chave.split(',');
            d.netIn({ from: 'BEEF', msg: 'dg3,' + pc[0] + ',' + pc[1] });
            assert(!s.powerups[chave], 'powerup pego pelo rival sumiu no meu mundo');
        }
        // morro na labareda DELE: em cima da bomba remota
        s.stats.inv = 0;
        s.player.x = (livre.c + 0.5) * s.cell;
        s.player.y = (livre.r + 0.5) * s.cell;
        bombaRival.vai = a.beatNow() + 0.05;
    }, 11400);
    at(function () {
        var d = h.detona, NET = d.net;
        var temDm = false;
        for (var i = 0; i < NET.tx.length; i++) if (/^dm/.test(NET.tx[i])) temDm = true;
        assert(temDm && NET.res === 'perdi', 'minha morte foi avisada (dm) e res=perdi');
        cena('round');
        // o rival morreu junto (dm tardio): derrota vira EMPATE
        d.netIn({ from: 'BEEF', msg: 'dm4' });
        assert(NET.res === 'empate', 'dm tardio do rival = empate');
    }, 13100);
};
