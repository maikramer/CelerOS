#!/usr/bin/env node
// Harness desktop para apps JS do KryonOS: stubs de System/FS/Net + toque e
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
            if (!files[a] || files[a].dir || files[b]) return false;
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
        prompt: function() { return ''; },
        print: function(s) { log.push('[serial] ' + String(s)); },
        exitApp: function() { throw 'OS_EXIT'; },
        restart: function() { throw 'OS_EXIT'; },
        getOSVersion: function() { return '1.2.0'; },
        getAPILevel: function() { return 5; },
        getInfo: function() {
            return {
                totalRAM: 320000, freeRAM: 150000, minFreeRAM: 120000, maxAllocRAM: 110000,
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
        present: function() {}, isBuffered: function() { return false; }
    };

    env.FS = FS;
    env.Net = {
        get: function() { return null; },
        getJSON: function() { return null; },
        post: function() { return null; },
        isConnected: function() { return false; },
        wifiScan: function() {
            return [{ ssid: 'CasaNet', rssi: -50, secure: 1 }, { ssid: 'Vizinho', rssi: -70, secure: 0 }];
        },
        wifiConnect: function() { return false; },
        wifiDisconnect: function() {}
    };

    env.__harness = {
        log: log,
        pushTouch: function(frames) { touchQ = touchQ.concat(frames); },
        pushKb: function(evts) { kbEvents = kbEvents.concat(evts); },
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
        var fn = new Function('System', 'FS', 'Net', '__harness', src);
        fn(env.System, env.FS, env.Net, env.__harness);
    } catch (e) {
        if (e === 'OS_EXIT' || (e && e.harnessStop)) return { log: env.__harness.log, err: null };
        return { log: env.__harness.log, err: e && (e.stack || String(e)) || String(e) };
    }
    return { log: env.__harness.log, err: null };
}

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
    check('welcome', j.indexOf('KryonOS 1.2.0 terminal') >= 0);
    check('help lista comandos', j.indexOf('Comandos:') >= 0);
    check('pwd mostra /local', j.indexOf('root@kos:/local$') >= 0);
    check('js 2+2 -> 4', j.split('\n').indexOf('4') >= 0);
    check('echo grava e cat le', j.indexOf('oi mundo') >= 0);
    check('ls lista apps', j.indexOf('apps') >= 0);
    check('neofetch mostra OS', j.indexOf('OS: KryonOS') >= 0);
    check('uname -a completo', j.indexOf('IDF v6.1') >= 0);
    check('comando inexistente', j.indexOf('naoexiste: comando nao encontrado') >= 0);
    check('history numerado', j.indexOf('17  history') >= 0);
    check('exit sai', r.err === null);
})();

// --- Calculator -------------------------------------------------------------
(function() {
    console.log('Calculator:');
    // centro das teclas: GX=9, GY=136, BW=51, BH=30, GAP=6
    function keyXY(col, row) {
        return { x: 9 + col * 57 + 25, y: 136 + row * 36 + 15 };
    }
    var r = runApp('data/apps/Calculator/main.js', function(env) {
        // 2 + 3 x 4 =  => 14 (precedencia)
        // grid: R0 C<( ) | R1 789/ | R2 456x | R3 123- | R4 0.=+
        var seq = ['1,3', '3,4', '2,3', '3,2', '0,2', '2,4'];  // 2 + 3 x 4 =
        for (var i = 0; i < seq.length; i++) {
            var rc = seq[i].split(',');
            var p = keyXY(parseInt(rc[0], 10), parseInt(rc[1], 10));
            env.__harness.tap(p.x, p.y);
        }
        // erro: C 5 / 0 =
        var seq2 = ['0,0', '1,2', '3,1', '1,4', '2,4'];
        for (var k = 0; k < seq2.length; k++) {
            var rc2 = seq2[k].split(',');
            var p2 = keyXY(parseInt(rc2[0], 10), parseInt(rc2[1], 10));
            env.__harness.tap(p2.x, p2.y);
        }
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    if (process.env.DEBUG_LOG) console.log('---- log ----\n' + j);
    check('expressao exibida', j.indexOf('2+3x4') >= 0, j);
    check('precedencia 2+3x4=14', j.indexOf('= 14') >= 0);
    check('divisao por zero -> Erro', j.indexOf('Erro') >= 0);
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
    check('header desenhado', j.indexOf('Snake') >= 0);
    check('recorde carregado do FS (17)', j.indexOf('Rec 17') >= 0);
})();

// resumo
console.log('');
if (failures) {
    console.log('FALHAS: ' + failures);
    process.exit(1);
}
console.log('OK: todos os testes passaram');
