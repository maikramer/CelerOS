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
})();

// resumo
console.log('');
if (failures) {
    console.log('FALHAS: ' + failures);
    process.exit(1);
}
console.log('OK: todos os testes do SDK passaram');
