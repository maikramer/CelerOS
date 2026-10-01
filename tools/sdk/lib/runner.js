'use strict';
// Runner do SDK: executa uma PASTA de app no harness do repo (makeEnv/runApp,
// test/js_harness) com opcional renderer headless (snapshot PNG) e wire de
// teste proprio do app (PASTA/test.js exportando wire(env)).

const fs = require('fs');
const path = require('path');
const harness = require('../../../test/js_harness/run.js');
const { Renderer } = require('./renderer.js');

const ROOT = path.resolve(__dirname, '../../..');

// Carrega o wire de teste do app: PASTA/test.js (module.exports.wire(env) ou
// module.exports = function(env)). Opcional e nao obrigatorio.
function loadAppWire(appDir) {
    const p = path.join(appDir, 'test.js');
    if (!fs.existsSync(p)) return null;
    const m = require(p);
    const fn = typeof m === 'function' ? m : m.wire;
    if (typeof fn !== 'function') throw new Error(p + ': exporte `wire(env)`');
    return fn;
}

// Roda o app: opts { render, stopAtMs, events }.
//   render    -> instala o renderer e devolve `renderer` no resultado
//   stopAtMs  -> para o app quando o relogio virtual passa disso (apps sao
//                loops infinitos; default 1200ms quando render, senao o
//                LIMIT do harness manda)
//   events    -> wire extra (CLI --events), roda apos o renderer
function runAppFolder(appDir, opts = {}) {
    const dir = path.resolve(appDir);
    let entry = 'main.js';
    const manifest = path.join(dir, 'app.json');
    if (fs.existsSync(manifest)) {
        try {
            const j = JSON.parse(fs.readFileSync(manifest, 'utf8'));
            if (j.main) entry = j.main;
        } catch (e) { /* app.json invalido: o lint acusa; aqui segue default */ }
    }
    const mainAbs = path.join(dir, entry);
    if (!fs.existsSync(mainAbs)) throw new Error('entrypoint nao encontrado: ' + mainAbs);

    const renderer = opts.render ? new Renderer() : null;
    const stopAtMs = opts.stopAtMs != null ? opts.stopAtMs : (opts.render ? 1200 : null);

    const wires = [];
    if (renderer) wires.push((env) => renderer.wire(env));
    if (stopAtMs != null) {
        wires.push((env) => {
            const orig = env.System.delay;
            env.System.delay = function (ms) {
                if (env.System.millis() > stopAtMs) throw { harnessStop: true };
                return orig(ms);
            };
        });
    }
    if (opts.events) wires.push(opts.events);
    const appWire = loadAppWire(dir);
    if (appWire) wires.push(appWire);

    // runApp espera caminho relativo ao ROOT do repo; path.relative resolve
    // pastas fora do repo com "../.." — mesma coisa para ele.
    const r = harness.runApp(path.relative(ROOT, mainAbs), (env) => {
        for (const w of wires) w(env);
    });
    r.renderer = renderer;
    return r;
}

module.exports = { runAppFolder, loadAppWire };
