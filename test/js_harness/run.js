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

    // ---- Timers (API 12): mesmo modelo do firmware — disparam no delay/
    // getTouch (os pontos de "present"); erro do callback propaga.
    var timers = [];
    var nextTid = 1;
    function fireTimers() {
        for (var i = 0; i < timers.length; i++) {
            var t = timers[i];
            if (clock >= t.at) {
                if (t.repeat) {
                    do { t.at += t.delay; } while (clock >= t.at);
                } else {
                    timers.splice(i--, 1);
                }
                t.fn();
            }
        }
    }
    function timerAdd(fn, ms, repeat) {
        if (typeof fn !== 'function' || timers.length >= 8) return 0;
        var d = Math.max(10, ms | 0);
        timers.push({ id: nextTid, fn: fn, delay: d, repeat: repeat, at: clock + d });
        return nextTid++;
    }
    function timerDel(id) {
        for (var i = 0; i < timers.length; i++)
            if (timers[i].id === id) { timers.splice(i, 1); return; }
    }

    // ---- Storage (API 12): NVS privado do app — mapa em memoria por runApp
    var storageMap = {};

    // Cores do tema no espaco do JS: RGB565, igual ao firmware (js_theme
    // converte THEME_* de RGB888 antes do push — o stub faz o mesmo)
    function to565(c) {
        return ((((c >> 16) & 0xFF) >> 3) << 11) | ((((c >> 8) & 0xFF) >> 2) << 5) | ((c & 0xFF) >> 3);
    }
    var theme = {
        bg: to565(0x080C18), card: to565(0x121A2C), raised: to565(0x1C2640), stroke: to565(0x2C3850),
        accent: to565(0x38BCF8), accentD: to565(0x0C4870), onAccent: to565(0x081020),
        text: to565(0xF0F4F8), textDim: to565(0x8894A8), ok: to565(0x20C864), warn: to565(0xF8A010), err: to565(0xF04848)
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
        readFile: function(p, maxLen) {
            p = norm(p);
            var e = files[p];
            if (!e || e.dir) return null;
            var d = e.data;
            var cap = typeof maxLen === 'number' ? maxLen : 16384;
            return d.substring(0, cap);
        },
        writeFile: function(p, c) {
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
            fireTimers();
            clock += ms || 0;
            if (++iters > LIMIT) throw { harnessStop: true };
        },
        delayMicroseconds: function() {},
        getTouch: function() {
            fireTimers();
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
        getAPILevel: function() { return 12; },
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
        battery: function() { return 4100; },
        micLevel: function() { return 12; },
        touchPad: function() { return 0; },
        print: function(s) { log.push('[print] ' + s); },
        neopixel: function() { return true; },
        getAutoBrightness: function() { return env.__autoBri; },
        setAutoBrightness: function(on) { env.__autoBri = !!on; return true; },
        present: function() { fireTimers(); }, isBuffered: function() { return false; },
        useSprite: function() { return true; },
        setTextDatum: function() {},
        // nivel 3 / apps de sistema: PIN, config, web, OTA e hora (Settings)
        setPin: function() { return true; },
        verifyPin: function() { return true; },
        pinClear: function() { log.push('[pin] clear'); return true; },
        pinState: function() { return 0; },
        md5: function() { return 'd41d8cd98f00b204e9800998ecf8427e'; },
        setting: function() { return ''; },
        toast: function(s) { log.push('[toast] ' + String(s)); },
        openWifiSetup: function() { log.push('[wifi] setup'); return true; },
        factoryReset: function(m) { log.push('[factoryReset] ' + m); return true; },
        otaCheck: function() {
            return { fetchFailed: true, available: false, hasFirmware: false,
                     version: '', url: '', changelog: '', guide: '', type: '' };
        },
        otaStart: function() { return false; },
        webActive: function() { return true; },
        webSetActive: function() {},
        webAuthInfo: function() { return { user: 'admin', pass: 'senha-web' }; },
        webAuthSetPass: function() { return true; },
        setManualTime: function() { log.push('[time] manual'); return true; },
        setTimezone: function() { return true; },
        set24hFormat: function() {},
        get24hFormat: function() { return 1; },
        setNtpEnabled: function() {},
        getNtpEnabled: function() { return 1; },
        // Energia/alarme (API 12)
        setScreenTimeout: function(ms) { env.__scrTmo = ms | 0; },
        screenTimeout: function() { return env.__scrTmo || 0; },
        deepSleep: function() { log.push('[deepSleep] ' + arguments[0] + 'ms'); throw 'OS_EXIT'; },
        setAlarm: function(h, m, msg) {
            if (h < 0 || h > 23 || m < 0 || m > 59) return false;
            env.__alarm = { armed: true, hour: h, minute: m, msg: msg || '' };
            return true;
        },
        clearAlarm: function() { env.__alarm = null; },
        getAlarm: function() { return env.__alarm ? JSON.parse(JSON.stringify(env.__alarm)) : null; }
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

    // Celer Link (API 9; pareamento API 11): fila de mensagens recebidas
    // alimentavel pelo __harness.pushLink — o mesmo contrato de poll() do
    // firmware. __harness.setLink({conn,pairing,code}) simula os estados
    // do handshake; verify() aceita so o codigo corrente.
    var linkRx = [];
    var linkConn = false, linkPairing = false, linkPairCode = '123456';
    env.CelerLink = {
        start: function(name, opts) {
            log.push('[link] adv ' + (name || 'Celer-TEST') + (opts && opts.pairing ? ' +pairing' : ''));
            return true;
        },
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
        verify: function(code) { log.push('[link] verify ' + code); return linkPairing && code === linkPairCode; },
        unpair: function() { log.push('[link] unpair'); return true; },
        status: function() {
            return { connected: linkConn && !linkPairing, peer: linkConn ? 'AA:BB:CC:DD:EE:FF' : '',
                     listening: false, role: linkConn ? 'central' : '', name: 'Celer-TEST',
                     pairing: linkPairing, verified: !linkPairing,
                     code: linkPairing ? linkPairCode : '',
                     mtu: 0, rssi: 0, pending: linkRx.length, dropped: 0 };
        }
    };

    env.__harness = {
        log: log,
        setLink: function(st) {
            if (st.hasOwnProperty('conn')) linkConn = !!st.conn;
            if (st.hasOwnProperty('pairing')) linkPairing = !!st.pairing;
            if (st.hasOwnProperty('code')) linkPairCode = String(st.code);
        },
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
    env.Storage = {
        get: function(k, def) { return storageMap.hasOwnProperty(k) ? storageMap[k] : def; },
        set: function(k, v) {
            v = v === undefined ? '' : String(v);
            if (v.length > 4096) return false;
            storageMap[k] = v;
            return true;
        },
        remove: function(k) { delete storageMap[k]; },
        clear: function() { storageMap = {}; return true; },
        clearFor: function() { return true; }
    };
    env.__storage = storageMap;
    env.setTimeout = function (fn, ms) { return timerAdd(fn, ms, false); };
    env.setInterval = function (fn, ms) { return timerAdd(fn, ms, true); };
    env.clearTimeout = timerDel;
    env.clearInterval = timerDel;
    return env;
}

function runApp(relPath, wire) {
    var src = fs.readFileSync(path.join(ROOT, relPath), 'utf8');
    var env = makeEnv();
    wire && wire(env);
    try {
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness',
                              'Storage', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness,
           env.Storage, env.setTimeout, env.setInterval, env.clearTimeout, env.clearInterval);
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
          name: 'Futuro', ver: '1.0.0', api: 99 },
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
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness',
                              'Storage', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness,
           env.Storage, env.setTimeout, env.setInterval, env.clearTimeout, env.clearInterval);
    } catch (e) {
        if (e !== 'OS_EXIT' && !(e && e.harnessStop)) err = e && (e.stack || String(e)) || String(e);
    }
    check('roda sem erro', err === null, err || '');
    var j = joinLog(env.__harness.log);
    check('scan acha o par stub', j.indexOf('1 pares') >= 0);
    check('send objeto vira JSON', j.indexOf('[link] tx {"cmd":"frente","v":80}') >= 0);
    check('send string vai crua', j.indexOf('[link] tx ping') >= 0);
    check('poll recebe mensagem', j.indexOf('rx {"ack":1}') >= 0);
    check('getAPILevel 12', env.System.getAPILevel() === 12);
})();

// --- Dog Face (robo: cara + gaits + protocolo do Celer Remote) ---------------
// Agenda de mensagens por relogio: poll() entrega cada item quando o
// millis() do harness passa do seu instante (relativo ao 1o poll).
function linkSchedule(env, items) {
    var t0 = -1;
    env.CelerLink.poll = function() {
        var now = env.System.millis();
        if (t0 < 0) t0 = now;
        if (items.length && now - t0 >= items[0][0]) return items.shift()[1];
        return null;
    };
}

// Sequencia de angulos escritos num pino, na ordem ('[servo] pin@ang').
function servoSeq(log, pin) {
    var out = [], pre = '[servo] ' + pin + '@';
    for (var i = 0; i < log.length; i++) {
        var l = String(log[i]);
        if (l.indexOf(pre) === 0) out.push(parseInt(l.substring(pre.length), 10));
    }
    return out;
}
function seqHas(seq, sub) {
    outer: for (var i = 0; i + sub.length <= seq.length; i++) {
        for (var k = 0; k < sub.length; k++) if (seq[i + k] !== sub[k]) continue outer;
        return true;
    }
    return false;
}
function range(a, b) {  // inclusivo, crescente ou decrescente
    var r = [], d = a <= b ? 1 : -1;
    for (var v = a; v !== b + d; v += d) r.push(v);
    return r;
}
function count(seq, v) { var n = 0; for (var i = 0; i < seq.length; i++) if (seq[i] === v) n++; return n; }

// Mapa cru/fisico -> angulo de servo lido do PROPRIO app (SIGN/NEUTRAL/FWD):
// os testes seguem a calibracao feita no cao, sem numeros magicos.
var DOG_SRC = fs.readFileSync(path.join(ROOT, 'boards/spotpear-dog/data/apps/Dog Face/main.js'), 'utf8');
function dogTable(name) {
    var m = new RegExp('var ' + name + ' = \\{ FL: (-?\\d+), FR: (-?\\d+), BL: (-?\\d+), BR: (-?\\d+) \\}').exec(DOG_SRC);
    return { FL: +m[1], FR: +m[2], BL: +m[3], BR: +m[4] };
}
var D_SIGN = dogTable('SIGN'), D_NEU = dogTable('NEUTRAL'), D_FWD = dogTable('FWD');
var D_PIN = { FL: 17, FR: 13, BL: 18, BR: 14 };
function rawAng(k, raw) { return Math.round(D_NEU[k] + D_SIGN[k] * raw); }
function physAng(k, a) { return rawAng(k, D_FWD[k] * a); }
function rawRange(k, a, b) { return range(a, b).map(function(v) { return rawAng(k, v); }); }
function dogSeqs(log) {
    return { FL: servoSeq(log, 17), FR: servoSeq(log, 13), BL: servoSeq(log, 18), BR: servoSeq(log, 14) };
}
function allNeutralAtEnd(q) {
    return ['FL', 'FR', 'BL', 'BR'].every(function(k) { return q[k][q[k].length - 1] === D_NEU[k]; });
}
function holdMoves(ms) {
    var items = [];
    for (var t = 0; t <= ms; t += 250) items.push([t, '{"type":"move","dir":"up"}']);
    return items;
}

(function() {
    console.log('Dog Face (marcha ESP-Hi, modo esphi):');
    var r = runApp('boards/spotpear-dog/data/apps/Dog Face/main.js', function(env) {
        env.CelerLink.status = function() { return { connected: true }; };
        // seta segurada 2 s (move a cada 250 ms), depois SOME sem stop
        // (stop perdido): o keepalive tem que parar o robo sozinho
        var items = [[0, '{"type":"mode","walk":"esphi"}']];
        holdMoves(2000).forEach(function(it) { items.push([it[0] + 100, it[1]]); });
        items.push([6000, '{"cmd":"pet"}']);
        linkSchedule(env, items);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var q = dogSeqs(r.log);
    check('boot em pe (neutro nas 4)', q.FL[0] === D_NEU.FL && q.FR[0] === D_NEU.FR && q.BL[0] === D_NEU.BL && q.BR[0] === D_NEU.BR);
    // servo_dog_forward, fase A (cru): FL = B - i, FR = F - i - 5, BL = B + i - 5, BR = F + i
    check('fase A identica ao ESP-Hi',
          seqHas(q.FL, rawRange('FL', 20, -19)) && seqHas(q.FR, rawRange('FR', 15, -24)) &&
          seqHas(q.BL, rawRange('BL', -25, 14)) && seqHas(q.BR, rawRange('BR', -20, 19)));
    // fase B: FL = F + i, FR = B + i + 5, BL = F - i + 5, BR = B - i
    check('fase B identica ao ESP-Hi',
          seqHas(q.FL, rawRange('FL', -20, 19)) && seqHas(q.FR, rawRange('FR', -15, 24)) &&
          seqHas(q.BL, rawRange('BL', 25, -14)) && seqHas(q.BR, rawRange('BR', 20, -19)));
    check('assimetria do STEP_OFFSET (FR -24->-15, BL 14->25)',
          seqHas(q.FR, [rawAng('FR', -24), rawAng('FR', -15)]) && seqHas(q.BL, [rawAng('BL', 14), rawAng('BL', 25)]));
    check('move repetido nao reinicia o passo (A nunca emenda em A)', !seqHas(q.FL, [rawAng('FL', -19), rawAng('FL', 20)]));
    check('varios ciclos enquanto segura', count(q.FL, rawAng('FL', 20)) >= 3, count(q.FL, rawAng('FL', 20)) + ' ciclos');
    check('keepalive expira e volta ao neutro', allNeutralAtEnd(q));
    check('telemetria com o modo', joinLog(r.log).indexOf('"mode":"esphi"') >= 0);
})();

(function() {
    console.log('Dog Face (outras marchas ESP-Hi + calibracao):');
    var r = runApp('boards/spotpear-dog/data/apps/Dog Face/main.js', function(env) {
        env.CelerLink.status = function() { return { connected: true }; };
        linkSchedule(env, [
            [0, '{"type":"mode","walk":"esphi"}'],
            [100, '{"type":"gait","name":"back"}'],
            [3000, '{"type":"gait","name":"left"}'],
            [6000, '{"type":"gait","name":"right"}'],
            [9000, '{"type":"calib"}']
        ]);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var q = dogSeqs(r.log);
    // backward A: FL = F + i - 5, BR = B - i - 5
    check('back = servo_dog_backward', seqHas(q.FL, rawRange('FL', -25, 14)) && seqHas(q.BR, rawRange('BR', 15, -24)));
    // turn_left A: FL = B - i + 5, FR = B + i
    check('left = servo_dog_turn_left', seqHas(q.FL, rawRange('FL', 25, -14)) && seqHas(q.FR, rawRange('FR', -20, 19)));
    // turn_right A: FL = F + i, FR = F - i + 5
    check('right = servo_dog_turn_right', seqHas(q.FL, rawRange('FL', -20, 19)) && seqHas(q.FR, rawRange('FR', 25, -14)));
    check('gait sem repeat para sozinha (2 ciclos) no neutro', q.FL[q.FL.length - 1] === D_NEU.FL);
    // calib: cada perna 25 graus pra FRENTE (fisico), em ordem
    var j = joinLog(r.log);
    var iFL = j.indexOf('[servo] 17@' + physAng('FL', 25)), iFR = j.indexOf('[servo] 13@' + physAng('FR', 25), iFL),
        iBL = j.indexOf('[servo] 18@' + physAng('BL', 25), iFR), iBR = j.indexOf('[servo] 14@' + physAng('BR', 25), iBL);
    check('calib FL,FR,BL,BR pra frente em ordem', iFL >= 0 && iFR > iFL && iBL > iFR && iBR > iBL);
})();

(function() {
    console.log('Dog Face (centopeia, modo creep default):');
    var P = 20, T = 25;
    var r = runApp('boards/spotpear-dog/data/apps/Dog Face/main.js', function(env) {
        env.CelerLink.status = function() { return { connected: true }; };
        var items = holdMoves(7000);
        items.push([12000, '{"type":"tune","P":30,"T":20,"order":["FR","BR","FL","BL"]}']);
        holdMoves(4000).forEach(function(it) { items.push([it[0] + 12100, it[1]]); });
        linkSchedule(env, items);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    function at(k, a, from) { return j.indexOf('[servo] ' + D_PIN[k] + '@' + physAng(k, a), from || 0); }
    // 1. remada: as 4 chegam juntas a -P (pata pra tras, corpo pra frente)
    var pw = Math.max(at('FL', -P), at('FR', -P), at('BL', -P), at('BR', -P));
    check('remada leva as 4 patas a -P', at('FL', -P) >= 0 && at('FR', -P) >= 0 && at('BL', -P) >= 0 && at('BR', -P) >= 0);
    // 2. recuperacao BL: encurta FR (-P-T), BL vai a +P no ar, FR volta a -P
    var tFR = at('FR', -P - T, pw), sBL = at('BL', P, tFR), uFR = at('FR', -P, sBL);
    check('BL recupera com FR encurtado (diagonal oposta)', tFR > pw && sBL > tFR && uFR > sBL);
    // depois FL com BR encurtado, BR com FL (ja na frente: +P+T), FR com BL (+P+T)
    var tBR = at('BR', -P - T, uFR), sFL = at('FL', P, tBR);
    var tFL = at('FL', P + T, sFL), sBR = at('BR', P, tFL);
    var tBL = at('BL', P + T, sBR), sFR = at('FR', P, tBL);
    check('ordem creep BL, FL, BR, FR com o canto oposto certo',
          tBR > uFR && sFL > tBR && tFL > sFL && sBR > tFL && tBL > sBR && sFR > tBL);
    check('nenhuma pata recua durante a recuperacao dela (so a remada empurra)',
          j.indexOf('[servo] ' + D_PIN.BL + '@' + physAng('BL', -P - T)) < 0);
    var q = dogSeqs(r.log);
    check('varios ciclos enquanto segura', count(q.FR, physAng('FR', -P - T)) >= 2, count(q.FR, physAng('FR', -P - T)) + ' ciclos');
    // tune ao vivo: P=30/T=20, ordem FR primeiro (unload BL a -30-20)
    check('tune responde e vale no proximo ciclo',
          j.indexOf('[link] tx {"type":"tune","P":30,"T":20') >= 0 && at('BL', -30 - 20) >= 0 &&
          at('FR', 30, at('BL', -30 - 20)) > 0);
    check('tune salvo em /local/dogtune.json', r.env.FS.exists('/local/dogtune.json') &&
          JSON.parse(r.env.FS.readTextFile('/local/dogtune.json')).P === 30);
    check('keepalive expira e volta ao neutro', allNeutralAtEnd(q));
})();

(function() {
    console.log('Dog Face (link cai andando):');
    var r = runApp('boards/spotpear-dog/data/apps/Dog Face/main.js', function(env) {
        var polls = 0;
        env.CelerLink.status = function() { return { connected: polls < 12 }; };
        var inner = null;
        linkSchedule(env, [[0, '{"type":"move","dir":"up"}']]);
        inner = env.CelerLink.poll;
        env.CelerLink.poll = function() { polls++; return inner(); };
    });
    check('roda sem erro', r.err === null, r.err || '');
    var q = dogSeqs(r.log);
    check('andou antes da queda', q.FR.indexOf(physAng('FR', -20)) >= 0);
    check('queda do link para o robo (neutro)', allNeutralAtEnd(q));
})();

// Pad capacitivo roteirizado por relogio: [[t0, t1], ...] = tocado entre
// t0 e t1 (ms relativos a 1a leitura).
function padSchedule(env, spans) {
    var t0 = -1;
    env.System.touchPad = function() {
        var now = env.System.millis();
        if (t0 < 0) t0 = now;
        var t = now - t0;
        for (var i = 0; i < spans.length; i++) if (t >= spans[i][0] && t < spans[i][1]) return 1;
        return 0;
    };
}

(function() {
    console.log('Dog Face (sem link, pad e sono):');
    // sem nenhuma mensagem: variaveis do link declaradas (ReferenceError
    // antigo so aparecia quando nada chegava)
    var quiet = runApp('boards/spotpear-dog/data/apps/Dog Face/main.js', function() {});
    check('roda sem mensagem alguma', quiet.err === null, quiet.err || '');

    // toque longo (1 s) -> cycleGait (stand -> walk) SEM link: nao pode parar
    var r = runApp('boards/spotpear-dog/data/apps/Dog Face/main.js', function(env) {
        padSchedule(env, [[500, 1500]]);
    });
    check('roda sem erro (pad)', r.err === null, r.err || '');
    var FRp = servoSeq(r.log, 13);
    check('toque longo inicia walk', FRp.indexOf(physAng('FR', -45)) >= 0);
    check('walk do pad segue sem link (sem keepalive)', count(FRp, physAng('FR', -45)) >= 3,
          count(FRp, physAng('FR', -45)) + ' ciclos');

    // parado e sem link: dorme apos 2 min e solta os servos; barulho acorda
    var r2 = runApp('boards/spotpear-dog/data/apps/Dog Face/main.js', function(env) {
        var t0 = -1;
        env.System.micLevel = function() {
            var now = env.System.millis();
            if (t0 < 0) t0 = now;
            return now - t0 > 200000 && now - t0 < 202000 ? 80 : 5;
        };
    });
    check('roda sem erro (sono)', r2.err === null, r2.err || '');
    var j2 = joinLog(r2.log);
    var off = j2.indexOf('[servo-off] 14');
    check('sono solta os servos', off >= 0);
    check('barulho acorda e retoma a pose', off >= 0 && j2.indexOf('[servo] 14@90', off) > off);
})();

(function() {
    console.log('Dog Face (pareamento por codigo):');
    // handshake pendente -> virou verified na 30a leitura do status; pad
    // segurado 5,1 s solta os bonds. O gate de mensagens e do firmware:
    // aqui se testa o lado do app (tela de codigo, telemetria so depois
    // de liberado, gesto de esquecer).
    var r = runApp('boards/spotpear-dog/data/apps/Dog Face/main.js', function(env) {
        var calls = 0;
        env.CelerLink.status = function() {
            calls++;
            if (calls < 30) return { connected: false, pairing: true, verified: false, code: '314159' };
            if (calls === 30) env.__harness.log.push('[harness] verified');
            return { connected: true, pairing: false, verified: true };
        };
        padSchedule(env, [[500, 5600]]);   // hold 5,1 s -> esquece pareados
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('start pede pareamento', j.indexOf('[link] adv Celer-Dog +pairing') >= 0);
    var flip = j.indexOf('[harness] verified');
    var firstTel = j.indexOf('[link] tx {"type":"tel"');
    check('sem telemetria enquanto o codigo pendura', flip >= 0 && (firstTel < 0 || firstTel > flip));
    check('telemetria volta liberado', firstTel > flip);
    check('hold 5s esquece os pareados', j.indexOf('[link] unpair') >= 0 &&
          j.indexOf('pareamentos esquecidos') >= 0);
    // o hold de 5s NAO pode disparar o ciclo de gait do toque longo
    check('hold 5s nao e carinho nem troca de gait', j.indexOf('touch gait') < 0);
})();

// --- Celer Remote (hub_apps) --------------------------------------------------
function holdFrames(x, y, n) {
    var f = [];
    for (var i = 0; i < n; i++) f.push({ x: x, y: y, touched: 1 });
    f.push({ x: 0, y: 0, touched: 0 });
    return f;
}

(function() {
    console.log('Celer Remote:');
    var r = runApp('hub_apps/Celer Remote/main.js', function(env) {
        // scan acha o dog; connect verdadeiro; toca no item (y 66..108)
        env.CelerLink.scan = function() {
            return [{ id: 'AA:BB:CC:DD:EE:FF', name: 'Celer-Dog', rssi: -48 }];
        };
        env.CelerLink.connect = function() { return true; };
        env.CelerLink.status = function() {
            return { connected: true, peer: 'AA:BB:CC:DD:EE:FF', listening: false, rssi: -50 };
        };
        env.__harness.tap(120, 80);                             // item 0 -> conecta -> D-pad
        env.__harness.pushTouch(holdFrames(120, 110, 20));      // segura ^ ~600 ms e solta
        env.__harness.tap(120, 170);                            // botao o
        env.__harness.pushLink(['{"type":"tel","batt":2340,"state":"parado"}']);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('conecta no dog', j.indexOf('< sair') >= 0 && j.indexOf('Celer-Dog') >= 0);
    check('D-pad envia move up', j.indexOf('[link] tx {"type":"move","dir":"up"}') >= 0);
    var moves = j.split('[link] tx {"type":"move","dir":"up"}').length - 1;
    check('segurar repete o move (keepalive)', moves >= 2, moves + ' moves');
    check('soltar a seta envia stop',
          j.indexOf('[link] tx {"type":"stop"}') > j.lastIndexOf('[link] tx {"type":"move"'));
    check('telemetria exibida', j.indexOf('batt 2340') >= 0);
})();

(function() {
    console.log('Celer Remote (botao de marcha):');
    var r = runApp('hub_apps/Celer Remote/main.js', function(env) {
        env.CelerLink.scan = function() { return [{ id: 'AA:BB:CC:DD:EE:FF', name: 'Celer-Dog', rssi: -48 }]; };
        env.CelerLink.connect = function() { return true; };
        env.CelerLink.status = function() { return { connected: true, peer: 'AA:BB:CC:DD:EE:FF', listening: false }; };
        env.__harness.tap(120, 80);
        // robo 1.4.1+: tel.modes com 2 funcionais -> botao aparece
        env.__harness.pushLink(['{"type":"tel","batt":4100,"state":"stand","mode":"creep","modes":["creep","esphi"]}']);
        env.__harness.pushTouch([{ x: 0, y: 0, touched: 0 }, { x: 0, y: 0, touched: 0 }]);
        env.__harness.tap(120, 268);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('mostra a marcha do robo (tel.modes >= 2)', j.indexOf('marcha: creep') >= 0);
    check('toque troca por nome ({"type":"mode","walk":...})',
          j.indexOf('[link] tx {"type":"mode","walk":"esphi"}') >= 0);
})();

(function() {
    console.log('Celer Remote (sem tel.modes nao ha botao):');
    var r = runApp('hub_apps/Celer Remote/main.js', function(env) {
        env.CelerLink.scan = function() { return [{ id: 'AA:BB:CC:DD:EE:FF', name: 'Celer-Dog', rssi: -48 }]; };
        env.CelerLink.connect = function() { return true; };
        env.CelerLink.status = function() { return { connected: true, peer: 'AA:BB:CC:DD:EE:FF', listening: false }; };
        env.__harness.tap(120, 80);
        // robo antigo (1.4.0) / sem modes: um toque ali nao pode trocar marcha
        // as cegas — era assim que o esphi ficava salvo no dog
        env.__harness.pushLink(['{"type":"tel","batt":4100,"state":"stand","mode":"creep"}']);
        env.__harness.pushTouch([{ x: 0, y: 0, touched: 0 }, { x: 0, y: 0, touched: 0 }]);
        env.__harness.tap(120, 268);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('sem tel.modes o botao nao aparece', j.indexOf('marcha:') < 0);
    check('nenhum mode sai do remote', j.indexOf('[link] tx {"type":"mode"') < 0);
})();

(function() {
    console.log('Celer Remote (queda e reconexao):');
    var connects = 0;
    var r = runApp('hub_apps/Celer Remote/main.js', function(env) {
        env.CelerLink.scan = function() {
            return [{ id: 'AA:BB:CC:DD:EE:FF', name: 'Celer-Dog', rssi: -48 }];
        };
        env.CelerLink.connect = function() { connects++; return connects !== 2; };  // 1a reconexao falha
        var calls = 0;
        env.CelerLink.status = function() {
            calls++;
            return { connected: !(calls >= 6 && calls < 8), peer: 'AA:BB:CC:DD:EE:FF', listening: false };
        };
        env.__harness.tap(120, 80);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('mostra reconectando', j.indexOf('reconectando (1/3)') >= 0);
    check('reconecta na 2a tentativa', connects === 3, connects + ' connects');
})();

(function() {
    console.log('Celer Remote (pareamento por codigo):');
    var r = runApp('hub_apps/Celer Remote/main.js', function(env) {
        env.CelerLink.scan = function() {
            return [{ id: 'AA:BB:CC:DD:EE:FF', name: 'Celer-Dog', rssi: -48 }];
        };
        env.CelerLink.connect = function() { return true; };
        var verified = false;
        env.CelerLink.status = function() {
            return { connected: verified, pairing: !verified, verified: verified,
                     peer: 'AA:BB:CC:DD:EE:FF', listening: false };
        };
        env.CelerLink.verify = function(code) {
            env.__harness.log.push('[link] verify ' + code);
            if (code === '123456') { verified = true; return true; }
            return false;
        };
        var answers = ['000000', '123456'];   // erra 1x, acerta
        env.System.prompt = function() { return answers.length ? answers.shift() : ''; };
        env.__harness.tap(120, 80);                            // item 0 -> connect -> pairing
        env.__harness.pushTouch(holdFrames(120, 110, 20));     // D-pad liberado: move up
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('pede o codigo (tela de pareamento)', j.indexOf('codigo na tela do robo') >= 0);
    check('tenta o codigo errado e o certo',
          j.indexOf('[link] verify 000000') >= 0 && j.indexOf('[link] verify 123456') >= 0);
    check('pareado chega ao D-pad', j.indexOf('pareado!') >= 0 &&
          j.indexOf('[link] tx {"type":"move","dir":"up"}') >= 0);
})();

// Settings: smoke do app de sistema (menu, navegacao, acoes com efeito)
(function() {
    console.log('Settings (menu e navegacao):');
    var r = runApp('data/apps/Settings/main.js');
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('menu desenha as secoes', j.indexOf('Wi-Fi') >= 0 && j.indexOf('Aplicativos') >= 0);
    check('estado do Wi-Fi e PIN no menu', j.indexOf('OFF') >= 0 && j.indexOf('--') >= 0);
})();

(function() {
    console.log('Settings (hora: toggle NTP):');
    var calls = [];
    var r = runApp('data/apps/Settings/main.js', function(env) {
        var orig = env.System.setNtpEnabled;
        env.System.setNtpEnabled = function(v) { calls.push(v); return orig(v); };
        // 1) abre "Hora e fuso" (linha 2 do menu), 2) toggle NTP (linha 2
        // da tela de hora), 3) "< Voltar" (rodape) volta ao menu
        env.__harness.tap(120, 159);
        env.__harness.tap(120, 159);
        env.__harness.tap(50, 297);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('tela de hora desenha NTP', j.indexOf('NTP (internet)') >= 0);
    check('toggle NTP chama setNtpEnabled', calls.length === 1, 'calls=' + JSON.stringify(calls));
    check('voltar redesenha o menu', j.indexOf('Wi-Fi') >= 0);
})();

(function() {
    console.log('Settings (tela: brilho):');
    var gets = 0;
    var r = runApp('data/apps/Settings/main.js', function(env) {
        var orig = env.System.getBrightness;
        env.System.getBrightness = function() { gets++; return orig(); };
        // abre "Tela" (linha 4 do menu)
        env.__harness.tap(120, 247);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('tela de brilho desenha', j.indexOf('Brilho') >= 0);
    check('le o brilho atual ao entrar', gets >= 1, 'gets=' + gets);
})();

// --- API 12: timers, Storage, FS binario ------------------------------------
(function() {
    console.log('API 12 (timers):');
    var src = [
        'var tiro = 0, tic = 0;',
        'setTimeout(function () { tiro++; }, 100);',
        'var iv = setInterval(function () { tic++; }, 50);',
        'for (var i = 0; i < 4; i++) System.delay(60);',  // 4 cedidas: tic 4x
        'var antes = tic;',
        'clearInterval(iv);',
        'System.delay(120);',              // interval cancelado: nao avanca
        'System.drawString(tiro + "/" + tic + "/" + antes, 1, 1);',
        'System.exitApp();'
    ].join('\n');
    var env = makeEnv();
    var err = null;
    try {
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness',
                              'Storage', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness,
           env.Storage, env.setTimeout, env.setInterval, env.clearTimeout, env.clearInterval);
    } catch (e) {
        if (e !== 'OS_EXIT' && !(e && e.harnessStop)) err = e && (e.stack || String(e)) || String(e);
    }
    check('roda sem erro', err === null, err || '');
    var j = joinLog(env.__harness.log);
    // 4 cedidas de 60ms: timeout(100) 1x; interval(50) 3x (1 por cedida,
    // com catch-up — mesmo modelo do firmware, sem rajada)
    check('setTimeout dispara 1x', j.indexOf('1/') >= 0, j);
    check('setInterval dispara 3x (uma por cedida)', j.indexOf('/3/3') >= 0, j);
    check('clearInterval para o interval', j.indexOf('1/3/3') >= 0, j);
})();

(function() {
    console.log('API 12 (erro no callback propaga):');
    var env = makeEnv();
    var err = null;
    try {
        var src = 'setTimeout(function () { throw new Error("bug no timer"); }, 10);' +
                  'System.delay(20); System.delay(20);';
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness',
                              'Storage', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness,
           env.Storage, env.setTimeout, env.setInterval, env.clearTimeout, env.clearInterval);
    } catch (e) {
        err = e && (e.stack || String(e)) || String(e);  // QUALQUER throw vira erro do app
    }
    check('erro do callback mata o app com a mensagem', err !== null && String(err).indexOf('bug no timer') >= 0, err);
})();

(function() {
    console.log('API 12 (Storage):');
    var src = [
        'Storage.set("nome", "Spot");',
        'Storage.set("peso", 12);',           // numero serializa
        'System.drawString(Storage.get("nome") + "|" + Storage.get("peso") + "|" + Storage.get("falta", "padrao"), 1, 1);',
        'Storage.remove("nome");',
        'System.drawString(String(Storage.get("nome", "vazio")), 1, 20);',
        'Storage.set("x", "1"); Storage.clear();',
        'System.drawString(String(Storage.get("x", "limpo")), 1, 40);',
        'System.exitApp();'
    ].join('\n');
    var env = makeEnv();
    var err = null;
    try {
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness',
                              'Storage', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness,
           env.Storage, env.setTimeout, env.setInterval, env.clearTimeout, env.clearInterval);
    } catch (e) {
        if (e !== 'OS_EXIT' && !(e && e.harnessStop)) err = e && (e.stack || String(e)) || String(e);
    }
    check('roda sem erro', err === null, err || '');
    var j = joinLog(env.__harness.log);
    check('set/get/serializa/default', j.indexOf('Spot|12|padrao') >= 0, j);
    check('remove apaga', j.indexOf('vazio') >= 0);
    check('clear limpa tudo', j.indexOf('limpo') >= 0);
})();

(function() {
    console.log('API 12 (FS binario com NUL):');
    var src = [
        'var dados = String.fromCharCode(1, 0, 2, 0, 255);',  // bytes com NUL no meio
        'FS.writeFile("/local/bin.dat", dados);',
        'var volta = FS.readFile("/local/bin.dat");',
        'System.drawString(volta.length + ":" + volta.charCodeAt(0) + "," + volta.charCodeAt(2) + "," + volta.charCodeAt(4), 1, 1);',
        'System.exitApp();'
    ].join('\n');
    var env = makeEnv();
    var err = null;
    try {
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness',
                              'Storage', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness,
           env.Storage, env.setTimeout, env.setInterval, env.clearTimeout, env.clearInterval);
    } catch (e) {
        if (e !== 'OS_EXIT' && !(e && e.harnessStop)) err = e && (e.stack || String(e)) || String(e);
    }
    check('roda sem erro', err === null, err || '');
    var j = joinLog(env.__harness.log);
    check('bytes atravessam sem truncar no NUL', j.indexOf('5:1,2,255') >= 0, j);
})();

(function() {
    console.log('Settings (tempo de tela):');
    var r = runApp('data/apps/Settings/main.js', function(env) {
        // abre "Tela" (linha 4) e toca na linha do timeout duas vezes
        env.__harness.tap(120, 247);
        var ty = env.System.getAutoBrightness() !== null ? 250 : 212;
        env.__harness.tap(120, ty + 15);
        env.__harness.tap(120, ty + 15);
    });
    check('roda sem erro', r.err === null, r.err || '');
    var j = joinLog(r.log);
    check('linha do timeout desenha', j.indexOf('Tela apaga:') >= 0);
    check('dois toques avancam 2 opcoes (sempre -> 1 min)', r.env.__scrTmo === 60000, 'tmo=' + r.env.__scrTmo);
})();

(function() {
    console.log('API 12 (alarme):');
    var src = [
        'System.setAlarm(7, 30, "cafe");',
        'var a = System.getAlarm();',
        'System.drawString(a.hour + "/" + a.minute + "/" + a.msg + "/" + System.setAlarm(25, 0, "x"), 1, 1);',
        'System.clearAlarm();',
        'System.drawString(String(System.getAlarm() === null), 1, 20);',
        'System.exitApp();'
    ].join('\n');
    var env = makeEnv();
    var err = null;
    try {
        var fn = new Function('System', 'FS', 'Net', 'CelerLink', '__harness',
                              'Storage', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', src);
        fn(env.System, env.FS, env.Net, env.CelerLink, env.__harness,
           env.Storage, env.setTimeout, env.setInterval, env.clearTimeout, env.clearInterval);
    } catch (e) {
        if (e !== 'OS_EXIT' && !(e && e.harnessStop)) err = e && (e.stack || String(e)) || String(e);
    }
    check('roda sem erro', err === null, err || '');
    var j = joinLog(env.__harness.log);
    check('set/get alarm', j.indexOf('7/30/cafe') >= 0, j);
    check('hora invalida rejeitada', j.indexOf('/false') >= 0, j);
    check('clear desarma', j.indexOf('true') >= 0);
})();

// resumo
console.log('');
if (failures) {
    console.log('FALHAS: ' + failures);
    process.exit(1);
}
console.log('OK: todos os testes passaram');
