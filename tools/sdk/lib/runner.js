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

// Roda o app: opts { render, stopAtMs, frames, events }.
//   render    -> instala o renderer e devolve `renderer` no resultado
//   stopAtMs  -> para o app quando o relogio virtual passa disso (apps sao
//                loops infinitos; default 1200ms quando render, senao o
//                LIMIT do harness manda)
//   frames    -> [ms...]: snapshot do framebuffer ao cruzar cada marco do
//                RELOGIO DO APP (0 = estado inicial); o app para apos o
//                ultimo. Resultado ganha r.frames = [{ms, fb, png}]
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
    const frames = Array.isArray(opts.frames) && opts.frames.length
        ? opts.frames.map(Number).sort((a, b) => a - b) : null;
    // clock do harness comeca em 1000: marcos do app = base + ms pedido
    const FR_BASE = 1000;
    const stopAtMs = frames ? FR_BASE + frames[frames.length - 1]
        : (opts.stopAtMs != null ? opts.stopAtMs : (opts.render ? 1200 : null));

    const wires = [];
    if (renderer) wires.push((env) => renderer.wire(env));
    if (stopAtMs != null) {
        wires.push((env) => {
            const orig = env.System.delay;
            let idx = frames ? 0 : -1;
            env.System.delay = function (ms) {
                // snapshot dos marcos vencidos ANTES de avancar o relogio
                if (frames && renderer && env.__framesOut) {
                    while (idx < frames.length && env.System.millis() >= FR_BASE + frames[idx]) {
                        env.__framesOut.push({ ms: frames[idx], fb: renderer.fb.slice(), png: renderer.png() });
                        idx++;
                    }
                }
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
        env.__framesOut = frames ? [] : null;
        for (const w of wires) w(env);
    });
    r.renderer = renderer;
    r.frames = (r.env && r.env.__framesOut) || [];
    return r;
}

module.exports = { runAppFolder, loadAppWire };
