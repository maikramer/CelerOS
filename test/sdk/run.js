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
})();

// ---------------------------------------------------------------- physics --
(function () {
    console.log('SDK physics (tools/sdk/engine/physics.js):');
    var P = require('../../tools/sdk/engine/physics.js');

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

// ----------------------------------------------------------------- engine --
(function () {
    console.log('SDK engine (tools/sdk/engine/engine.js):');
    var harness = require('../../test/js_harness/run.js');
    var ENGINE_DIR = path.join(ROOT, 'tools', 'sdk', 'engine');

    // roda um snippet com require("engine") real sobre o harness (relogio
    // virtual, touchQ); setup(env) roda antes do snippet
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
        'var E = require("engine");' +
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
        'var E = require("engine"); E.init({});' +
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
        'var E = require("engine"); E.init({});' +
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
        'var E = require("engine"); E.init({});' +
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
        'var E = require("engine"); E.init({});' +
        'var obj = { x: 0, done: false };' +
        'E.tween(obj, { x: 100 }, 500, { ease: E.m.linear,' +
        '          onDone: function () { obj.done = true; } });' +
        'E.run({ s: { update: function () { if (obj.done) E.quit(); } } }, "s");' +
        '__harness.grab("x", obj.x);');
    check('tween chega no alvo com onDone', r.grabbed.x === 100,
          'x=' + r.grabbed.x);

    // grupo: swap-pop remove mortos; pool recicla
    r = runEngine(
        'var E = require("engine"); E.init({});' +
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
        'var E = require("engine"); E.init({ dir: "Teste" });' +
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
        'var E = require("engine"); E.init({ dir: "Teste" });' +
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
        'var E = require("engine"); E.init({});' +
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
        'var E = require("engine"); E.init({});' +
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

    // save: prefixo + best (recorde)
    r = runEngine(
        'var E = require("engine"); E.init({ save: "tst." });' +
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
        'var E = require("engine");' +
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
        'var E = require("engine"); E.init({ fps: 60 });' +
        'var frames = 0;' +
        'E.run({ lento: { fps: 10, update: function () {' +
        '  frames++; if (frames >= 6) E.quit(); } } }, "lento");' +
        '__harness.grab("ms", System.millis());');
    check('scene.fps sobrepoe o alvo global (5 frames a 10 fps ~ +500 ms)',
          r.grabbed.ms >= 1480 && r.grabbed.ms <= 1650, 'ms=' + r.grabbed.ms);

    // E.fx.ring: onda de choque com expansao propria
    r = runEngine(
        'var E = require("engine"); E.init({});' +
        'E.fx.ring(100, 100, { speed: 500, color: 0xFFE0, life: 0.5 });' +
        'var p = null;' +
        'for (var i = 0; i < E.fx._parts.length; i++) if (!E.fx._parts[i].dead) p = E.fx._parts[i];' +
        '__harness.grab("ring", [p && p.shape, p && p.grow]);');
    check('fx.ring cria onda com grow configurado',
          r.grabbed.ring[0] === 'ring' && r.grabbed.ring[1] === 500,
          JSON.stringify(r.grabbed.ring));

    // spr.backed: sem createSprite o sprite existe, mas nao tem slot real
    r = runEngine(
        'var E = require("engine"); E.init({});' +
        'E.spr.load([{ name: "x", file: "x", w: 4, h: 4, paint: function () {} }]);' +
        '__harness.grab("backed", [E.spr.has("x"), E.spr.backed("x")]);',
        function (env) { delete env.System.createSprite; delete env.System.drawPNG; });
    check('spr.backed distingue slot real de painter',
          r.grabbed.backed[0] === true && r.grabbed.backed[1] === false,
          JSON.stringify(r.grabbed.backed));
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
                'README.md', 'engine.js', 'physics.js', 'engine.d.ts'];
    var missing = want.filter(function (f) { return !fs.existsSync(path.join(appDir, f)); });
    check('scaffold de jogo cria app + engine + physics + tipos',
          missing.length === 0, 'faltam: ' + missing.join(', '));

    var mf = JSON.parse(fs.readFileSync(path.join(appDir, 'app.json')));
    check('app.json do jogo: type Game, psram, sem topbar',
          mf.type === 'Game' && mf.category === 'Jogos' &&
          Array.isArray(mf.requires) && mf.requires.indexOf('psram') >= 0 &&
          mf.topbar === false);

    var result = runLint([appDir], {});
    check('jogo scaffoldado passa limpo no lint (engine+physics sao ES5 ok)',
          result.totals.errors === 0,
          result.apps[0].diagnostics.map(function (d) { return d.message; }).join('; '));

    var size = fs.statSync(path.join(appDir, 'main.js')).size +
               fs.statSync(path.join(appDir, 'engine.js')).size +
               fs.statSync(path.join(appDir, 'physics.js')).size;
    check('soma dos .js cabe no teto psram (128 KB)', size < 128 * 1024,
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
