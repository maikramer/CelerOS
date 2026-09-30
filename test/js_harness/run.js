#!/usr/bin/env node
// Harness desktop para apps JS do CelerOS: stubs de System/FS/Net + toque e
// teclado scriptados. Roda cada main.js por N iteracoes e confere a saida.
// Uso: node test/js_harness/run.js

'use strict';
var fs = require('fs');
var path = require('path');

var ROOT = path.resolve(__dirname, '..', '..');

// ------------------------------------------------------------ stubs -------
function makeEnv() {
    var log = [];          // tudo que o app "desenha" (drawString)
    var clock = 1000;
    var iters = 0;
    var LIMIT = 200000;
    var env = {};

    var theme = {
        bg: 0x080C18, card: 0x121A2C, raised: 0x1C2640, stroke: 0x2C3850,
        accent: 0x38BCF8, accentD: 0x0C4870, onAccent: 0x081020,
        text: 0xF0F4F8, textDim: 0x8894A8, ok: 0x20C864, warn: 0xF8A010, err: 0xF04848
    };

    // FS em memoria: mapa path -> {dir?:true, data?:string}
    var files = {
        '/local': { dir: true },
        '/sd': { dir: true },
        '/local/apps': { dir: true },
        '/local/config_snake_hi.txt': { data: '17' }
    };
    function parentOf(p) {
        var i = p.lastIndexOf('/');
        return i <= 0 ? '/' : p.substring(0, i);
    }
    function norm(p) {
        var parts = p.split('/');
        var st = [];
        for (var i = 0; i < parts.length; i++) {
            if (parts[i] === '' || parts[i] === '.') continue;
            if (parts[i] === '..') { if (st.length) st.pop(); continue; }
            st.push(parts[i]);
        }
        return '/' + st.join('/');
    }

    var FS = {
        readTextFile: function(p) {
            var e = files[norm(p)];
            return e && e.data !== undefined ? e.data : null;
        },
        writeTextFile: function(p, c) {
            p = norm(p);
            var par = files[parentOf(p)];
            if (!par || !par.dir) return false;
            files[p] = { data: String(c) };
            return true;
        },
        appendTextFile: function(p, c) {
            p = norm(p);
            var e = files[p];
            if (!e || e.data === undefined) return FS.writeTextFile(p, c);
            e.data += String(c);
            return true;
        },
        deleteFile: function(p) {
            p = norm(p);
            if (!files[p] || files[p].dir) return false;
            delete files[p];
            return true;
        },
        renameFile: function(a, b) {
            a = norm(a); b = norm(b);
            // semantica do rename() POSIX (FileSystem::renameFile): substitui
            // arquivo existente; so falha se origem nao existe ou b e diretorio
            if (!files[a] || files[a].dir || (files[b] && files[b].dir)) return false;
            files[b] = files[a];
            delete files[a];
            return true;
        },
        copyFile: function(a, b) {
            a = norm(a); b = norm(b);
            if (!files[a] || files[a].dir) return false;
            return FS.writeTextFile(b, files[a].data);
        },
        exists: function(p) { return !!files[norm(p)]; },
        isDirectory: function(p) { var e = files[norm(p)]; return !!(e && e.dir); },
        isFile: function(p) { var e = files[norm(p)]; return !!(e && !e.dir); },
        listDir: function(p) {
            p = norm(p);
            var out = [];
            var prefix = p === '/' ? '/' : p + '/';
            for (var k in files) {
                if (k.indexOf(prefix) === 0 && k !== p) {
                    var rest = k.substring(prefix.length);
                    if (rest.indexOf('/') >= 0) rest = rest.substring(0, rest.indexOf('/'));
                    if (rest && out.indexOf(prefix + rest) < 0) out.push(prefix + rest);
                }
            }
            return out;
        },
        mkdir: function(p) {
            p = norm(p);
            if (files[p]) return false;
            var par = files[parentOf(p)];
            if (!par || !par.dir) return false;
            files[p] = { dir: true };
            return true;
        },
        rmdir: function(p) {
            p = norm(p);
            var e = files[p];
            if (!e || !e.dir) return false;
            for (var k in files) if (k.indexOf(p + '/') === 0) return false;
            delete files[p];
            return true;
        },
        removeDirectory: function(p) {
            p = norm(p);
            if (!files[p] || !files[p].dir) return false;
            for (var k in files) if (k.indexOf(p + '/') === 0) delete files[k];
            delete files[p];
            return true;
        },
        getFileSize: function(p) { var e = files[norm(p)]; return e && e.data !== undefined ? e.data.length : 0; },
        getTotalSpace: function() { return 384 * 1024; },
        getUsedSpace: function() { return 163 * 1024; },
        getFreeSpace: function() { return 221 * 1024; },
        getFileMD5: function() { return 'deadbeef'; },
        mountSD: function() { return true; },
        unmountSD: function() {}
    };

    // fila de toques: cada item = {x,y,touched} consumido por getTouch
    var touchQ = [];
    // fila de eventos do teclado acoplado
    var kbEvents = [];
    var kbBuffer = '';
    var kbOpen = false;

    env.System = {
        theme: function() { return JSON.parse(JSON.stringify(theme)); },
        color: function(r, g, b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); },
        screenWidth: function() { return 240; },
        screenHeight: function() { return 320; },
        fillScreen: function() {},
        fillRect: function() {},
        drawRect: function() {},
        drawLine: function() {},
        drawPixel: function() {},
        drawCircle: function() {},
        fillCircle: function() {},
        drawTriangle: function() {},
        fillTriangle: function() {},
        drawRoundRect: function() {},
        fillRoundRect: function() {},
        drawFastVLine: function() {},
        drawFastHLine: function() {},
        drawBMP: function() { return true; },
        drawPNG: function() { return true; },
        drawIcon: function() {},
        setTextColor: function() {},
        setTextSize: function() {},
        drawString: function(s) { log.push(String(s)); },
        textWidth: function(s, font) {
            s = String(s);
            var per = font === 1 ? 6 : font === 4 ? 14 : 8;
            return s.length * per;
        },
        millis: function() { return clock; },
        micros: function() { return clock * 1000; },
        delay: function(ms) {
            clock += ms || 0;
            if (++iters > LIMIT) throw { harnessStop: true };
        },
        delayMicroseconds: function() {},
        getTouch: function() {
            if (touchQ.length) return touchQ.shift();
            return { x: 0, y: 0, touched: 0 };
        },
        keypadOpen: function() { kbOpen = true; kbBuffer = ''; return true; },
        keypadPoll: function() {
            if (kbEvents.length) {
                var ev = kbEvents.shift();
                if (ev && ev.type === 'enter') kbBuffer = '';
                if (ev && ev.type === 'cancel') kbOpen = false;
                return ev;
            }
            return null;
        },
        keypadText: function() { return kbBuffer; },
        keypadRect: function() { return { x: 0, y: 186, w: 240, h: 134 }; },
        keypadDraw: function() {},
        keypadClose: function() { kbOpen = false; },
        topbarText: function() {},
        topbarButtons: function() { return 0; },
        topbarPop: function() { return null; },
        prompt: function() { return ''; },
        print: function(s) { log.push('[serial] ' + String(s)); },
        exitApp: function() { throw 'OS_EXIT'; },
        restart: function() { throw 'OS_EXIT'; },
        getOSVersion: function() { return '1.2.0'; },
        getAPILevel: function() { return 10; },
        getInfo: function() {
            return {
                totalRAM: 320000, freeRAM: 150000, minFreeRAM: 120000, maxAllocRAM: 110000, appRAM: 225000, hasLed: true, hasLightSensor: true, hasSpeaker: true,
                totalPSRAM: 0, freePSRAM: 0, cpuFreqMHz: 240, chipModel: 'ESP32',
                chipCores: 2, chipRevision: 1, flashSize: 4194304, uptimeMs: clock * 1000,
                macAddress: 'AA:BB:CC:DD:EE:FF', resetReason: 'power on', idfVersion: 'v6.1'
            };
        },
        getTime: function() { return '10:32'; },
        getSeconds: function() { return 0; },
        getDate: function() { return '27/09/2026'; },
        getYear: function() { return 2026; },
        getMonth: function() { return 9; },
        getDay: function() { return 27; },
        getTimezone: function() { return 'UTC'; },
        wifiStatus: function() { return { connected: false, ip: '', webServer: false, savedNetworks: 0 }; },
        getIPAddress: function() { return ''; },
        isWiFiActive: function() { return false; },
        rescanApps: function() {},
        setBrightness: function() {}, getBrightness: function() { return 200; },
        backlightSupported: function() { return true; },
        led: function(r, g, b) { log.push('[led] ' + [r, g, b].join(',')); return true; },
        gpio: {
            servo: function(pin, angle) { log.push('[servo] ' + pin + '@' + angle); return true; },
            servoOff: function(pin) { log.push('[servo-off] ' + pin); return true; }
        },
        lightLevel: function() { return 80; },
        beep: function() { return true; },
        relay: function() { return true; },
        relayState: function() { return 0; },
        relayCount: function() { return 0; },
        getAutoBrightness: function() { return env.__autoBri; },
        setAutoBrightness: function(on) { env.__autoBri = !!on; return true; },
        present: function() {}, isBuffered: function() { return false; }
    };

    env.FS = FS;
    env.Net = {
        get: function() { return null; },
        getJSON: function() { return null; },
        post: function() { return null; },
        download: function() { return false; },  // streaming p/ arquivo (API 6)
        isConnected: function() { return false; },
        wifiScan: function() {
            return [{ ssid: 'CasaNet', rssi: -50, secure: 1 }, { ssid: 'Vizinho', rssi: -70, secure: 0 }];
        },
        wifiConnect: function() { return false; },
        wifiDisconnect: function() {}
    };

    // Celer Link (API 9): fila de mensagens recebidas alimentavel pelo
    // __harness.pushLink — o mesmo contrato de poll() do firmware
    var linkRx = [];
    env.CelerLink = {
        start: function(name) { log.push('[link] adv ' + (name || 'Celer-TEST')); return true; },
        stop: function() { return true; },
        scan: function() {
            return [{ id: 'AA:BB:CC:DD:EE:FF', name: 'Celer-TEST', rssi: -55 }];
        },
        connect: function(id) { return String(id).indexOf('AA:BB') >= 0; },
        disconnect: function() { return true; },
        send: function(m) {
            log.push('[link] tx ' + (typeof m === 'object' ? JSON.stringify(m) : String(m)));
            return true;
        },
        poll: function() { return linkRx.length ? linkRx.shift() : null; },
        status: function() { return { connected: false, peer: '', listening: false }; }
    };

    env.__harness = {
        log: log,
        pushTouch: function(frames) { touchQ = touchQ.concat(frames); },
        pushKb: function(evts) { kbEvents = kbEvents.concat(evts); },
        pushLink: function(msgs) { linkRx = linkRx.concat(msgs); },
        typeLine: function(text) {
            // simula digitacao: 1 change por char + enter com o texto completo
            kbBuffer = '';
            for (var i = 0; i < text.length; i++) {
                kbBuffer += text.charAt(i);
                kbEvents.push({ type: 'change' });
            }
            kbEvents.push({ type: 'enter', text: text });
        },
        tap: function(x, y) {
            touchQ = touchQ.concat([{ x: x, y: y, touched: 1 }, { x: x, y: y, touched: 0 }]);
        },
        swipe: function(x0, y0, x1, y1) {
            touchQ = touchQ.concat([
                { x: x0, y: y0, touched: 1 },
                { x: (x0 + x1) / 2, y: (y0 + y1) / 2, touched: 1 },
                { x: x1, y: y1, touched: 1 },
                { x: 0, y: 0, touched: 0 }
            ]);
        },
        iters: function() { return iters; },
        System: env.System
    };
    return env;
}

function runApp(relPath, wire) {
    var src = fs.readFileSync(path.join(ROOT, relPath), 'utf8');
    var env = makeEnv();
    wire && wire(env);
    try {
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness);
    } catch (e) {
        if (e === 'OS_EXIT' || (e && e.harnessStop)) return { log: env.__harness.log, err: null, env: env };
        return { log: env.__harness.log, err: e && (e.stack || String(e)) || String(e), env: env };
    }
    return { log: env.__harness.log, err: null, env: env };
}

// Exporta os stubs para outras ferramentas (ex.: tools/app_lint, modo check).
// Os testes abaixo rodam apenas quando executado direto:
//   node test/js_harness/run.js
module.exports = { makeEnv: makeEnv, runApp: runApp };
if (require.main !== module) return;

// ------------------------------------------------------------- testes -----
var failures = 0;
function check(name, cond, extra) {
    if (cond) {
        console.log('  PASS  ' + name);
    } else {
        failures++;
        console.log('  FAIL  ' + name + (extra ? '  [' + extra + ']' : ''));
    }
}

function joinLog(log) { return log.join('\n'); }

// --- Terminal ---------------------------------------------------------------
(function() {
    console.log('Terminal:');
    var cmds = ['help', 'pwd', 'ls', 'js 2+2', 'echo oi mundo > /local/x.txt',
                'cat /local/x.txt', 'mkdir /local/tmpd', 'cd /local/tmpd', 'cd ..',
                'rmdir /local/tmpd', 'neofetch', 'uname -a', 'df', 'free', 'date',
                'whoami', 'history', 'naoexiste', 'echo "aspas duplas" fim', 'exit'];
    var r = runApp('data/apps/Terminal/main.js', function(env) {
        // enfileira a digitacao de todos os comandos de uma vez
        for (var i = 0; i < cmds.length; i++) env.__harness.typeLine(cmds[i]);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    if (process.env.DEBUG_LOG) console.log('---- log ----\n' + j);
    check('welcome', j.indexOf('CelerOS 1.2.0 terminal') >= 0);
    check('help lista comandos', j.indexOf('Comandos:') >= 0);
    check('pwd mostra /local', j.indexOf('root@celeros:/local$') >= 0);
    check('js 2+2 -> 4', j.split('\n').indexOf('4') >= 0);
    check('echo grava e cat le', j.indexOf('oi mundo') >= 0);
    check('ls lista apps', j.indexOf('apps') >= 0);
    check('neofetch mostra OS', j.indexOf('OS: CelerOS') >= 0);
    check('uname -a completo', j.indexOf('IDF v6.1') >= 0);
    check('comando inexistente', j.indexOf('naoexiste: comando não encontrado') >= 0);
    check('history numerado', j.indexOf('17  history') >= 0);
    check('exit sai', r.err === null);
})();

// --- 2048 (hub_apps) --------------------------------------------------------
(function() {
    console.log('2048:');
    var r = runApp('hub_apps/2048/main.js', function(env) {
        // sequencia de swipes: down, left, down, right (qualquer estado valido)
        env.__harness.swipe(120, 90, 120, 240);
        env.__harness.swipe(200, 150, 30, 150);
        env.__harness.swipe(120, 90, 120, 240);
        env.__harness.swipe(30, 150, 200, 150);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('header 2048', j.indexOf('2048') >= 0);
    check('pontuacao desenhada', j.indexOf('PONTOS') >= 0);
})();

// --- Breakout (hub_apps) ----------------------------------------------------
(function() {
    console.log('Breakout:');
    var r = runApp('hub_apps/Breakout/main.js', function(env) {
        // 1o tap lanca a bola (serve->play); os outros arrastam/nao arrastam
        env.__harness.tap(120, 280);
        for (var i = 0; i < 200; i++) env.__harness.System.delay(10);
        env.__harness.tap(60, 280);
        env.__harness.tap(180, 280);
        for (var k = 0; k < 200; k++) env.__harness.System.delay(10);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('placar desenhado', j.indexOf('Pontos 0') >= 0);
})();

// --- Cronometro (hub_apps) --------------------------------------------------
(function() {
    console.log('Cronometro:');
    var r = runApp('hub_apps/Cronometro/main.js', function(env) {
        env.__harness.tap(120, 285);   // iniciar
        for (var i = 0; i < 60; i++) env.__harness.System.delay(10);
        env.__harness.tap(70, 285);    // volta
        env.__harness.tap(170, 285);   // zerar
        for (var k = 0; k < 20; k++) env.__harness.System.delay(10);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('status parado desenhado', j.indexOf('parado') >= 0);
})();

// --- Snake ------------------------------------------------------------------
(function() {
    console.log('Snake:');
    var r = runApp('data/apps/Snake/main.js', function(env) {
        // swipe para baixo, depois para a esquerda; deixa o jogo correr
        env.__harness.swipe(120, 80, 120, 200);
        for (var i = 0; i < 40; i++) env.__harness.System.delay(10);
        env.__harness.swipe(200, 150, 40, 150);
        for (var i2 = 0; i2 < 80; i2++) env.__harness.System.delay(10);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('placar desenhado', j.indexOf('Pontos 0') >= 0);
    check('recorde carregado do FS (17)', j.indexOf('Rec 17') >= 0);
})();

// --- App Store (fluxo de atualizacao via hub) -------------------------------
(function() {
    console.log('App Store:');
    var api = null;
    var MD5 = 'deadbeef';  // o que o stub de FS.getFileMD5 responde
    var r = runApp('data/apps/App Store/main.js', function(env) {
        env.Net.isConnected = function() { return true; };
        env.Net.download = function(url, p, cb) {
            if (!env.FS.writeTextFile(p, 'NOVO-CODIGO')) return false;
            if (cb) { cb(1024, 4000); cb(4096, 4000); }
            return true;
        };
        env.Net.get = function(url) {
            // app.json do hub: versao nova do pacote em atualizacao
            if (String(url).indexOf('app.json') >= 0) {
                return JSON.stringify({ packageName: 'celeros.beta', name: 'Beta',
                                        version: '2.0.0' });
            }
            return null;
        };
        // loja em modo teste: o arquivo termina em storeTest(...) sem loop
        env.__harness.storeTest = function(a) { api = a; };
    });
    check('roda sem erro', r.err === null, r.err || '');
    check('harness recebeu as funcoes da loja', !!api);
    if (!api) return;
    var env = r.env;  // FS/Net vivos p/ semear o "dispositivo" nos checks

    check('cmpV igual', api.cmpV('1.2.0', '1.2.0') === 0);
    check('cmpV maior', api.cmpV('2.0.0', '1.9.9') === 1);
    check('cmpV menor', api.cmpV('1.0.0', '1.0.1') === -1);
    check('fmtKB', api.fmtKB(4000) === '3.9 KB' && api.fmtKB(30720) === '30 KB');

    // FS: beta v1.0.0 instalado (catalogo traz v2.0.0), mesmo v1.0.0 igual
    env.FS.mkdir('/local/apps/celeros.beta');
    env.FS.writeTextFile('/local/apps/celeros.beta/app.json',
        JSON.stringify({ packageName: 'celeros.beta', name: 'Beta', version: '1.0.0' }));
    env.FS.writeTextFile('/local/apps/celeros.beta/main.js', 'CODIGO-ANTIGO');
    env.FS.mkdir('/local/apps/celeros.mesmo');
    env.FS.writeTextFile('/local/apps/celeros.mesmo/app.json',
        JSON.stringify({ packageName: 'celeros.mesmo', name: 'Mesmo', version: '1.0.0' }));
    env.FS.writeTextFile('/local/apps/celeros.mesmo/main.js', 'MMM');
    env.FS.mkdir('/local/apps/celeros.dev');
    env.FS.writeTextFile('/local/apps/celeros.dev/app.json',
        JSON.stringify({ packageName: 'celeros.dev', name: 'Dev', version: '2.1.2' }));
    env.FS.writeTextFile('/local/apps/celeros.dev/main.js', 'DDD');

    api.scanLocalApps();
    api.setCatalog([
        { pkg: 'celeros.beta', metaUrl: 'h/beta/app.json', appUrl: 'h/beta/main.js',
          name: 'Beta', ver: '2.0.0', api: 3, md5: MD5, size: 4000 },
        { pkg: 'celeros.novo', metaUrl: 'h/n/app.json', appUrl: 'h/n/main.js',
          name: 'Novo', ver: '1.0.0', api: 3 },
        { pkg: 'celeros.mesmo', metaUrl: 'h/m/app.json', appUrl: 'h/m/main.js',
          name: 'Mesmo', ver: '1.0.0', api: 3 },
        { pkg: 'celeros.futuro', metaUrl: 'h/f/app.json', appUrl: 'h/f/main.js',
          name: 'Futuro', ver: '1.0.0', api: 11 },
        { pkg: 'celeros.dev', metaUrl: 'h/d/app.json', appUrl: 'h/d/main.js',
          name: 'Dev', ver: '2.0.1', api: 3 }
    ]);

    var st = function(pkg) {
        var it = api.catalog().filter(function(x) { return x.pkg === pkg; })[0];
        return it ? api.stateInfo(it).code : '?';
    };
    check('estado upd (remota maior)', st('celeros.beta') === 'upd');
    check('estado new (nao instalado)', st('celeros.novo') === 'new');
    check('estado inst (mesma versao)', st('celeros.mesmo') === 'inst');
    check('estado api (exige API futura)', st('celeros.futuro') === 'api');
    check('estado inst (local mais nova que o hub: sem downgrade)', st('celeros.dev') === 'inst');
    check('contador de atualizacoes = 1', api.updCount() === 1);

    api.refresh();  // ordena: atualizacao vem primeiro
    check('atualizacao ordenada no topo', api.catalog()[0].pkg === 'celeros.beta');

    // update feliz: main.js trocado, app.json nova versao, sem .new sobrando
    check('update instala', api.installApp() === 'done');
    check('main.js substituido', env.FS.readTextFile('/local/apps/celeros.beta/main.js') === 'NOVO-CODIGO');
    check('staging removido', !env.FS.exists('/local/apps/celeros.beta/main.js.new'));
    check('app.json vira v2.0.0',
          env.FS.readTextFile('/local/apps/celeros.beta/app.json').indexOf('2.0.0') >= 0);
    check('estado vira inst apos update', st('celeros.beta') === 'inst');
    check('contador zera apos update', api.updCount() === 0);

    // icone: baixado e renomeado (sem .new sobrando)
    env.FS.mkdir('/local/apps/celeros.ic');
    env.FS.writeTextFile('/local/apps/celeros.ic/app.json',
        JSON.stringify({ packageName: 'celeros.ic', name: 'Ic', version: '1.0.0' }));
    env.FS.writeTextFile('/local/apps/celeros.ic/main.js', 'IC-ANTIGO');
    api.scanLocalApps();
    api.setCatalog([
        { pkg: 'celeros.ic', metaUrl: 'h/ic/app.json', appUrl: 'h/ic/main.js',
          name: 'Ic', ver: '2.0.0', api: 3, md5: MD5,
          icon: 'http://hub/celeros.ic/icon.png' }
    ]);
    check('update com icone instala', api.installApp() === 'done');
    check('icone gravado', env.FS.exists('/local/apps/celeros.ic/icon.png'));
    check('staging do icone removido', !env.FS.exists('/local/apps/celeros.ic/icon.png.new'));

    // checksum divergente: rejeita e deixa a versao instalada intacta
    env.FS.mkdir('/local/apps/celeros.zeta');
    env.FS.writeTextFile('/local/apps/celeros.zeta/app.json',
        JSON.stringify({ packageName: 'celeros.zeta', name: 'Zeta', version: '1.0.0' }));
    env.FS.writeTextFile('/local/apps/celeros.zeta/main.js', 'ZETA-ANTIGO');
    api.scanLocalApps();
    api.setCatalog([
        { pkg: 'celeros.zeta', metaUrl: 'h/z/app.json', appUrl: 'h/z/main.js',
          name: 'Zeta', ver: '2.0.0', api: 3, md5: 'md5-errado' }
    ]);
    check('update com md5 invalido falha', api.installApp() === 'err');
    check('versao antiga intacta apos md5 invalido',
          env.FS.readTextFile('/local/apps/celeros.zeta/main.js') === 'ZETA-ANTIGO');
    check('staging descartado apos md5 invalido',
          !env.FS.exists('/local/apps/celeros.zeta/main.js.new'));

    // sem espaco: rejeita antes de baixar
    var freeReal = env.FS.getFreeSpace;
    env.FS.getFreeSpace = function() { return 100; };
    api.setCatalog([
        { pkg: 'celeros.grande', metaUrl: 'h/g/app.json', appUrl: 'h/g/main.js',
          name: 'Grande', ver: '1.0.0', api: 3, md5: MD5, size: 30000 }
    ]);
    check('update sem espaco falha', api.installApp() === 'err');
    check('nada gravado sem espaco',
          !env.FS.exists('/local/apps/celeros.grande/main.js'));
    env.FS.getFreeSpace = freeReal;
})();

// --- App Store v3: pasta por packageName, desinstalar, atualizar tudo -------
(function() {
    console.log('App Store v3:');
    var api = null;
    var MD5 = 'deadbeef';
    var dlUrls = [];
    var r = runApp('data/apps/App Store/main.js', function(env) {
        env.Net.isConnected = function() { return true; };
        env.Net.download = function(url, p, cb) {
            dlUrls.push(String(url));
            if (!env.FS.writeTextFile(p, 'NOVO-CODIGO')) return false;
            if (cb) { cb(1024, 4000); cb(4096, 4000); }
            return true;
        };
        env.Net.get = function(url) {
            if (String(url).indexOf('app.json') >= 0) {
                var m = String(url).match(/celeros\.[a-z0-9.]+/);
                return JSON.stringify({ packageName: m ? m[0] : 'x',
                                        name: 'App', version: '2.0.0' });
            }
            return null;
        };
        env.__harness.storeTest = function(a) { api = a; };
    });
    check('v3 roda sem erro', r.err === null, r.err || '');
    check('v3 harness recebeu a loja', !!api);
    if (!api) return;
    var env = r.env;

    // preinstalado com pasta de nome diferente do packageName (caso Terminal)
    env.FS.mkdir('/local/apps/Terminal');
    env.FS.writeTextFile('/local/apps/Terminal/app.json',
        JSON.stringify({ packageName: 'celeros.terminal', name: 'Terminal',
                         version: '1.0.0' }));
    env.FS.writeTextFile('/local/apps/Terminal/main.js', 'TERMINAL-ANTIGO');

    check('resolve pasta pelo packageName',
          api.resolveInstalledDir('celeros.terminal') === '/local/apps/Terminal');
    check('resolve null p/ nao instalado',
          api.resolveInstalledDir('celeros.nada') === null);

    // update do preinstalado: tem que ir NA PASTA existente
    api.setCatalog([
        { pkg: 'celeros.terminal', metaUrl: 'h/celeros.terminal/app.json',
          appUrl: 'h/celeros.terminal/main.js', name: 'Terminal', ver: '2.0.0',
          api: 3, md5: MD5, size: 4000 }
    ]);
    check('update instala', api.installApp() === 'done');
    check('update na pasta certa (sem duplicata)',
          env.FS.readTextFile('/local/apps/Terminal/main.js') === 'NOVO-CODIGO' &&
          !env.FS.exists('/local/apps/celeros.terminal'));

    // duplicata pre-existente: update na 1a pasta e limpa a sombreada
    env.FS.mkdir('/local/apps/celeros.terminal');
    env.FS.writeTextFile('/local/apps/celeros.terminal/app.json',
        JSON.stringify({ packageName: 'celeros.terminal', name: 'Terminal',
                         version: '1.5.0' }));
    env.FS.writeTextFile('/local/apps/celeros.terminal/main.js', 'SOMBRA');
    api.scanLocalApps();
    check('duplicata: primeira vista ganha',
          api.resolveInstalledDir('celeros.terminal') === '/local/apps/Terminal');
    api.setCatalog([
        { pkg: 'celeros.terminal', metaUrl: 'h/celeros.terminal/app.json',
          appUrl: 'h/celeros.terminal/main.js', name: 'Terminal', ver: '2.1.0',
          api: 3, md5: MD5, size: 4000 }
    ]);
    check('update com duplicata instala', api.installApp() === 'done');
    check('copia sombreada removida',
          !env.FS.exists('/local/apps/celeros.terminal') &&
          env.FS.readTextFile('/local/apps/Terminal/main.js') === 'NOVO-CODIGO');

    // desinstalar
    check('desinstala', api.uninstallApp('celeros.terminal') === true &&
                        !env.FS.exists('/local/apps/Terminal'));
    check('desinstala inexistente falha',
          api.uninstallApp('celeros.nada') === false);

    // categorias
    api.setCatalog([
        { pkg: 'celeros.j1', metaUrl: 'h/j1/app.json', appUrl: 'h/j1/main.js',
          name: 'J1', ver: '1.0.0', api: 3, cat: 'Arcade' },
        { pkg: 'celeros.u1', metaUrl: 'h/u1/app.json', appUrl: 'h/u1/main.js',
          name: 'U1', ver: '1.0.0', api: 3, cat: 'Utilidades' }
    ]);
    check('categorias do catalogo',
          api.cats().indexOf('Todos') === 0 &&
          api.cats().indexOf('Arcade') >= 0);
    api.setCat('Arcade');
    check('filtro por categoria', api.filtered().length === 1 &&
                                  api.filtered()[0].pkg === 'celeros.j1');
    api.setCat('Todos');

    // atualizar tudo: self-update (celeros.appstore) vai por ultimo
    env.FS.mkdir('/local/apps/celeros.alpha');
    env.FS.writeTextFile('/local/apps/celeros.alpha/app.json',
        JSON.stringify({ packageName: 'celeros.alpha', name: 'Alpha',
                         version: '1.0.0' }));
    env.FS.writeTextFile('/local/apps/celeros.alpha/main.js', 'A');
    env.FS.mkdir('/local/apps/App Store');
    env.FS.writeTextFile('/local/apps/App Store/app.json',
        JSON.stringify({ packageName: 'celeros.appstore', name: 'App Store',
                         version: '1.0.0' }));
    env.FS.writeTextFile('/local/apps/App Store/main.js', 'LOJA-ANTIGA');
    dlUrls = [];
    api.setCatalog([
        { pkg: 'celeros.appstore', metaUrl: 'h/celeros.appstore/app.json',
          appUrl: 'h/celeros.appstore/main.js', name: 'App Store',
          ver: '2.0.0', api: 6, md5: MD5, size: 4000 },
        { pkg: 'celeros.alpha', metaUrl: 'h/celeros.alpha/app.json',
          appUrl: 'h/celeros.alpha/main.js', name: 'Alpha', ver: '2.0.0',
          api: 3, md5: MD5, size: 4000 }
    ]);
    check('updCount = 2 antes do lote', api.updCount() === 2);
    check('updateAll roda', api.updateAll() === 'batchDone');
    check('updateAll atualiza todos', api.batchStats().ok === 2 &&
                                      api.batchStats().fails.length === 0);
    check('self-update flag', api.selfUpdatedFlag() === true);
    check('self-update por ultimo',
          dlUrls.length === 2 &&
          dlUrls[1].indexOf('celeros.appstore') >= 0);
    check('self-update na propria pasta',
          env.FS.readTextFile('/local/apps/App Store/main.js') === 'NOVO-CODIGO');
    check('updCount = 0 apos lote', api.updCount() === 0);
    check('meus apps lista instalados', api.installed().length === 2);
})();

// --- Celer Link (API 9) ------------------------------------------------------
(function() {
    console.log('CelerLink:');
    var src = [
        'CelerLink.start();',
        'var peers = CelerLink.scan();',
        'System.drawString(peers.length + " pares", 10, 10);',
        'if (peers.length && CelerLink.connect(peers[0].id)) {',
        '  CelerLink.send({cmd:"frente", v:80});',
        '  CelerLink.send("ping");',
        '}',
        'var msg = CelerLink.poll();',
        'System.drawString("rx " + (msg === null ? "-" : msg), 10, 30);',
        'System.delay(10);',
        'System.exitApp();'
    ].join('\n');
    var env = makeEnv();
    env.__harness.pushLink(['{"ack":1}']);
    var err = null;
    try {
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness);
    } catch (e) {
        if (e !== 'OS_EXIT' && !(e && e.harnessStop)) err = e && (e.stack || String(e)) || String(e);
    }
    check('roda sem erro', err === null, err || '');
    var j = joinLog(env.__harness.log);
    check('scan acha o par stub', j.indexOf('1 pares') >= 0);
    check('send objeto vira JSON', j.indexOf('[link] tx {"cmd":"frente","v":80}') >= 0);
    check('send string vai crua', j.indexOf('[link] tx ping') >= 0);
    check('poll recebe mensagem', j.indexOf('rx {"ack":1}') >= 0);
    check('getAPILevel 10', env.System.getAPILevel() === 10);
})();

// --- Celer Remote (hub_apps) --------------------------------------------------
(function() {
    console.log('Celer Remote:');
    var r = runApp('hub_apps/Celer Remote/main.js', function(env) {
        // scan acha o dog; connect verdadeiro; toca no item (y 60..100)
        env.CelerLink.scan = function() {
            return [{ id: 'AA:BB:CC:DD:EE:FF', name: 'Celer-Dog', rssi: -48 }];
        };
        env.CelerLink.connect = function() { return true; };
        env.CelerLink.status = function() { return { connected: true, peer: 'AA:BB:CC:DD:EE:FF', listening: false }; };
        env.__harness.tap(120, 80);    // item 0 da lista -> conecta -> D-pad
        for (var i = 0; i < 3; i++) { env.__harness.System.delay(30); env.__harness.tap(120, 110); }  // seta ^
        for (var k = 0; k < 3; k++) { env.__harness.System.delay(30); env.__harness.tap(120, 170); }  // stop
        env.__harness.pushLink(['{"type":"tel","batt":2340,"state":"parado"}']);
        for (var j = 0; j < 3; j++) env.__harness.System.delay(30);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('conecta no dog', j.indexOf('conectado') >= 0);
    check('D-pad envia move up', j.indexOf('[link] tx {"type":"move","dir":"up"}') >= 0);
    check('botao o envia stop', j.indexOf('[link] tx {"type":"stop"}') >= 0);
    check('telemetria exibida', j.indexOf('batt 2340') >= 0);
})();

// resumo
console.log('');
if (failures) {
    console.log('FALHAS: ' + failures);
    process.exit(1);
}
console.log('OK: todos os testes passaram');
