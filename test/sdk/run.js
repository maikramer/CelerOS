#!/usr/bin/env node
// Testes do SDK (tools/sdk): scaffold passa no lint, types sem drift,
// renderer com pixels conferidos, emu gera PNG valido, icon do scaffold e PNG.
// Uso: node test/sdk/run.js

'use strict';
var fs = require('fs');
var os = require('os');
var path = require('path');

var ROOT = path.resolve(__dirname, '..', '..');
var failures = 0;

function check(name, ok, detail) {
    console.log((ok ? '  PASS  ' : '  FAIL  ') + name + (ok || !detail ? '' : ' — ' + detail));
    if (!ok) failures++;
}

// ---------------------------------------------------------------- scaffold --
(function () {
    console.log('SDK scaffold:');
    var { makeIconPng } = require('../../tools/sdk/celer.js');
    var { runLint } = require('../../tools/app_lint/lint.js');
    var { execSync } = require('child_process');

    var tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'celer-sdk-'));
    var appDir = path.join(tmp, 'AppTeste');
    execSync('node tools/sdk/celer.js new AppTeste --dir ' + JSON.stringify(tmp), { cwd: ROOT });

    for (var i = 0; i < ['app.json', 'main.js', 'icon.png', 'jsconfig.json', 'celer.d.ts', 'README.md'].length; i++);
    var want = ['app.json', 'main.js', 'icon.png', 'jsconfig.json', 'celer.d.ts', 'README.md'];
    var missing = want.filter(function (f) { return !fs.existsSync(path.join(appDir, f)); });
    check('cria os arquivos do app', missing.length === 0, 'faltam: ' + missing.join(', '));

    var icon = fs.readFileSync(path.join(appDir, 'icon.png'));
    check('icon.png e PNG valido (assinatura + IHDR 64x64)',
          icon.length > 33 && icon[0] === 0x89 && icon[1] === 0x50 &&
          icon.readUInt32BE(16) === 64 && icon.readUInt32BE(20) === 64);

    var result = runLint([appDir], {});
    var errs = result.totals.errors;
    check('scaffold passa limpo no lint', errs === 0,
          result.apps[0].diagnostics.map(function (d) { return d.message; }).join('; '));
    check('app.json do scaffold tem api = firmware', JSON.parse(fs.readFileSync(path.join(appDir, 'app.json'))).api === result.manifest.apiLevel);
    fs.rmSync(tmp, { recursive: true, force: true });
})();

// ------------------------------------------------------------------ types --
(function () {
    console.log('SDK types:');
    var { generate } = require('../../tools/sdk/lib/dts.js');
    var typesPath = path.join(ROOT, 'tools', 'sdk', 'types', 'celer.d.ts');
    var committed = fs.existsSync(typesPath) ? fs.readFileSync(typesPath, 'utf8') : '';
    var fresh = generate() + '\n';
    check('celer.d.ts commitado = gerado (rode `celer.js types` se falhar)', committed === fresh);
    check('declara os 5 globals + tema + touch',
          /declare const System/.test(fresh) && /declare const Net/.test(fresh) &&
          /declare const FS/.test(fresh) && /CelerLink/.test(fresh) &&
          /interface CelerTheme/.test(fresh) && /interface TouchPoint/.test(fresh));
})();

// --------------------------------------------------------------- renderer --
(function () {
    console.log('SDK renderer:');
    var { Renderer, W, H } = require('../../tools/sdk/lib/renderer.js');

    var r = new Renderer();
    var T = { bg: 0x0000, fg: 0xFFFF, red: 0xF800, accent: 0x07FF };
    r.fb.fill(T.bg);
    r.rect(10, 10, 50, 30, T.red, true);
    check('fillRect pinta o interior', r.fb[10 * W + 11] === T.red && r.fb[39 * W + 59] === T.red);
    check('fillRect respeita o bound (fora fica bg)', r.fb[10 * W + 9] === T.bg && r.fb[40 * W + 10] === T.bg);

    r.line(0, 0, 10, 0, T.fg);
    check('drawLine horizontal', r.fb[5] === T.fg);

    r.circle(100, 100, 8, T.accent, true);
    check('fillCircle centro e borda', r.fb[100 * W + 100] === T.accent && r.fb[100 * W + 108] === T.accent);

    // sprite: cria, bind, desenha NELE, push para a tela
    var env = { System: { fillRect: function () {}, drawPNG: function () { return true; } } };
    r.wire(env.System ? env : env);  // wire espera env; aqui so exercita S.*
    env.System.createSprite(20, 20);
    check('createSprite nao direciona sem bind', r.target() === r.fb);
    env.System.bindSprite(true);
    env.System.fillRect(0, 0, 20, 20, T.red);
    check('bindSprite(true) direciona ao sprite', r.sprite.fb[0] === T.red && r.fb[0] !== T.red);
    env.System.bindSprite(false);
    env.System.pushSprite(30, 30);
    check('pushSprite copia para o framebuffer', r.fb[30 * W + 30] === T.red && r.fb[30 * W + 49] === T.red);
    env.System.deleteSprite();

    // texto: glifo 'A' (0x41) com fg
    r.fg = T.fg; r.bg = T.bg; r.textSize = 1;
    r.drawString('A', 5, 5, 1);
    var anyOn = false;
    for (var i = 0; i < 8; i++) if (r.fb[5 * W + 5 + i] === T.fg || r.fb[(5 + i) * W + 5] === T.fg) anyOn = true;
    check('drawString pinta pixels do glifo', anyOn);
    check('textWidth acompanha a metrica do draw', env.System.textWidth('ABCD', 1) === 4 * 6);

    var png = r.png();
    check('png() gera PNG 240x320 (IHDR)', png.length > 100 && png[0] === 0x89 &&
          png.readUInt32BE(16) === W && png.readUInt32BE(20) === H);
})();

// -------------------------------------------------------------------- emu --
(function () {
    console.log('SDK emu (dogfood com apps reais):');
    var { runAppFolder } = require('../../tools/sdk/lib/runner.js');

    var r = runAppFolder(path.join(ROOT, 'data', 'apps', 'Snake'), { render: true, stopAtMs: 800 });
    check('Snake roda limpo no emu', r.err === null, r.err);
    check('renderer sem primitivas nao renderizadas', r.renderer.unsupported.size === 0,
          Array.from(r.renderer.unsupported).join(', '));

    // tela nao pode ficar vazia (fillScreen do Snake deixa fundo do tema)
    var nonzero = 0;
    for (var i = 0; i < r.renderer.fb.length; i += 997) if (r.renderer.fb[i] !== 0) nonzero++;
    check('framebuffer do Snake nao esta zerado', nonzero > 0);

    var r2 = runAppFolder(path.join(ROOT, 'hub_apps', 'Cronometro'), { render: true, stopAtMs: 600 });
    check('Cronometro roda limpo no emu', r2.err === null, r2.err);

    // --frames: um PNG por marco do relogio do app + diff entre consecutivos
    // (Watchface redesenha quando o segundo vira — o relogio do harness anda)
    var r3 = runAppFolder(path.join(ROOT, 'data', 'apps', 'Watchface'),
                          { render: true, frames: [0, 1100, 2300] });
    check('Watchface roda limpo com frames', r3.err === null, r3.err);
    check('frames nos marcos pedidos',
          r3.frames.length === 3 && r3.frames[0].ms === 0 && r3.frames[2].ms === 2300,
          JSON.stringify(r3.frames.map(function (f) { return f.ms; })));
    check('cada frame tem PNG 240x320',
          r3.frames.every(function (f) { return f.png.length > 100 && f.fb.length === 240 * 320; }));
    var d01 = 0;
    for (var i = 0; i < r3.frames[0].fb.length; i++)
        if (r3.frames[0].fb[i] !== r3.frames[1].fb[i]) d01++;
    check('diff detecta o relogio andando (frame 0 -> 1100)', d01 > 0, 'pixels=' + d01);

    // Supernova 2.0 (engine do SDK + fisica): roda o smoke completo dele
    // (titulo -> morte -> fim -> de novo -> detonacao -> pausa) headless;
    // o watchdog e do proprio test.js do app
    var r4 = runAppFolder(path.join(ROOT, 'hub_apps', 'Supernova'));
    check('Supernova roda limpo no harness (smoke completo)', r4.err === null, r4.err);

    // Detona! (bomberman de grade): smoke completo no canvas nativo do
    // stub (titulo -> jogar -> bomba no beat -> explosao abre macio ->
    // morte por labareda -> fim) — watchdog do proprio test.js do app
    var r5 = runAppFolder(path.join(ROOT, 'hub_apps', 'Detona'));
    check('Detona roda limpo no harness (smoke completo)', r5.err === null, r5.err);
})();

// ---------------------------------------------------------------- physics --
(function () {
    console.log('SDK physics (tools/sdk/engine/celeros.physics.js):');
    var P = require('../../tools/sdk/engine/celeros.physics.js');

    check('exporta version', typeof P.version === 'string' && !!P.version);

    // P.hit: os tres pares de formas
    var box1 = { x: 100, y: 100, w: 20, h: 20 }, box2 = { x: 110, y: 110, w: 20, h: 20 };
    var boxFar = { x: 200, y: 200, w: 20, h: 20 };
    var ball1 = { x: 100, y: 100, r: 10 }, ball2 = { x: 115, y: 100, r: 10 };
    check('hit aabb-aabb detecta', P.hit(box1, box2) === true);
    check('hit aabb-aabb rejeita longe', P.hit(box1, boxFar) === false);
    check('hit circle-circle detecta', P.hit(ball1, ball2) === true);
    check('hit circle-aabb detecta', P.hit(ball1, box2) === true);
    check('hit circle-aabb rejeita longe', P.hit({ x: 50, y: 50, r: 5 }, boxFar) === false);

    // queda + repouso: bounce 0 assenta no chao com grounded e vy ~0
    var w = P.world({ gravity: { x: 0, y: 1000 },
                      bounds: { x: 0, y: 0, w: 240, h: 320 }, walls: 'contain' });
    var ball = w.add({ x: 120, y: 100, r: 8 });
    var floor = w.add({ x: 120, y: 310, w: 240, h: 20, static: true });
    for (var i = 0; i < 240; i++) w.step(1 / 60);
    check('corpo assenta no chao estatico', Math.abs(ball.y - 292) < 3,
          'y=' + ball.y.toFixed(1));
    check('grounded e vy quase nula no repouso',
          ball.grounded === true && Math.abs(ball.vy) < 10,
          'grounded=' + ball.grounded + ' vy=' + ball.vy.toFixed(2));

    // restituicao: bounce 1 conserva a energia (volta perto do topo)
    var w2 = P.world({ gravity: { x: 0, y: 1000 },
                       bounds: { x: 0, y: 0, w: 240, h: 320 }, walls: 'contain' });
    var jumper = w2.add({ x: 120, y: 100, r: 8, bounce: 1 });
    w2.add({ x: 120, y: 310, w: 240, h: 20, static: true });
    var minY = 999;
    for (var k = 0; k < 360; k++) {
        w2.step(1 / 60);
        if (jumper.y < minY) minY = jumper.y;
        if (k === 180 && jumper.vy < 0) check('bounce=1 sobe apos o quique', true);
    }
    check('bounce=1 volta perto da altura inicial', minY < 130, 'minY=' + minY.toFixed(1));

    // atrito: corpo deslizando no chao para
    var w3 = P.world({ gravity: { x: 0, y: 1000 },
                       bounds: { x: 0, y: 0, w: 240, h: 320 }, walls: 'contain' });
    var slider = w3.add({ x: 40, y: 292, r: 8, friction: 1, vx: 150 });
    w3.add({ x: 120, y: 310, w: 240, h: 20, static: true });
    for (var k2 = 0; k2 < 180; k2++) w3.step(1 / 60);
    check('atrito para o deslize (vx < 10)', Math.abs(slider.vx) < 10,
          'vx=' + slider.vx.toFixed(2));

    // anti-tunel: bala rapida nao atravessa parede fina (4px) — e atravessa
    // se maxSub for 1 (prova que o sub-passo e quem salva)
    function tunnelRun(maxSub) {
        var wt = P.world({ gravity: { x: 0, y: 0 }, maxSub: maxSub });
        var b = wt.add({ x: 20, y: 50, r: 2, vx: 1500 });
        wt.add({ x: 120, y: 50, w: 4, h: 100, static: true });
        for (var n = 0; n < 30; n++) wt.step(1 / 60);
        return b.x < 118;
    }
    check('bala rapida nao atravessa parede fina', tunnelRun(8) === true);
    check('sem sub-passo (maxSub=1) atravessa (sanidade)', tunnelRun(1) === false);

    // sensor: conta a travessia sem alterar o movimento
    var w4 = P.world({ gravity: { x: 0, y: 0 } });
    var hits = 0;
    var ghost = w4.add({ x: 50, y: 100, w: 40, h: 100, static: true, sensor: true,
                         onCollide: function () { hits++; } });
    var passThru = w4.add({ x: 20, y: 100, r: 4, vx: 60 });
    for (var n2 = 0; n2 < 60; n2++) w4.step(1 / 60);
    check('sensor dispara onCollide na travessia', hits >= 2, 'hits=' + hits);
    check('sensor nao resolve (corpo atravessa)', passThru.x > 70, 'x=' + passThru.x.toFixed(1));
    check('sem penetracao residual apos sensor', P.hit(ghost, passThru) === false);

    // grupos/mascaras: pares fora da mascara se atravessam
    var w5 = P.world({ gravity: { x: 0, y: 0 } });
    var ga = w5.add({ x: 100, y: 100, r: 10, group: 1, mask: 2 });       // so colide c/ grupo 2
    var gb = w5.add({ x: 105, y: 100, r: 10, group: 4, mask: 4 });       // mundo do grupo 4
    var startGap = Math.abs(ga.x - gb.x);
    for (var n3 = 0; n3 < 30; n3++) w5.step(1 / 60);
    check('mask/group desligado nao resolve (sobrepostos e nada acontece)',
          Math.abs(Math.abs(ga.x - gb.x) - startGap) < 0.5);
    var gc = w5.add({ x: 100, y: 130, r: 10, group: 2, mask: 1 });
    w5.step(1 / 60);
    check('mask/group ligado resolve (separam)', P.hit(ga, gc) === false ||
          Math.abs(ga.y - 130) > 10);

    // tilemap: plataforma solida e one-way
    var GRID = [
        '........',
        '..==....',
        '........',
        '####....',
        '........'
    ];
    var wt2 = P.world({ gravity: { x: 0, y: 1000 } });
    var tw2 = 16;
    wt2.addTiles(P.tiles(GRID, tw2, tw2));
    var hero = wt2.add({ x: 24, y: 8, w: 10, h: 12 });   // cai sobre '#' (linha 3, y=48)
    for (var n4 = 0; n4 < 120; n4++) wt2.step(1 / 60);
    check('platformer assenta no tile solido', hero.grounded && Math.abs(hero.y - 42) < 2,
          'y=' + hero.y.toFixed(1) + ' grounded=' + hero.grounded);
    // anda e cai da borda do plato (### termina na col 4)
    hero.vx = 60;
    for (var n5 = 0; n5 < 120; n5++) { hero.vx = 60; wt2.step(1 / 60); }
    check('saiu da borda do plato e caiu', hero.y > 60, 'y=' + hero.y.toFixed(1));
    // one-way: com drop=true atravessa e vai assentar no solido de baixo
    var drop = wt2.add({ x: 40, y: 8, w: 10, h: 12, drop: true });
    for (var n6 = 0; n6 < 120; n6++) wt2.step(1 / 60);
    check('drop=true atravessa o one-way (assenta no solido de baixo)',
          drop.grounded && Math.abs(drop.y - 42) < 2 && drop.y > 32, 'y=' + drop.y.toFixed(1));
    // one-way: subindo por baixo, passa (col 3, longe do drop assentado;
    // registra a altura minima alcancada)
    var riser = wt2.add({ x: 56, y: 44, w: 10, h: 12, vy: -200 });
    var riseMin = 999;
    for (var n7 = 0; n7 < 20; n7++) {
        wt2.step(1 / 60);
        if (riser.y < riseMin) riseMin = riser.y;
    }
    check('one-way deixa passar subindo', riseMin < 30, 'minY=' + riseMin.toFixed(1));
    check('tileAt le o grid', wt2.tiles.tileAt(40, 56) === '#' &&
          wt2.tiles.tileAt(40, 26) === '=' && wt2.tiles.tileAt(200, 8) === null);

    // top-down (gravity 0 + tiles): para na parede nas 4 direcoes e desliza
    // no eixo livre — a receita bomberman/zelda do guia
    var TD = [
        '#######',
        '#.....#',
        '#.##..#',
        '#.....#',
        '#..#..#',
        '#######'
    ];
    var wt3 = P.world({ gravity: { x: 0, y: 0 }, maxSub: 4 });
    wt3.addTiles(P.tiles(TD, 16, 16));
    var walker = wt3.add({ x: 24, y: 24, r: 5 });   // celula (1,1), livre
    // baixo: corredor da col 1 livre ate a borda (linha 5) — desliza reto
    for (var t1 = 0; t1 < 90; t1++) { walker.vy = 60; wt3.step(1 / 60); }
    check('top-down desce o corredor ate a parede de baixo',
          walker.y > 70 && walker.y < 78 && walker.x < 32,
          'x=' + walker.x.toFixed(1) + ' y=' + walker.y.toFixed(1));
    // direita: da celula (1,4) ate o pilar da col 3 (linha 4)
    walker.vy = 0;
    for (var t2 = 0; t2 < 60; t2++) { walker.vx = 80; wt3.step(1 / 60); }
    check('top-down para na parede a direita', walker.x > 40 && walker.x < 47,
          'x=' + walker.x.toFixed(1));
    // esquerda: ate a borda da col 0
    for (var t3 = 0; t3 < 60; t3++) { walker.vx = -80; wt3.step(1 / 60); }
    check('top-down para na borda a esquerda', walker.x > 18 && walker.x <= 21.01,
          'x=' + walker.x.toFixed(1));

    // P.flow: campo BFS do alvo — distancias contornam parede, next desce o
    // gradiente, barreira total fica inalcancavel, passable customiza
    var FL = [
        '#####',
        '#...#',
        '#.#.#',
        '#...#',
        '#####'
    ];
    var fl = P.tiles(FL, 16, 16);
    var flow = P.flow(fl, 1, 1);                    // alvo na celula (1,1)
    check('flow: origem dist 0', flow.dist(1, 1) === 0);
    check('flow: vizinha imediata dist 1', flow.dist(2, 1) === 1);
    // (3,3) -> (1,1): pela direita sao 4 passos contornando o pilar (2,2)
    check('flow: contorna o pilar (dist 4)', flow.dist(3, 3) === 4,
          'dist=' + flow.dist(3, 3));
    check('flow: parede e inalcancavel', flow.dist(2, 2) === Infinity);
    var n1 = flow.next(3, 3);
    check('flow: next desce o gradiente', n1 && flow.dist(n1.c, n1.r) === 3,
          n1 ? '-> (' + n1.c + ',' + n1.r + ')' : 'null');
    check('flow: next na origem e null', flow.next(1, 1) === null);
    check('flow: next fora do grid e null', flow.next(9, 9) === null);
    // seguindo next() chega ao alvo em dist passos
    var c = 3, r = 3, steps = 0, ok = true;
    while (steps < 20) {
        var st = flow.next(c, r);
        if (!st) break;
        c = st.c; r = st.r; steps++;
    }
    check('flow: trilha de next chega na origem', c === 1 && r === 1 && steps === 4,
          'fim (' + c + ',' + r + ') em ' + steps + ' passos');
    // determinismo: mesmo grid, mesmo campo
    var flow2 = P.flow(fl, 1, 1);
    var same = true;
    for (var fc = 0; fc < 5 && same; fc++)
        for (var fr = 0; fr < 5; fr++)
            if (flow.dist(fc, fr) !== flow2.dist(fc, fr)) { same = false; break; }
    check('flow: deterministico', same === true);
    // barreira total: alvo murado nao alcancava nada fora do muro
    var WALL = [
        '#####',
        '##.##',
        '#####',
        '#...#',
        '#####'
    ];
    var flowW = P.flow(P.tiles(WALL, 16, 16), 2, 1);
    check('flow: barreira total deixa inalcancavel', flowW.dist(1, 3) === Infinity);
    // passable custom: fantasma atravessa '%' (bloco macio) — o tilemap do
    // jogo declara '%' solido; o flow dele ignora e passa
    var GH = [
        '#####',
        '#.%.#',
        '#####'
    ];
    var ghT = P.tiles(GH, 16, 16, { solid: function (ch) { return ch === '#' || ch === '%'; } });
    var flowG1 = P.flow(ghT, 1, 1);
    check('flow: solido do tilemap vale (macio bloqueia)', flowG1.dist(3, 1) === Infinity);
    var flowG2 = P.flow(ghT, 1, 1, { passable: function (ch) { return ch !== '#'; } });
    check('flow: passable custom atravessa o macio (dist 2)', flowG2.dist(3, 1) === 2);

    // verlet: corda presa conserva o comprimento total
    var pts = [];
    for (var p = 0; p < 6; p++) pts.push({ x: 120, y: 20 + p * 10 });
    var sticksSpec = [];
    for (var q = 1; q < 6; q++) sticksSpec.push({ a: q - 1, b: q });
    var rope = P.verlet({ points: pts, sticks: sticksSpec, iterations: 4 });
    rope.pin(0);
    var len0 = 0;
    for (var s1 = 0; s1 < rope.sticks.length; s1++) len0 += rope.sticks[s1].len;
    for (var n8 = 0; n8 < 120; n8++) rope.step(1 / 60);
    var len1 = 0;
    for (var s2 = 0; s2 < rope.sticks.length; s2++) {
        var A = rope.points[rope.sticks[s2].a], B = rope.points[rope.sticks[s2].b];
        len1 += Math.sqrt((A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y));
    }
    check('corda de verlet conserva comprimento',
          Math.abs(len1 - len0) / len0 < 0.05, 'len ' + len0.toFixed(1) + ' -> ' + len1.toFixed(1));
    check('corda pendura abaixo do pino', rope.points[5].y > rope.points[0].y + 20);

    // determinismo: mesma cena, dois mundos, resultado identico
    function simSeed() {
        var wd = P.world({ gravity: { x: 0, y: 900 },
                           bounds: { x: 0, y: 0, w: 240, h: 320 }, walls: 'contain' });
        var b1 = wd.add({ x: 30, y: 30, r: 6, vx: 120, vy: -40, bounce: 0.7 });
        var b2 = wd.add({ x: 200, y: 60, r: 6, vx: -90, bounce: 0.5 });
        wd.add({ x: 120, y: 310, w: 240, h: 16, static: true });
        for (var n9 = 0; n9 < 200; n9++) wd.step(1 / 60);
        return [b1.x, b1.y, b1.vx, b2.x, b2.y].join(',');
    }
    check('duas rodadas identicas = mesmo estado (determinismo)',
          simSeed() === simSeed(), simSeed() + ' vs ' + simSeed());

    // wrap: corpo que sai pela direita entra pela esquerda
    var ww = P.world({ bounds: { x: 0, y: 0, w: 240, h: 320 }, walls: 'wrap' });
    var orb = ww.add({ x: 239, y: 100, r: 4, vx: 40 });
    for (var n10 = 0; n10 < 20; n10++) ww.step(1 / 60);
    check('walls wrap teletransporta pela borda', orb.x < 20, 'x=' + orb.x.toFixed(1));

    // sweep-and-prune: sensor encontra o par isolado no meio de 20 estaticos
    var ws = P.world({ gravity: { x: 0, y: 0 } });
    var hits6 = 0;
    var lone = ws.add({ x: 20, y: 200, r: 6, vx: 90, sensor: true,
                        onCollide: function () { hits6++; } });
    ws.add({ x: 80, y: 200, r: 6, static: true, sensor: true });
    for (var q2 = 0; q2 < 20; q2++) {
        ws.add({ x: 150 + (q2 % 10) * 12, y: 20 + Math.floor(q2 / 10) * 12,
                 r: 5, static: true, sensor: true });
    }
    for (var n11 = 0; n11 < 60; n11++) ws.step(1 / 60);
    check('sweep-and-prune: par isolado na multidao detecta e atravessa',
          hits6 >= 3 && hits6 <= 20 && lone.x > 80,
          'hits=' + hits6 + ' x=' + lone.x.toFixed(1));
})();

// ------------------------------------------------------------------- deps --
(function () {
    console.log('SDK deps (celeros.sfx + celeros.grid):');
    var SFX = require('../../tools/sdk/engine/celeros.sfx.js');
    var GRID = require('../../tools/sdk/engine/celeros.grid.js');

    // sfx
    check('sfx exporta version e tabela', typeof SFX.version === 'string' &&
          Array.isArray(SFX.tabela.boomPeq));
    var m = SFX.mel('coin');
    m[0][0] = 1;
    check('sfx.mel devolve copia (mutar nao suja a tabela)',
          SFX.tabela.coin[0][0] === 988, JSON.stringify(SFX.tabela.coin[0]));
    check('sfx.mel de nome unico vira melodia de 1 nota',
          SFX.mel('ui').length === 1 && SFX.mel('ui')[0][0] === 880);
    check('sfx.mel de nome inexistente e null', SFX.mel('nada') === null);
    var tr = SFX.transpor([[440, 100], [0, 50]], 12);
    check('sfx.transpor dobra a frequencia e preserva a pausa',
          tr[0][0] === 880 && tr[1][0] === 0, JSON.stringify(tr));
    check('sfx.tempo escala duracoes com piso de 15 ms',
          SFX.tempo([[440, 100]], 0.5)[0][1] === 50 &&
          SFX.tempo([[440, 10]], 0.5)[0][1] === 15);
    var tocou = null;
    var audioFake = { sfx: function (mel) { tocou = mel; } };
    SFX.via(audioFake, 'boomGra', { oitava: 1 });
    check('sfx.via toca variante (transposta) pelo audio',
          tocou && tocou.length === 3 && tocou[0][0] === 200,
          tocou ? JSON.stringify(tocou[0]) : 'null');
    check('sfx.via sem audio e no-op', SFX.via(null, 'ui') === false);

    // grid
    check('grid exporta version', typeof GRID.version === 'string');
    var ra = GRID.rng(7), rb = GRID.rng(7);
    check('grid.rng deterministico', ra() === rb() && ra() === rb());
    var ar = GRID.classica(15, 13, GRID.rng(101), { dens: 0.55 });
    check('grid.classica: borda e pilares nas pares',
          ar.grid[0][0] === '#' && ar.grid[12][14] === '#' && ar.grid[2][2] === '#');
    check('grid.classica: canto do spawn respira (protege + vizinhos)',
          ar.grid[1][1] === '.' && ar.grid[2][1] === '.' && ar.grid[1][2] === '.');
    check('grid.classica: lista de macios bate com o grid',
          GRID.contar(ar.grid, '%') === ar.macios.length && ar.macios.length > 0);
    var fr = GRID.flood(ar.grid, 1, 1, function (ch) { return ch !== '#'; });
    check('grid.flood: explodindo macios alcansa a maioria da arena',
          fr.quantos > 15 * 13 * 0.5, 'quantos=' + fr.quantos);
    var fl = GRID.flood(ar.grid, 1, 1, function (ch) { return ch === '.'; });
    check('grid.flood: so celulas livres alcanca menos',
          fl.quantos < fr.quantos, 'livres=' + fl.quantos);
    // escolheAlcancavel: bolso cercado por # e rejeitado em favor do livre
    var gsel = GRID.nova(7, 5, '#');
    for (var cc = 1; cc <= 5; cc++) gsel[1][cc] = '.';
    gsel[2][5] = '%';   // vizinho de (5,1): alcancavel
    // (3,3) segue '#': qualquer candidato la dentro e isolado
    var escolhido = GRID.escolheAlcancavel(gsel, [{ c: 3, r: 3 }, { c: 5, r: 2 }], 1, 1);
    check('grid.escolheAlcancavel pula o bolso isolado',
          escolhido && escolhido.c === 5 && escolhido.r === 2,
          escolhido ? escolhido.c + ',' + escolhido.r : 'null');
    check('grid.escolheAlcancavel sem opcao devolve o primeiro',
          GRID.escolheAlcancavel(gsel, [{ c: 3, r: 3 }], 1, 1).c === 3);
    var livre = GRID.celulaLivre(ar.grid, GRID.rng(3), function (c, r, ch) {
        return ch === '.' && c + r > 12;
    });
    check('grid.celulaLivre respeita o filtro',
          livre && ar.grid[livre.r][livre.c] === '.' && livre.c + livre.r > 12,
          livre ? livre.c + ',' + livre.r : 'null');
})();

// ----------------------------------------------------------------- engine --
(function () {
    console.log('SDK engine (tools/sdk/engine/celeros.engine.js):');
    var harness = require('../../test/js_harness/run.js');
    var ENGINE_DIR = path.join(ROOT, 'tools', 'sdk', 'engine');

    // roda um snippet com require("celeros.engine") real sobre o harness
    // (relogio virtual, touchQ); setup(env) roda antes do snippet
    function runEngine(code, setup) {
        var env = harness.makeEnv();
        var req = harness.makeRequire(ENGINE_DIR, env);
        var grabbed = {};
        env.__harness.grab = function (k, v) { grabbed[k] = v; };
        if (setup) setup(env);
        var fn = new Function('System', 'Storage', '__harness', 'require', code);
        fn(env.System, env.Storage, env.__harness, req);
        return { env: env, grabbed: grabbed, log: env.__harness.log };
    }

    // caps e init
    var r = runEngine(
        'var E = require("celeros.engine");' +
        '__harness.grab("caps", E.init({ fps: 30, save: "tst." }));' +
        '__harness.grab("wh", [E.W, E.H]);');
    check('init devolve caps com tela 240x320',
          r.grabbed.caps && r.grabbed.caps.w === 240 && r.grabbed.caps.h === 320 &&
          r.grabbed.wh[0] === 240 && r.grabbed.wh[1] === 320,
          JSON.stringify(r.grabbed));
    check('caps detecta sem PSRAM no host (getInfo do harness)',
          r.grabbed.caps.psram === false && r.grabbed.caps.speaker === true);

    // cenas: enter/update/draw/exit na ordem, goto e quit
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'var marks = [];' +
        'E.run({' +
        '  a: { enter: function () { marks.push("a.enter"); },' +
        '       update: function () { marks.push("a.update");' +
        '                             if (marks.length >= 4) E.goto("b"); },' +
        '       draw: function () { marks.push("a.draw"); },' +
        '       exit: function () { marks.push("a.exit"); } },' +
        '  b: { enter: function () { marks.push("b.enter"); E.quit(); } }' +
        '}, "a");' +
        '__harness.grab("marks", marks);');
    check('cenas: ciclo enter/update/draw/exit + goto + quit',
          JSON.stringify(r.grabbed.marks) === JSON.stringify(
              ['a.enter', 'a.update', 'a.draw', 'a.update', 'a.draw', 'a.exit', 'b.enter']),
          JSON.stringify(r.grabbed.marks));

    // input: tap, swipe e justDown sinteticos via touchQ
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'var ev = [];' +
        'E.run({ jogo: { update: function () {' +
        '  if (E.input.justDown) ev.push("down");' +
        '  if (E.input.tap) ev.push("tap@" + E.input.tap.x + "," + E.input.tap.y);' +
        '  if (E.input.swipe) ev.push("swipe:" + E.input.swipe.dir);' +
        '  if (ev.length >= 4) E.quit();' +
        '} } }, "jogo");' +
        '__harness.grab("ev", ev);',
        function (env) {
            env.__harness.pushTouch([{ x: 30, y: 40, touched: 1 }, { x: 30, y: 40, touched: 0 }]);
            env.__harness.swipe(120, 200, 30, 200);
        });
    check('input: justDown + tap com coords',
          r.grabbed.ev[0] === 'down' && r.grabbed.ev[1] === 'tap@30,40',
          JSON.stringify(r.grabbed.ev));
    check('input: swipe reconhece direcao',
          r.grabbed.ev[3] === 'swipe:left', JSON.stringify(r.grabbed.ev));

    // timers da engine: after e every (tickados pelo loop, sem setTimeout)
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'var got = [];' +
        'E.after(200, function () { got.push("after"); });' +
        'var id = E.every(100, function () { got.push("every"); E.cancel(id); });' +
        'E.run({ s: { update: function () {' +
        '  if (got.length >= 2) E.quit(); } } }, "s");' +
        '__harness.grab("got", got);');
    check('timers: after e every (com cancel) disparam no loop',
          r.grabbed.got.indexOf('after') >= 0 && r.grabbed.got.indexOf('every') >= 0,
          JSON.stringify(r.grabbed.got));

    // tween com easing linear chega no alvo e chama onDone
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'var obj = { x: 0, done: false };' +
        'E.tween(obj, { x: 100 }, 500, { ease: E.m.linear,' +
        '          onDone: function () { obj.done = true; } });' +
        'E.run({ s: { update: function () { if (obj.done) E.quit(); } } }, "s");' +
        '__harness.grab("x", obj.x);');
    check('tween chega no alvo com onDone', r.grabbed.x === 100,
          'x=' + r.grabbed.x);

    // grupo: swap-pop remove mortos; pool recicla
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'var g = E.group();' +
        'g.add({ update: function () { this.n = (this.n || 0) + 1; } });' +
        'g.add({ dead: true, update: function () { throw "nao devia rodar"; } });' +
        'g.update(0.016);' +
        '__harness.grab("count", g.count);' +
        'var p = E.pool(2, function () { return { v: 0, update: function () { this.v++; } }; });' +
        'var a = p.spawn(), b = p.spawn();' +
        'p.update(0.016); p.update(0.016);' +
        'var cheio = p.spawn();' +
        'a.dead = true;' +
        'var rec = p.spawn();' +
        '__harness.grab("pool", [a.v, b.v, !cheio, rec === a]);');
    check('grupo remove dead (swap-pop) e updatea vivos',
          r.grabbed.count === 1, 'count=' + r.grabbed.count);
    check('pool: lota, e spawn recicla o corpo morto',
          r.grabbed.pool[0] === 2 && r.grabbed.pool[1] === 2 &&
          r.grabbed.pool[2] === true && r.grabbed.pool[3] === true,
          JSON.stringify(r.grabbed.pool));

    // sprites sem API de sprite (placa pobre): painter e chamado no load e
    // no blit com x/y absolutos
    r = runEngine(
        'var E = require("celeros.engine"); E.init({ dir: "Teste" });' +
        'var painted = [];' +
        'E.spr.load([{ name: "heroi", file: "heroi", w: 8, h: 8,' +
        '  paint: function (w, h, x, y) { painted.push([w, h, x, y]); } }]);' +
        'E.spr.blit("heroi", 10, 20);' +
        'E.spr.blit("fantasma", 0, 0);' +
        '__harness.grab("spr", [E.spr.has("heroi"), painted.length, painted[0][2], painted[0][3]]);',
        function (env) { delete env.System.drawPNG; delete env.System.createSprite; });
    check('sprite sem sprites/PNG: painter fallback recebe (w,h,x,y) no blit',
          r.grabbed.spr[0] === true && r.grabbed.spr[1] === 1 &&
          r.grabbed.spr[2] === 10 && r.grabbed.spr[3] === 20,
          JSON.stringify(r.grabbed.spr));

    // com sprites + PNG disponiveis, o slot aloca id do pool
    r = runEngine(
        'var E = require("celeros.engine"); E.init({ dir: "Teste" });' +
        'E.spr.load([{ name: "a", file: "a", w: 8, h: 8, paint: function () {} },' +
        '            { name: "b", file: "b", w: 8, h: 8, paint: function () {} }]);' +
        '__harness.grab("slots", [!!E.spr._slots.a.id, !!E.spr._slots.b.id, E.spr._used]);' +
        'E.spr.free("a");' +
        '__harness.grab("posfree", E.spr._used);');
    check('sprite com PNG: aloca 2 slots do pool e free devolve',
          r.grabbed.slots[0] === true && r.grabbed.slots[1] === true &&
          r.grabbed.slots[2] === 2 && r.grabbed.posfree === 1,
          JSON.stringify(r.grabbed.slots) + ' free=' + r.grabbed.posfree);

    // fx: burst spawna, tick mata por life, popText registra floater
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'E.fx.burst(120, 160, { n: 5, life: 0.2 });' +
        'var vivas0 = 0;' +
        'for (var i = 0; i < E.fx._parts.length; i++) if (!E.fx._parts[i].dead) vivas0++;' +
        'E.fx.popText(10, 10, "oi");' +
        'E.fx.draw();' +
        'E.run({ s: { update: function () {' +
        '  var vivos = 0;' +
        '  for (var j = 0; j < E.fx._parts.length; j++) if (!E.fx._parts[j].dead) vivos++;' +
        '  if (vivos === 0) E.quit();' +
        '} } }, "s");' +
        '__harness.grab("fx", [vivas0, E.fx._floaters.length]);');
    check('fx: burst spawna 5 e tick mata por life',
          r.grabbed.fx[0] === 5 && r.grabbed.fx[1] === 1, JSON.stringify(r.grabbed.fx));

    // audio: sfx por nome e melodia, beat via musicPos
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'var b0 = E.audio.beat();' +
        'E.audio.sfx("ui");' +
        'E.audio.sfx("inexistente");' +
        'E.audio.sfx([440, 30]);' +
        'E.audio.music({ bpm: 120, loops: 1,' +
        '  tracks: [{ wave: "sq", vol: 80, notes: [[69, 16]] }] });' +
        'var batida = E.audio.beat();' +
        'E.audio.stop();' +
        '__harness.grab("aud", [b0, batida >= 0, !E.audio.playing()]);');
    var tones = r.log.filter(function (l) { return l.indexOf('[tone]') === 0; });
    check('audio: sfx por nome e melodia chegam ao playTone (2 tons)',
          tones.length === 2, JSON.stringify(tones));
    check('audio: beat -1 sem musica e >= 0 com musica tocando',
          r.grabbed.aud[0] === -1 && r.grabbed.aud[1] === true && r.grabbed.aud[2] === true,
          JSON.stringify(r.grabbed.aud));

    // slots por sonda: o cap vem do firmware (spriteSlots) — a def que passa
    // do limite vira painter; sem a chamada (firmware velho) fica em 4
    function slotDefs(n) {
        var defs = [];
        for (var i = 0; i < n; i++)
            defs.push({ name: 's' + i, w: 8, h: 8, paint: function () {} });
        return defs;
    }
    r = runEngine(
        'var E = require("celeros.engine"); E.init({ dir: "Teste" });' +
        '__harness.grab("cap", E.caps.slots);' +
        'E.spr.load(__harness.defs);' +
        '__harness.grab("res", [E.spr._used, E.spr.backed("s3"), E.spr.backed("s4")]);',
        function (env) { env.__harness.defs = slotDefs(5); });
    check('slots: cap 4 do stub — 4 slots e a 5a def vira painter',
          r.grabbed.cap === 4 && r.grabbed.res[0] === 4 &&
          r.grabbed.res[1] === true && r.grabbed.res[2] === false,
          JSON.stringify(r.grabbed));
    r = runEngine(
        'var E = require("celeros.engine"); E.init({ dir: "Teste" });' +
        '__harness.grab("cap", E.caps.slots);' +
        'E.spr.load(__harness.defs);' +
        '__harness.grab("res", [E.spr._used, E.spr.backed("s7"), E.spr.backed("s8")]);',
        function (env) {
            env.System.spriteSlots = function () { return 8; };
            env.__harness.defs = slotDefs(9);
        });
    check('slots: cap 8 (API 29) — 8 slots e a 9a def vira painter',
          r.grabbed.cap === 8 && r.grabbed.res[0] === 8 &&
          r.grabbed.res[1] === true && r.grabbed.res[2] === false,
          JSON.stringify(r.grabbed));

    // audio.duck: abafa a trilha (sfx destrava na janela) e retoma sozinho
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'var semMusica = E.audio.duck(300);' +
        'E.audio.music({ bpm: 120, loops: 1,' +
        '  tracks: [{ wave: "sq", vol: 80, notes: [[69, 16]] }] });' +
        'var d1 = E.audio.duck(500);' +
        'var parada = E.audio.playing();' +
        'E.audio.sfx("boom");' +
        'E.audio._tick();' +
        'var aindaParada = E.audio.playing();' +
        'System.delay(600);' +
        'E.audio._tick();' +
        '__harness.grab("duck", [semMusica, d1, parada, aindaParada, E.audio.playing()]);');
    var duckTones = r.log.filter(function (l) { return l.indexOf('[tone]') === 0; });
    check('audio.duck: sfx toca na janela (playing falso destrava o canal)',
          duckTones.length === 1, JSON.stringify(duckTones));
    check('audio.duck: para a trilha e retoma sozinho depois da janela',
          r.grabbed.duck[0] === false && r.grabbed.duck[1] === true &&
          r.grabbed.duck[2] === false && r.grabbed.duck[3] === false &&
          r.grabbed.duck[4] === true,
          JSON.stringify(r.grabbed.duck));

    // save: prefixo + best (recorde)
    r = runEngine(
        'var E = require("celeros.engine"); E.init({ save: "tst." });' +
        'E.save.set("hi", 42);' +
        'var get = [E.save.get("hi"), E.save.get("nada", "def"), E.save.num("hi")];' +
        'var b1 = E.save.best("hi", 10);' +
        'var b2 = E.save.best("hi", 99);' +
        '__harness.grab("save", [get, b1, b2, E.save.num("hi")]);');
    check('save: get/set/num com prefixo', r.grabbed.save[0][0] === '42' &&
          r.grabbed.save[0][1] === 'def' && r.grabbed.save[0][2] === 42,
          JSON.stringify(r.grabbed.save[0]));
    check('save: best so grava recorde novo',
          r.grabbed.save[1] === false && r.grabbed.save[2] === true &&
          r.grabbed.save[3] === 99);

    // mat/rng: clamp/lerp/wrap e xorshift deterministico
    r = runEngine(
        'var E = require("celeros.engine");' +
        'var r1 = E.rng(42), r2 = E.rng(42);' +
        'var s1 = [r1(), r1(), r1()].join(","), s2 = [r2(), r2(), r2()].join(",");' +
        '__harness.grab("rng", [s1 === s2, s1.length > 0]);' +
        '__harness.grab("math", [E.m.clamp(5, 0, 3), E.m.lerp(0, 10, 0.5), E.m.wrap(250, 0, 240)]);');
    check('rng deterministico por seed', r.grabbed.rng[0] === true);
    check('math: clamp/lerp/wrap',
          r.grabbed.math[0] === 3 && r.grabbed.math[1] === 5 && r.grabbed.math[2] === 10,
          JSON.stringify(r.grabbed.math));

    // fps por cena: cena com fps 10 avanca o relogio ~100 ms por frame
    r = runEngine(
        'var E = require("celeros.engine"); E.init({ fps: 60 });' +
        'var frames = 0;' +
        'E.run({ lento: { fps: 10, update: function () {' +
        '  frames++; if (frames >= 6) E.quit(); } } }, "lento");' +
        '__harness.grab("ms", System.millis());');
    check('scene.fps sobrepoe o alvo global (5 frames a 10 fps ~ +500 ms)',
          r.grabbed.ms >= 1480 && r.grabbed.ms <= 1650, 'ms=' + r.grabbed.ms);

    // E.fx.ring: onda de choque com expansao propria
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'E.fx.ring(100, 100, { speed: 500, color: 0xFFE0, life: 0.5 });' +
        'var p = null;' +
        'for (var i = 0; i < E.fx._parts.length; i++) if (!E.fx._parts[i].dead) p = E.fx._parts[i];' +
        '__harness.grab("ring", [p && p.shape, p && p.grow]);');
    check('fx.ring cria onda com grow configurado',
          r.grabbed.ring[0] === 'ring' && r.grabbed.ring[1] === 500,
          JSON.stringify(r.grabbed.ring));

    // spr.backed: sem createSprite o sprite existe, mas nao tem slot real
    r = runEngine(
        'var E = require("celeros.engine"); E.init({});' +
        'E.spr.load([{ name: "x", file: "x", w: 4, h: 4, paint: function () {} }]);' +
        '__harness.grab("backed", [E.spr.has("x"), E.spr.backed("x")]);',
        function (env) { delete env.System.createSprite; delete env.System.drawPNG; });
    check('spr.backed distingue slot real de painter',
          r.grabbed.backed[0] === true && r.grabbed.backed[1] === false,
          JSON.stringify(r.grabbed.backed));
})();

// ------------------------------------------------------------------ mesh --
(function () {
    console.log('SDK mesh (tools/sdk/engine/celeros.mesh.js):');
    var harness = require('../../test/js_harness/run.js');

    var tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'celer-mesh-'));
    fs.writeFileSync(path.join(tmp, 'app.json'),
                     JSON.stringify({ deps: { 'celeros.mesh': '^1.0.0' } }));

    // placa sem BT: os globais CelerNet/Pack nem existem — o modulo tem que
    // nascer nesse mundo (o typeof do escopo e fixo por execucao, como no
    // heap do device)
    var env = harness.makeEnv();
    var mesh = harness.makeRequire(tmp, env)('celeros.mesh');
    check('exporta version', typeof mesh.version === 'string');
    check('available/active false sem CelerNet (placa sem BT)',
          mesh.available() === false && mesh.active() === false);

    // com a malha exposta (meshsim): gate desenha o portao quando desligada
    // e libera o frame quando ativa; me() e o drain da fila
    var env2 = harness.makeEnv();
    env2.__exposeMesh = true;
    var req2 = harness.makeRequire(tmp, env2);
    var mesh2 = req2('celeros.mesh');
    check('available true com CelerNet+Pack', mesh2.available() === true);
    var out = { closed: -1 };
    try {
        var fn = new Function('UI', 'System', '__harness', 'require',
            'var mesh = require("celeros.mesh");' +
            'var closed = 0;' +
            'UI.begin();' +
            'if (!mesh.gate("ligue a malha")) closed = 1;' +
            'UI.end();' +
            '__harness.grab("closed", closed);');
        env2.__harness.grab = function (k, v) { out[k] = v; };
        fn(env2.UI, env2.System, env2.__harness, req2);
    } catch (e) { out.closed = 'erro: ' + e; }
    check('gate devolve false com a malha desligada (portao desenhado)',
          out.closed === 1, String(out.closed));
    env2.CelerNet.start({});
    check('active true apos CelerNet.start', mesh2.active() === true);
    var gateOk = false;
    try { gateOk = mesh2.gate('x') === true; } catch (e) { gateOk = false; }
    check('gate devolve true com a malha ativa', gateOk);
    var me = mesh2.me();
    check('me() devolve o papel do Pack', !!(me && (me.name || me.caps)));
    var got = [];
    env2.__harness.pushMesh([{ from: 'a1', msg: 'm1' }, { from: 'b2', msg: 'm2' }]);
    var n = mesh2.each(function (m) { got.push(m.msg); }, 0);
    check('each drena a fila (2 quadros) e esvazia',
          n === 2 && got[0] === 'm1' && got[1] === 'm2' &&
          mesh2.each(function () {}, 0) === 0);
    fs.rmSync(tmp, { recursive: true, force: true });
})();

// ------------------------------------------------------- verlet nativo --
(function () {
    console.log('SDK verletFast (celeros.physics x System.verlet*, API 31):');
    var harness = require('../../test/js_harness/run.js');

    var tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'celer-vf-'));
    fs.writeFileSync(path.join(tmp, 'app.json'),
                     JSON.stringify({ deps: { 'celeros.physics': '^1.2.0' } }));

    // caminho nativo: o stub do harness espelha o binding C++ (JsPhysics.cpp)
    var env = harness.makeEnv();
    var P = harness.makeRequire(tmp, env)('celeros.physics');
    check('physics 1.2.0 exporta verletFast', typeof P.verletFast === 'function');
    var v = P.verletFast({ iterations: 4 });
    check('verletFast usa o nativo quando existe (stub do harness)', v.native === true);
    var a = v.add(120, 10), b = v.add(120, 40);
    check('add devolve indices crescentes', a === 0 && b === 1);
    v.stick(a, b);                    // len = distancia atual (30)
    v.pin(a);
    check('count conta os pontos', v.count() === 2);
    var st = v.sticks();
    check('sticks() devolve pares planos', st.length === 2 && st[0] === a && st[1] === b);
    for (var i = 0; i < 240; i++) v.step(1 / 60, { gravity: { x: 0, y: 900 } });
    var xy = v.xy();
    var len = Math.sqrt((xy[2] - xy[0]) * (xy[2] - xy[0]) + (xy[3] - xy[1]) * (xy[3] - xy[1]));
    check('corda pende no nativo: b desceu e o vinculo sobrevive',
          xy[3] > 35 && Math.abs(len - 30) < 1,
          'b.y=' + xy[3].toFixed(1) + ' len=' + len.toFixed(2));
    var v2 = P.verletFast({});
    var c = v2.add(120, 20);
    for (var k = 0; k < 400; k++) v2.step(1 / 60, { gravity: { x: 0, y: 900 },
                                                    bounds: { x: 0, y: 0, w: 240, h: 50 } });
    check('bounds do nativo seguram no chao', v2.xy()[1] <= 50.01,
          'y=' + v2.xy()[1].toFixed(2));
    v2.set(c, 30, 10);
    check('set reposiciona o ponto', v2.xy()[0] === 30 && v2.xy()[1] === 10);
    v2.free();
    v.free();
    check('free devolve o mundo (count 0)', v2.count() === 0);

    // fallback: firmware sem o binding — verletFast cai para o verlet JS
    var env2 = harness.makeEnv();
    delete env2.System.verletNew;
    var P2 = harness.makeRequire(tmp, env2)('celeros.physics');
    var v3 = P2.verletFast({ iterations: 4 });
    check('sem System.verletNew: cai para o verlet JS (mesma interface)',
          v3.native === false && typeof v3.xy === 'function' &&
          typeof v3.delStick === 'function' && typeof v3.step === 'function');
    var x0 = v3.add(120, 10);
    v3.pin(x0);
    for (var n = 0; n < 120; n++) v3.step(1 / 60, { gravity: { x: 0, y: 900 } });
    check('fallback pende igual (no preso no lugar)', Math.abs(v3.xy()[0] - 120) < 0.01);
    fs.rmSync(tmp, { recursive: true, force: true });
})();

// ------------------------------------------- verlet: manuseio + colisao --
(function () {
    console.log('SDK verletFast manuseio (del/pins/colisao, physics 1.3.0):');
    var harness = require('../../test/js_harness/run.js');

    var tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'celer-vf2-'));
    fs.writeFileSync(path.join(tmp, 'app.json'),
                     JSON.stringify({ deps: { 'celeros.physics': '^1.3.0' } }));
    var env = harness.makeEnv();
    var P = harness.makeRequire(tmp, env)('celeros.physics');

    // delStick/delPoint mantem os indices coerentes (delecoes do fim pro comeco)
    var v = P.verletFast({ iterations: 2 });
    var a = v.add(50, 50), b = v.add(80, 50), c = v.add(110, 50), d = v.add(140, 50);
    v.stick(a, b); v.stick(b, c); v.stick(c, d);
    check('3 vinculos criados', v.sticks().length === 6);
    check('delStick remove (swap do fim)', v.delStick(1) === true && v.sticks().length === 4);
    check('delStick de indice morto devolve false', v.delStick(9) === false);
    v.free();
    // delPoint no meio: vinculos ligados saem e o ultimo ponto herda o indice
    v = P.verletFast({ iterations: 2 });
    a = v.add(50, 50); b = v.add(80, 50); c = v.add(110, 50); d = v.add(140, 50);
    v.stick(a, b); v.stick(b, c); v.stick(c, d);
    check('delPoint(b) derruba o ponto', v.delPoint(b) === true && v.count() === 3);
    var st = v.sticks();
    check('delPoint remove vinculos ligados e reindexa (c-d vira 1-2)',
          st.length === 2 && st[0] === 1 && st[1] === 2,
          JSON.stringify(st));
    check('delPoint de indice morto devolve false', v.delPoint(7) === false);
    v.free();

    // pins: alterna e le
    v = P.verletFast({});
    var p0 = v.add(30, 30), p1 = v.add(60, 30);
    v.pin(p0);
    check('pins le o estado (1 fixado, 1 livre)',
          JSON.stringify(v.pins()) === '[1,0]', JSON.stringify(v.pins()));
    v.pin(p0, false);
    check('pin(idx,false) solta', v.pins()[0] === 0);
    v.free();

    // colisao ponto-ponto: dois nos soltos sobrepostos se separam; um par
    // VINCULADO nao se afasta alem do comprimento do vinculo
    var vc = P.verletFast({ iterations: 2, radius: 4 });
    var s0 = vc.add(100, 100), s1 = vc.add(101, 101);       // sobrepostos (d~1.4 < 8)
    for (var i = 0; i < 30; i++) vc.step(1, { gravity: { x: 0, y: 0 } });
    var xy = vc.xy();
    var dd = Math.sqrt((xy[0] - xy[2]) * (xy[0] - xy[2]) + (xy[1] - xy[3]) * (xy[1] - xy[3]));
    check('colisao separa nos sobrepostos e PARA (d ~ 2r, sem ejetar)',
          dd >= 7.2 && dd <= 9, 'd=' + dd.toFixed(2));
    vc.free();
    var vr = P.verletFast({ iterations: 2, radius: 4 });
    var r0 = vr.add(100, 100), r1 = vr.add(104, 100);
    vr.stick(r0, r1, 4);                                      // vinculados a 4px
    for (var k = 0; k < 30; k++) vr.step(1, { gravity: { x: 0, y: 0 } });
    var xy2 = vr.xy();
    var dr = Math.sqrt((xy2[0] - xy2[2]) * (xy2[0] - xy2[2]) + (xy2[1] - xy2[3]) * (xy2[1] - xy2[3]));
    check('par vinculado nao e separado pela colisao (fica no len)', dr < 5,
          'd=' + dr.toFixed(2));
    v.free(); vc.free(); vr.free();
    fs.rmSync(tmp, { recursive: true, force: true });
})();

// -------------------------------------------------------- scaffold --game --
(function () {
    console.log('SDK scaffold --game (engine + physics):');
    var { runLint } = require('../../tools/app_lint/lint.js');
    var { execSync } = require('child_process');
    var { runAppFolder } = require('../../tools/sdk/lib/runner.js');

    var tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'celer-game-'));
    var appDir = path.join(tmp, 'Quica');
    execSync('node tools/sdk/celer.js new Quica --game --dir ' + JSON.stringify(tmp),
             { cwd: ROOT });

    var want = ['app.json', 'main.js', 'icon.png', 'jsconfig.json', 'celer.d.ts',
                'README.md', 'celeros.engine.d.ts'];
    var missing = want.filter(function (f) { return !fs.existsSync(path.join(appDir, f)); });
    check('scaffold de jogo cria app + tipos da engine (dev-only)',
          missing.length === 0, 'faltam: ' + missing.join(', '));
    check('engine/physics NAO sao copiadas (deps do hub, API 30)',
          !fs.existsSync(path.join(appDir, 'celeros.engine.js')) &&
          !fs.existsSync(path.join(appDir, 'celeros.physics.js')) &&
          !fs.existsSync(path.join(appDir, 'engine.js')));

    var mf = JSON.parse(fs.readFileSync(path.join(appDir, 'app.json')));
    check('app.json do jogo: type Game, psram, sem topbar',
          mf.type === 'Game' && mf.category === 'Jogos' &&
          Array.isArray(mf.requires) && mf.requires.indexOf('psram') >= 0 &&
          mf.topbar === false);
    check('app.json declara deps celeros.engine/physics',
          mf.deps && /^(\^)?\d+\.\d+\.\d+$/.test(mf.deps['celeros.engine']) &&
          /^(\^)?\d+\.\d+\.\d+$/.test(mf.deps['celeros.physics']),
          JSON.stringify(mf.deps));

    var result = runLint([appDir], {});
    check('jogo scaffoldado passa limpo no lint',
          result.totals.errors === 0,
          result.apps[0].diagnostics.map(function (d) { return d.message; }).join('; '));

    // soma dos .js como o install ve: main.js do pacote + as deps da arvore
    // do SDK (o que a loja baixaria para /local/modules)
    var size = fs.statSync(path.join(appDir, 'main.js')).size +
               fs.statSync(path.join(ROOT, 'tools', 'sdk', 'engine', 'celeros.engine.js')).size +
               fs.statSync(path.join(ROOT, 'tools', 'sdk', 'engine', 'celeros.physics.js')).size;
    check('soma dos .js (pacote + deps) cabe no teto psram (128 KB)', size < 128 * 1024,
          (size / 1024).toFixed(1) + ' KB');

    var r = runAppFolder(appDir, { render: true, stopAtMs: 2000 });
    check('jogo roda limpo no emu (cena titulo)', r.err === null, r.err);
    var nonzero = 0;
    for (var i = 0; i < r.renderer.fb.length; i += 997) {
        if (r.renderer.fb[i] !== 0) nonzero++;
    }
    check('framebuffer do titulo nao esta zerado', nonzero > 0);

    // tap no JOGAR entra na cena de jogo (bola + raquete + HUD)
    var r2 = runAppFolder(appDir, {
        render: true, stopAtMs: 8000,
        events: function (env) { env.__harness.tap(120, 178); },
    });
    check('tap no JOGAR entra na cena de jogo sem erro', r2.err === null, r2.err);
    fs.rmSync(tmp, { recursive: true, force: true });
})();

// resumo
console.log('');
if (failures) {
    console.log('FALHAS: ' + failures);
    process.exit(1);
}
console.log('OK: todos os testes do SDK passaram');
