#!/usr/bin/env node
'use strict';
// celer.js — CLI do SDK de apps CelerOS.
//
// Fluxo do autor de app:
//   node tools/sdk/celer.js new MeuApp            scaffold (app.json + main.js
//                                                 + icon + types para o editor)
//   node tools/sdk/celer.js emu MeuApp            roda no emulador (tela PNG)
//   python3 tools/celerctl.py dev MeuApp          live no dispositivo
//   node tools/sdk/celer.js publish MeuApp        publica na loja
//
// `lint`/`check` delegam ao tools/app_lint (manifest derivado do firmware);
// `dev`/`publish` delegam ao celerctl/celerctl (Python). Sem dependencias:
// acorn e font8x8 sao vendorados, PNG usa o zlib do Node.

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const { buildManifest, runLint, runCheck } = require('../app_lint/lint.js');
const { generate } = require('./lib/dts.js');
const { runAppFolder } = require('./lib/runner.js');
const { encodePng } = require('./lib/png.js');
const { glyph } = require('./lib/font8x8.js');

const ROOT = path.resolve(__dirname, '../..');
const TYPES_OUT = path.join(__dirname, 'types', 'celer.d.ts');

function die(msg, code = 1) {
    console.error('erro: ' + msg);
    process.exit(code);
}

function usage() {
    console.log('uso: node tools/sdk/celer.js <comando> [args]');
    console.log('');
    console.log('  new NOME [--pkg br.autor.nome] [--dir BASE] [--game]     cria app de exemplo');
    console.log('                                               (--game: scaffold de jogo c/ engine)');
    console.log('  engine PASTA                                 vendoriza/atualiza engine+physics no app');
    console.log('  lint [alvos...] [--strict]                   valida ES5 + API (app_lint)');
    console.log('  types [--out ARQ]                            (re)gera celer.d.ts do manifest');
    console.log('  test PASTA                                   roda o app no harness (stubs Node)');
    console.log('  emu PASTA [--events ARQ] [--ms N] [--out ARQ] roda + snapshot PNG da tela (240x320)');
    console.log('  check                                        drift: codigo x docs x stubs x types x emulador');
    console.log('  publish PASTA... [--dry]                     publica na loja (celerhub)');
    console.log('  dev PASTA [--port P] [--shots]               watch + reload no device (celerctl)');
}

// ---------------------------------------------------------------- scaffold

// Icone 64x64: quadrado arredondado no tom accent do tema com a inicial do
// app em glifo 8x8 escalado — placeholder para o autor trocar depois.
function makeIconPng(letter) {
    const S = 64, R = 14;
    const bg = [0x38, 0xBC, 0xF8];   // accent do tema (RGB888)
    const fg = [0x08, 0x10, 0x20];   // onAccent
    const px = new Uint8Array(S * S * 3);
    const inside = (x, y) => {
        // retangulo com cantos arredondados (anti-alias simples por distancia)
        const cx = Math.min(Math.max(x, R), S - 1 - R), cy = Math.min(Math.max(y, R), S - 1 - R);
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= R * R;
    };
    for (let y = 0; y < S; y++) {
        for (let x = 0; x < S; x++) {
            const i = (y * S + x) * 3;
            if (!inside(x, y)) continue;  // transparente (preto; PNG RGB sem alpha)
            px[i] = bg[0]; px[i + 1] = bg[1]; px[i + 2] = bg[2];
        }
    }
    const bm = glyph(letter.toUpperCase().charCodeAt(0));
    if (bm) {
        const scale = 6, gw = 8 * scale, ox = (S - gw) / 2 | 0, oy = (S - gw) / 2 | 0;
        for (let row = 0; row < 8; row++) {
            for (let col = 0; col < 8; col++) {
                if (!((bm[row] >> col) & 1)) continue;
                for (let sy = 0; sy < scale; sy++) {
                    for (let sx = 0; sx < scale; sx++) {
                        const x = ox + col * scale + sx, y = oy + row * scale + sy;
                        if (x < 0 || y < 0 || x >= S || y >= S) continue;
                        const i = (y * S + x) * 3;
                        px[i] = fg[0]; px[i + 1] = fg[1]; px[i + 2] = fg[2];
                    }
                }
            }
        }
    }
    return encodePng(S, S, Buffer.from(px), false);
}

function cmdNew(args) {
    const name = args[0];
    if (!name) die('new: informe o NOME do app (ex: new MeuApp)');
    const flags = parseFlags(args.slice(1));
    const base = flags.dir ? path.resolve(flags.dir) : process.cwd();
    const dir = path.join(base, name);
    if (fs.existsSync(dir)) die('new: ' + dir + ' ja existe');
    const game = !!flags.game;

    // packageName: default br.celer.<slug> (autor troca pelo proprio)
    const slug = name.toLowerCase().replace(/[^a-z0-9]+/g, '');
    const pkg = flags.pkg || ('br.celer.' + slug);
    const api = buildManifest().apiLevel;

    const appJson = {
        name: name,
        packageName: pkg,
        version: '0.1.0',
        author: 'Seu Nome',
        description: game ? 'Jogo feito com a engine CelerOS' :
                            'App CelerOS criado com o SDK',
        type: game ? 'Game' : 'App',
        category: game ? 'Jogos' : 'Utilidades',
        api: api,
        permissions: [],
        changelog: '0.1.0: primeira versao',
    };
    if (game) {
        // jogos com a engine rodam nas placas S3 com PSRAM (teto de 128 KB);
        // sem topbar, tela cheia como o Supernova
        appJson.requires = ['psram'];
        appJson.topbar = false;
    }

    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'app.json'), JSON.stringify(appJson, null, 2) + '\n');
    const mainSrc = fs.readFileSync(
        path.join(__dirname, game ? 'template-game' : 'template', 'main.js'), 'utf8');
    fs.writeFileSync(path.join(dir, 'main.js'),
                     mainSrc.replace(/\{\{APP_NAME\}\}/g, name));
    fs.writeFileSync(path.join(dir, 'README.md'),
                     fs.readFileSync(path.join(__dirname, 'template', 'README.md'), 'utf8')
                         .replace(/\{\{APP_NAME\}\}/g, name)
                         .replace(/\{\{APP_DIR\}\}/g, name));
    fs.copyFileSync(TYPES_OUT, path.join(dir, 'celer.d.ts'));
    fs.writeFileSync(path.join(dir, 'jsconfig.json'), JSON.stringify({
        compilerOptions: { checkJs: false },
        include: ['*.js', 'celer.d.ts'],
    }, null, 2) + '\n');
    fs.writeFileSync(path.join(dir, 'icon.png'), makeIconPng(name[0]));
    if (game) {
        for (const f of ['engine.js', 'physics.js', 'engine.d.ts']) {
            fs.copyFileSync(path.join(__dirname, 'engine', f), path.join(dir, f));
        }
    }

    console.log('criado: ' + dir + ' (api ' + api + (game ? ', jogo com engine' : '') + ')');
    console.log('proximos passos:');
    console.log('  node tools/sdk/celer.js lint ' + dir);
    console.log('  node tools/sdk/celer.js emu ' + dir);
    console.log("  python3 tools/celerctl.py dev " + dir);
}

// ------------------------------------------------------------- engine ----

// vendoriza/atualiza engine.js + physics.js + engine.d.ts num app existente
function cmdEngine(args) {
    const folder = args.find((a) => !a.startsWith('--'));
    if (!folder) die('engine: informe a PASTA do app');
    const dir = path.resolve(folder);
    if (!fs.existsSync(dir)) die('engine: ' + dir + ' nao existe');
    const src = path.join(__dirname, 'engine');
    for (const f of ['engine.js', 'physics.js', 'engine.d.ts']) {
        fs.copyFileSync(path.join(src, f), path.join(dir, f));
    }
    const ver = (f) => {
        const m = fs.readFileSync(path.join(src, f), 'utf8')
            .match(/version: '([^']+)'/);
        return m ? m[1] : '?';
    };
    console.log('engine ' + ver('engine.js') + ' + physics ' + ver('physics.js') +
                ' copiados para ' + dir);
    console.log('no app: var E = require("engine"); var P = require("physics");');
}

// ---------------------------------------------------------------- lint/types

function cmdLint(args) {
    const strict = args.includes('--strict');
    // alvos relativos ao cwd viram absolutos: collectTargets do app_lint
    // resolve relativo ao ROOT do repo (bom para data/apps, ruim para apps fora)
    const targets = args.filter((a) => !a.startsWith('--'))
        .map((a) => path.isAbsolute(a) ? a : path.resolve(process.cwd(), a));
    const result = runLint(targets, { strict });
    for (const a of result.apps) {
        const errs = a.diagnostics.filter((dg) => dg.severity === 'erro').length;
        const warns = a.diagnostics.length - errs;
        const status = errs ? errs + ' erro(s)' : 'OK';
        console.log(a.relDir + ': ' + status + (warns ? ' (' + warns + ' aviso(s))' : ''));
        for (const dg of a.diagnostics) {
            console.log('  ' + dg.file + ':' + dg.line + ':' + dg.col + ' ' +
                        dg.severity + '[' + dg.rule + ']: ' + dg.message);
        }
    }
    console.log('');
    console.log('apps: ' + result.totals.apps + '  erros: ' + result.totals.errors +
                '  avisos: ' + result.totals.warnings +
                '  (API level ' + result.manifest.apiLevel + ', ' +
                result.manifest.counts.functions + ' funcoes no manifest)');
    process.exit(result.ok ? 0 : 1);
}

function cmdTypes(args) {
    const flags = parseFlags(args);
    const out = flags.out ? path.resolve(flags.out) : TYPES_OUT;
    const text = generate() + '\n';
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, text);
    console.log(out + ': regenerado (' + text.split('\n').length + ' linhas)');
}

// ---------------------------------------------------------------- test/emu

function printRun(r) {
    for (const line of r.log) console.log('[app] ' + line);
    if (r.err) {
        console.error('erro do app:');
        console.error(String(r.err).split('\n').slice(0, 12).join('\n'));
    } else {
        console.log('ok: app terminou limpo (' + r.log.length + ' linhas de log)');
    }
}

function cmdTest(args) {
    const folder = args.find((a) => !a.startsWith('--'));
    if (!folder) die('test: informe a PASTA do app');
    try {
        printRun(runAppFolder(folder));
    } catch (e) { die(e.message); }
}

function cmdEmu(args) {
    const flags = parseFlags(args);
    const folder = flags._[0];
    if (!folder) die('emu: informe a PASTA do app');
    const dir = path.resolve(folder);
    const out = flags.out ? path.resolve(flags.out) : path.join(dir, '.dev', 'tela.png');

    let events = null;
    if (flags.events) {
        const m = require(path.resolve(flags.events));
        events = typeof m === 'function' ? m : m.events;
        if (typeof events !== 'function') die(flags.events + ': exporte `events(env)` (use env.__harness.tap/pushTouch)');
    }

    // --frames "0,600,1500": um PNG por marco do relogio do app (0 = estado
    // inicial) + diff de pixels entre consecutivos — ver a UI andar sem GUI
    let frames = null;
    if (flags.frames != null) {
        frames = String(flags.frames).split(',').map((s) => parseInt(s.trim(), 10));
        if (frames.some((n) => isNaN(n) || n < 0)) die('--frames: lista de ms nao-negativos, ex. "0,600,1500"');
    }

    try {
        const r = runAppFolder(dir, { render: true, stopAtMs: flags.ms ? +flags.ms : undefined,
                                      frames, events });
        fs.mkdirSync(path.dirname(out), { recursive: true });
        if (frames) {
            let prev = null;
            for (const f of r.frames) {
                const fout = path.join(path.dirname(out),
                                       'tela-' + String(f.ms).padStart(4, '0') + '.png');
                fs.writeFileSync(fout, f.png);
                if (prev) {
                    let diff = 0;
                    for (let i = 0; i < prev.fb.length; i++) if (prev.fb[i] !== f.fb[i]) diff++;
                    const pct = diff * 100 / prev.fb.length;
                    const label = pct >= 0.1 ? pct.toFixed(1) : (diff > 0 ? '<0.1' : '0.0');
                    console.log('frame ' + f.ms + 'ms: ' + fout + ' (' + label + '% dos pixels mudaram)');
                } else {
                    console.log('frame ' + f.ms + 'ms: ' + fout + ' (estado inicial)');
                }
                prev = f;
            }
        } else {
            fs.writeFileSync(out, r.renderer.png());
        }
        printRun(r);
        if (!frames) {
            console.log('tela: ' + out + ' (240x320' +
                        (r.renderer.unsupported.size ? '; nao renderizado: ' + Array.from(r.renderer.unsupported).join(', ') : '') + ')');
        }
        process.exit(r.err ? 1 : 0);
    } catch (e) { die(e.message); }
}

// ---------------------------------------------------------------- check

function cmdCheck() {
    // 1) drift base do app_lint: codigo x docs x stubs do harness
    const lintCheck = runCheck();
    if (lintCheck.text) console.log(lintCheck.text);
    let errors = lintCheck.errors;

    // 2) celer.d.ts commitado vs manifest atual
    const typesText = fs.existsSync(TYPES_OUT) ? fs.readFileSync(TYPES_OUT, 'utf8') : '';
    const fresh = generate() + '\n';
    if (typesText !== fresh) {
        errors++;
        console.log('ERRO types: tools/sdk/types/celer.d.ts esta com drift do manifest —');
        console.log('      rode `node tools/sdk/celer.js types` e commit junto com a mudanca da API');
    } else {
        console.log('types: celer.d.ts em sincronia com o manifest');
    }

    // 3) emulador: primitivas de desenho do manifest sem render
    const { RENDERED } = require('./lib/renderer.js');
    const manifest = buildManifest();
    const KNOWN_UNRENDERED = new Set(['drawBMP', 'drawPNG', 'drawIcon']);  // por projeto (runtime avisa)
    const isDraw = (name) => /^(fill|draw|push|create|bind)[A-Z]/.test(name) ||
                          ['setTextColor', 'setTextSize', 'setTextDatum', 'textWidth', 'fontHeight', 'deleteSprite'].includes(name);
    const missing = [];
    for (const [objPath, obj] of Object.entries(manifest.objects)) {
        if (objPath !== 'System' && objPath !== 'System.gpio') continue;
        for (const f of obj.fns) {
            if (isDraw(f.name) && !RENDERED.includes(f.name) && !KNOWN_UNRENDERED.has(f.name)) {
                missing.push(objPath + '.' + f.name);
            }
        }
    }
    for (const n of missing) {
        console.log('aviso: ' + n + ' nao renderiza no emulador (wire do renderer sem a primitiva)');
    }

    process.exit(errors ? 1 : 0);
}

// ---------------------------------------------------------------- externos

function cmdPublish(args) {
    const r = spawnSync('python3', [path.join(ROOT, 'tools', 'celerhub.py'), 'publish', ...args],
                        { stdio: 'inherit' });
    process.exit(r.status == null ? 1 : r.status);
}

function cmdDev(args) {
    const r = spawnSync('python3', [path.join(ROOT, 'tools', 'celerctl.py'), 'dev', ...args],
                        { stdio: 'inherit' });
    process.exit(r.status == null ? 1 : r.status);
}

// ---------------------------------------------------------------- cli

function parseFlags(args) {
    const out = { _: [] };
    for (let i = 0; i < args.length; i++) {
        const a = args[i];
        if (a.startsWith('--')) {
            const key = a.slice(2);
            const next = args[i + 1];
            if (next && !next.startsWith('--')) { out[key] = next; i++; }
            else out[key] = true;
        } else out._.push(a);
    }
    return out;
}

function main() {
    const [cmd, ...rest] = process.argv.slice(2);
    switch (cmd) {
        case 'new': return cmdNew(rest);
        case 'engine': return cmdEngine(rest);
        case 'lint': return cmdLint(rest);
        case 'types': return cmdTypes(rest);
        case 'test': return cmdTest(rest);
        case 'emu': return cmdEmu(rest);
        case 'check': return cmdCheck(rest);
        case 'publish': return cmdPublish(rest);
        case 'dev': return cmdDev(rest);
        default:
            usage();
            process.exit(cmd ? 2 : 0);
    }
}

if (require.main === module) main();
module.exports = { makeIconPng, main };
