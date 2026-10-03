#!/usr/bin/env node
// Testes do debugger de apps (tools/debug): o protocolo dmsg (dmsg.js) e o
// cliente (dbg.js) contra um ALVO FALSO que imita o Duktape 2.7 do device:
// handshake unilateral, pausa no attach, breakpoints, Eval de verdade (vm do
// Node, inclusive o wrapper que formata objetos), erro nao capturado,
// AppRequest restart/info e Detaching. Sem hardware — a bancada cobre o
// transporte real (celerctl debug + JsDebugger no firmware).
// Uso: node test/debug/run.js

'use strict';
var net = require('net');
var path = require('path');
var vm = require('vm');
var spawn = require('child_process').spawn;

var ROOT = path.resolve(__dirname, '..', '..');
var D = require(path.join(ROOT, 'tools/debug/dmsg.js'));
var failures = 0;

function check(name, ok, detail) {
    console.log((ok ? '  PASS  ' : '  FAIL  ') + name + (ok || !detail ? '' : ' — ' + detail));
    if (!ok) failures++;
}

// ------------------------------------------------------------------ dmsg --
(function () {
    console.log('dmsg (codificacao):');
    function rt(buf) { return new D.Reader(buf).dvalue(); }
    var ints = [0, 1, 63, 64, 255, 16383, 16384, 100000, -1, -5];
    var allOk = ints.every(function (n) { var d = rt(D.int(n)); return d.t === 'int' && d.v === n; });
    check('int ida-e-volta nas fronteiras (0/63/64/16383/16384/negativos)', allOk);
    check('int 63 usa 1 byte, 64 usa 2, 16384 usa INT4',
          D.int(63).length === 1 && D.int(64).length === 2 && D.int(16384).length === 5);
    var s31 = 'x'.repeat(31), s32 = 'y'.repeat(32);
    check('string curta (<=31) em 0x60+len', D.str(s31)[0] === 0x7f && rt(D.str(s31)).v === s31);
    check('string longa em STR4', D.str(s32)[0] === 0x11 && rt(D.str(s32)).v === s32);
    check('utf-8 conta bytes, nao caracteres', rt(D.str('ação')).v === 'ação');
    check('valor: double, bool, null, undefined',
          rt(D.value(1.5)).v === 1.5 && rt(D.value(true)).v === true &&
          rt(D.value(null)).t === 'null' && rt(D.value(undefined)).t === 'undefined');
    check('valor: inteiro pequeno vira int, -0 continua double',
          rt(D.value(7)).t === 'int' && rt(D.value(-0)).t === 'num');
    var req = D.request(D.CMD.GETVAR, [D.int(-1), D.str('n')]);
    check('request = REQ, cmd int, args, EOM',
          req[0] === D.IB.REQ && req[1] === 0x80 + D.CMD.GETVAR && req[req.length - 1] === D.IB.EOM);
    check('REP nao leva cmd', D.message(D.IB.REP, null, [D.int(1)]).equals(Buffer.from([0x02, 0x81, 0x00])));

    console.log('dmsg (fluxo):');
    var wire = Buffer.concat([
        Buffer.from('2 20700 v2.7.0 alvo\n'),
        D.message(D.IB.NFY, D.CMD.STATUS, [D.int(1), D.str('/local/apps/X/main.js'), D.str('f'), D.int(3), D.int(0)]),
        D.message(D.IB.REP, null, [D.int(0)]),
        D.message(D.IB.NFY, D.CMD.DETACHING, [D.int(0)]),
        Buffer.from('2 20700 v2.7.0 alvo\n'),
        D.message(D.IB.REP, null, [])
    ]);
    function kinds(evs) { return evs.map(function (e) { return e.hello ? 'H' : e.msg ? 'M' : 'E'; }).join(''); }
    var whole = kinds(new D.Stream().feed(wire));
    check('versao + mensagens + Detaching + versao do proximo app', whole === 'HMMMHM', whole);
    var st = new D.Stream(), evs = [];
    for (var i = 0; i < wire.length; i++) evs = evs.concat(st.feed(wire.slice(i, i + 1)));
    check('mesmo fluxo entregue byte a byte', kinds(evs) === whole, kinds(evs));
    var bad = new D.Stream();
    bad.feed(Buffer.from('2 x\n'));
    var e = bad.feed(Buffer.from([0x02, 0x0a, 0x00]));
    check('byte invalido vira erro (nao loop/garbage silencioso)', e.length === 1 && !!e[0].error);
    check('handshake que nao e protocolo 2 vira erro', !!new D.Stream().feed(Buffer.from('1 10000 x\n'))[0].error);
})();

// ------------------------------------------------------------ alvo falso --
// Programa do app falso: cada RESUME anda para a proxima "parada"; a parada
// so vira pausa se tiver breakpoint na linha (ou erro). Variaveis por parada
// alimentam GetVar/GetLocals/Eval (vm do Node).
var FILE = '/local/apps/Fake/main.js';
function makeStops() {
    var stops = [];
    for (var n = 1; n <= 5; n++) {
        stops.push({ line: 14, func: 'passo', vars: { n: n, ponto: { x: n * 10, y: n * 5 } } });
    }
    stops.push({ line: 17, func: 'passo', vars: { n: 6, quebrado: null },
                 thr: 'TypeError: cannot read property \'campo\' of null' });
    return stops;
}

function startTarget(onReady) {
    var log = [];
    var server = net.createServer(function (sock) {
        var st = { session: 0 };
        function send(b) { sock.write(b); }
        function status(paused, cur) {
            send(D.message(D.IB.NFY, D.CMD.STATUS, [D.int(paused ? 1 : 0), D.str(FILE),
                 D.str(cur.func), D.int(cur.line), D.int(0)]));
        }
        function attach() {
            st.session++;
            st.stops = makeStops();
            st.bps = [];
            st.cur = { line: 1, func: 'global', vars: {} };
            st.restart = false;
            st.exiting = false;
            send(Buffer.from('2 20700 v2.7.0 fake-alvo\n'));
            status(true, st.cur);
        }
        function rep(vals) { send(D.message(D.IB.REP, null, vals || [])); }
        function val(v) { return (v !== null && typeof v === 'object') ? D.object(1) : D.value(v); }
        function advance(step) {
            setTimeout(function () {
                if (st.exiting) {  // retomou a pausa do "app exit": app sai
                    send(D.message(D.IB.NFY, D.CMD.DETACHING, [D.int(0)]));
                    if (st.restart) setTimeout(attach, 30);
                    return;
                }
                if (st.restart) {  // proximo yield: o runtime joga "app exit"
                    st.exiting = true;
                    send(D.message(D.IB.NFY, D.CMD.THROW, [D.int(1), D.str('Error: app exit'), D.str('JsInternal.h'), D.int(39)]));
                    status(true, st.cur);
                    return;
                }
                while (st.stops.length) {
                    var s = st.stops.shift();
                    var hit = st.bps.some(function (b) { return b.file === FILE && b.line === s.line; });
                    if (hit || step || s.thr) {
                        st.cur = s;
                        if (s.thr) send(D.message(D.IB.NFY, D.CMD.THROW, [D.int(1), D.str(s.thr), D.str(FILE), D.int(s.line)]));
                        status(true, s);
                        return;
                    }
                }
                send(D.message(D.IB.NFY, D.CMD.DETACHING, [D.int(0)]));
            }, 5);
        }
        sock.on('error', function () { /* cliente saiu com o socket aberto (q/exit) */ });
        var rs = new D.RequestStream();
        sock.on('data', function (chunk) {
            rs.feed(chunk).forEach(function (ev) {
                var m = ev.msg;
                if (!m || m[0].t !== 'req') return log.push('lixo');
                var cmd = m[1].v, a = m.slice(2);
                log.push(cmd);
                var vars = st.cur.vars;
                switch (cmd) {
                    case D.CMD.BASICINFO: return rep([D.int(20700), D.str('v2.7.0'), D.str('fake'), D.int(1), D.int(4)]);
                    case D.CMD.ADDBREAK:
                        st.bps.push({ file: a[0].v, line: a[1].v });
                        return rep([D.int(st.bps.length - 1)]);
                    case D.CMD.DELBREAK:
                        if (a[0].v >= st.bps.length) return send(D.message(D.IB.ERR, null, [D.int(3), D.str('invalid breakpoint index')]));
                        st.bps.splice(a[0].v, 1);
                        return rep();
                    case D.CMD.LISTBREAK:
                        return rep([].concat.apply([], st.bps.map(function (b) { return [D.str(b.file), D.int(b.line)]; })));
                    case D.CMD.RESUME: rep(); status(false, st.cur); return advance(false);
                    case D.CMD.STEPOVER: case D.CMD.STEPINTO: case D.CMD.STEPOUT:
                        rep(); return advance(true);
                    case D.CMD.PAUSE: rep(); return status(true, st.cur);
                    case D.CMD.GETVAR:
                        if (!(a[1].v in vars)) return rep([D.int(0), Buffer.from([0x15])]);
                        return rep([D.int(1), val(vars[a[1].v])]);
                    case D.CMD.PUTVAR: vars[a[1].v] = a[2].v; return rep();
                    case D.CMD.GETLOCALS:
                        return rep([].concat.apply([], Object.keys(vars).map(function (k) { return [D.str(k), val(vars[k])]; })));
                    case D.CMD.GETCALLSTACK:
                        return rep([D.str(FILE), D.str(st.cur.func), D.int(st.cur.line), D.int(0),
                                    D.str(FILE), D.str('global'), D.int(30), D.int(0)]);
                    case D.CMD.EVAL: {
                        var ctx = vm.createContext(JSON.parse(JSON.stringify(vars)));
                        try { return rep([D.int(0), val(vm.runInContext(a[1].v, ctx))]); } catch (e) {
                            return rep([D.int(1), D.str(e.name + ': ' + e.message)]);
                        }
                    }
                    case D.CMD.APPREQUEST:
                        if (a[0].v === 'info') return rep([D.str(FILE), D.int(1000), D.int(500)]);
                        if (a[0].v === 'restart') { st.restart = true; return rep([D.str('/local/apps/Fake')]); }
                        return send(D.message(D.IB.ERR, null, [D.int(4), D.str('pedido desconhecido')]));
                    case D.CMD.DETACH:
                        rep();
                        return send(D.message(D.IB.NFY, D.CMD.DETACHING, [D.int(0)]));
                    default:
                        return send(D.message(D.IB.ERR, null, [D.int(1), D.str('unsupported')]));
                }
            });
        });
        attach();
    });
    server.listen(0, '127.0.0.1', function () { onReady(server, server.address().port, log); });
}

// canal lateral falso do proxy: manda uma linha de log e atende "sync"
function startSide(syncReply, onReady) {
    var reqs = [];
    var server = net.createServer(function (s) {
        s.on('error', function () {});
        s.write('I (1) app: linha de log do device\n');
        var acc = '';
        s.on('data', function (d) {
            acc += d;
            var parts = acc.split('\n');
            acc = parts.pop();
            parts.forEach(function (l) { reqs.push(l); s.write('\x01sync ' + syncReply + '\n'); });
        });
    });
    server.listen(0, '127.0.0.1', function () { onReady(server, server.address().port, reqs); });
}

// roda o dbg.js com um roteiro no stdin contra um alvo novo; com syncReply,
// tambem um canal lateral falso (done recebe os pedidos de sync)
function session(script, done, syncReply) {
    if (!syncReply) return runClient(script, 1, function (o, code, log) { done(o, code, log, []); });
    startSide(syncReply, function (side, sidePort, reqs) {
        runClient(script, sidePort, function (o, code, log) { side.close(); done(o, code, log, reqs); });
    });
}

function runClient(script, logPort, done) {
    startTarget(function (server, port, log) {
        var child = spawn(process.execPath, [path.join(ROOT, 'tools/debug/dbg.js'), '--port', String(port),
                                             '--log-port', String(logPort), '--src', path.join(__dirname, 'Fake')],
                          { env: Object.assign({}, process.env, { NO_COLOR: '1' }) });
        var outp = '';
        child.stdout.on('data', function (d) { outp += d; });
        child.stderr.on('data', function (d) { outp += d; });
        var killer = setTimeout(function () { child.kill(); }, 15000);
        child.on('exit', function (code) {
            clearTimeout(killer);
            server.close();
            done(outp, code, log);
        });
        child.stdin.end(script.join('\n') + '\n');
    });
}

function has(outp, re) { return re.test(outp); }

var scenarios = [
    function (next) {
        console.log('dbg.js (sessao basica):');
        session(['b 14', 'B', 'c', 'v n', 'v ponto', 'lc', 'e n * 2 + 1', 'e ponto', 'set n 42', 'v n',
                 'cs', 'i', 'zzz', 'q'], function (o, code) {
            check('pausa inicial na linha 1 com dica', has(o, /\[pausado\] \/local\/apps\/Fake\/main\.js:1 em global\(\)/) &&
                  has(o, /app pausado: `b <linha>`/));
            check('versao do Duktape e alvo no attach', has(o, /attachado — Duktape 2\.7\.0 \(fake-alvo\)/));
            check('breakpoint no arquivo da pausa', has(o, /breakpoint #0 \/local\/apps\/Fake\/main\.js:14/));
            check('c para no breakpoint com hit', has(o, /main\.js:14 em passo\(\)\s+\(breakpoint #0, hit 1\)/));
            check('fonte local da linha da pausa', has(o, /=>\s+14\s+total \+= soma/));
            check('v de primitivo', has(o, /n = 1\n/));
            check('v de objeto mostra o conteudo (JSON no alvo)', has(o, /ponto = \{"x":10,"y":5\}/));
            check('lc expande objetos', has(o, /dbg> lc\nn = 1\nponto = \{"x":10,"y":5\}/));
            check('e avalia no frame', has(o, /= 3\n/) && has(o, /= \{"x":10,"y":5\}/));
            check('set + v', has(o, /n = 42\n/));
            check('cs lista os frames', has(o, /\* -1\s+passo\(\)/) && has(o, /-2\s+global\(\)/));
            check('i junta BasicInfo e AppRequest info', has(o, /app \/local\/apps\/Fake\/main\.js \| heap livre 1 KB/));
            check('comando desconhecido nao derruba', has(o, /comando desconhecido: zzz/));
            check('q sai limpo', code === 0, 'exit ' + code);
            next();
        });
    },
    function (next) {
        console.log('dbg.js (condicao, temporario, watches):');
        session(['w n * 100', 'b 14 if n === 3', 'c', 'B', 'd 0', 'u 14', 'B', 'q'], function (o) {
            check('condicao falsa nao para (n=1,2 pulados)', has(o, /\[pausado\][^\n]*main\.js:14[^\n]*hit 1\)/) &&
                  !has(o, /w0 n \* 100 = 100\n/) && has(o, /w0 n \* 100 = 300/));
            check('B mostra condicao e hits', has(o, /#0\s+\/local\/apps\/Fake\/main\.js:14\s+if n === 3\s+hits 1/));
            check('u = breakpoint temporario: para e some', has(o, /w0 n \* 100 = 400/) && has(o, /dbg> B\n\(sem breakpoints\)/));
            check('watch indefinido antes de existir nao vira erro', has(o, /w0 n \* 100 = <indefinida>/));
            next();
        });
    },
    function (next) {
        console.log('dbg.js (erro nao capturado):');
        session(['c', 'lc', 'c'], function (o) {
            check('throw fatal avisado com local', has(o, /\[erro nao capturado\] TypeError: cannot read property 'campo' of null\s+em \/local\/apps\/Fake\/main\.js:17/));
            check('pausa no ponto do erro com dica', has(o, /main\.js:17 em passo\(\)\s+\(erro nao capturado: `c` deixa o erro seguir\)/));
            check('locais inspecionaveis no erro', has(o, /quebrado = null/));
            next();
        });
    },
    function (next) {
        console.log('dbg.js (r: relanca mantendo breakpoints):');
        session(['b 14', 'c', 'r', 'B', 'c', 'v n', 'q'], function (o, code, log) {
            check('app exit e retomado sozinho', has(o, /\[app encerrando\]/) && has(o, /\[detach\] app saiu/));
            check('sessao 2 attacha', has(o, /attachado — .*sessao 2/));
            check('breakpoint restaurado e dispara de novo', has(o, /1 breakpoint\(s\) restaurado\(s\)/) &&
                  (o.match(/\(breakpoint #0, hit 1\)/g) || []).length === 2);
            check('nenhum byte invalido no fio', log.indexOf('lixo') < 0);
            next();
        });
    },
    function (next) {
        console.log('dbg.js (canal lateral: logs + sync do r):');
        session(['b 14', 'c', 'r', 'v n', 'q'], function (o, code, log, reqs) {
            check('log do device aparece no REPL', has(o, /\| I \(1\) app: linha de log do device/));
            check('r pede sync da pasta do app no device', reqs[0] === 'sync /local/apps/Fake', JSON.stringify(reqs));
            check('arquivos enviados informados', has(o, /enviado: main\.js/));
            check('reinicia depois do sync', has(o, /sessao 2/));
            next();
        }, 'ok 1 main.js');
    },
    function (next) {
        session(['c', 'r', 'q'], function (o, code, log) {
            check('lint com erro: app NAO reinicia', has(o, /sync: lint com erros.*app NAO reiniciado/) &&
                  log.indexOf(D.CMD.APPREQUEST) < 0);
            next();
        }, 'erro lint com erros');
    }
];

(function run(i) {
    if (i < scenarios.length) return scenarios[i](function () { run(i + 1); });
    console.log(failures ? '\nFALHAS: ' + failures : '\nOK: todos os testes passaram');
    process.exit(failures ? 1 : 0);
})(0);
