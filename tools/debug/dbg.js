#!/usr/bin/env node
// Cliente minimo do Duktape debugger para o CelerOS: conecta no proxy TCP do
// `celerctl debug` e fala o protocolo dmsg do Duktape 2.7 (breakpoints, step,
// Eval, GetVar/PutVar, call stack, locals) num REPL de uma linha. Zero
// dependencias (Node puro, como o resto do repo).
//
//   python3 tools/celerctl.py debug MeuApp   # terminal 1: proxy + roda o app
//   node tools/debug/dbg.js                  # terminal 2: REPL abaixo
//
// O app PAUSA na primeira linha ao attachar: ponha os breakpoints e `c`.
// Comandos (frame = o do topo da pilha, salvo `lc N`/`up`/`down`):
//   b <linha> [arquivo]  breakpoint (default: arquivo da pausa atual)
//   B                    lista breakpoints     d <idx>  remove
//   c | p                continue | pause      q        detach e sair
//   s | n | o            step into/over/out    l [n]    fonte ao redor da linha
//   e <expr JS>          Eval no frame         v <nome> GetVar no frame
//   set <nome> <json>    PutVar no frame       cs       call stack
//   lc                   locals do frame       up|down  troca o frame
//   i                    BasicInfo
//
// Opcoes: --file /local/apps/X/main.js (default dos breakpoints antes da 1a
// pausa), --src <pasta|arquivo> (fonte local para o `l`; sem ela o cliente
// procura o app em data/apps, hub_apps e boards/*/data/apps), --host, --port.
// Variaveis DBG_HOST/DBG_PORT/DBG_FILE valem como default.
'use strict';

var net = require('net');
var fs = require('fs');
var path = require('path');
var readline = require('readline');

// ---- opcoes -----------------------------------------------------------------
var OPT = { host: process.env.DBG_HOST || '127.0.0.1', port: process.env.DBG_PORT || '9092',
            file: process.env.DBG_FILE || '', src: '' };
(function parseArgs(argv) {
    for (var i = 0; i < argv.length; i++) {
        var m = /^--(host|port|file|src)(?:=(.*))?$/.exec(argv[i]);
        if (!m) {
            console.error('uso: dbg.js [--host H] [--port P] [--file /local/apps/X/main.js] [--src pasta]');
            process.exit(2);
        }
        OPT[m[1]] = m[2] !== undefined ? m[2] : argv[++i];
    }
})(process.argv.slice(2));
var REPO = path.resolve(__dirname, '..', '..');

// ---- constantes dmsg (duktape.c, DUK_DBG_CMD_* / DUK_DBG_IB_*) --------------
var CMD = {
    STATUS: 0x01, THROW: 0x05, DETACHING: 0x06, APPNOTIFY: 0x07,
    BASICINFO: 0x10, PAUSE: 0x12, RESUME: 0x13, STEPINTO: 0x14, STEPOVER: 0x15,
    STEPOUT: 0x16, LISTBREAK: 0x17, ADDBREAK: 0x18, DELBREAK: 0x19, GETVAR: 0x1a,
    PUTVAR: 0x1b, GETCALLSTACK: 0x1c, GETLOCALS: 0x1d, EVAL: 0x1e, DETACH: 0x1f
};
var IB = { EOM: 0x00, REQ: 0x01, REP: 0x02, ERR: 0x03, NFY: 0x04 };
var ERRS = ['desconhecido', 'nao suportado', 'demais', 'nao encontrado', 'aplicacao'];
var DETACH_REASONS = ['normal', 'erro de transporte/protocolo'];

// ---- codificacao de dvalues -------------------------------------------------
function dvInt(n) {
    if (n >= 0 && n <= 0x3f) return Buffer.from([0x80 + n]);
    if (n >= 0 && n <= 0x3fff) return Buffer.from([0xc0 + (n >> 8), n & 0xff]);
    var b = Buffer.alloc(5);
    b[0] = 0x10;  // INT4
    b.writeInt32BE(n, 1);
    return b;
}
function dvStr(s) {
    var bytes = Buffer.from(String(s), 'utf8');
    if (bytes.length <= 31) return Buffer.concat([Buffer.from([0x60 + bytes.length]), bytes]);
    var head = Buffer.alloc(5);
    head[0] = 0x11;  // STR4
    head.writeUInt32BE(bytes.length, 1);
    return Buffer.concat([head, bytes]);
}
// valor JS (do JSON digitado no `set`) -> dvalue
function dvValue(v) {
    if (v === undefined) return Buffer.from([0x16]);
    if (v === null) return Buffer.from([0x17]);
    if (v === true) return Buffer.from([0x18]);
    if (v === false) return Buffer.from([0x19]);
    if (typeof v === 'string') return dvStr(v);
    if (typeof v === 'number') {
        var b = Buffer.alloc(9);
        b[0] = 0x1a;
        b.writeDoubleBE(v, 1);
        return b;
    }
    throw new Error('so numero, string, true/false/null');
}
// mensagem = [REQ][cmd int][args...][EOM] — fluxo continuo de dvalues, sem
// framing de comprimento
function request(cmd, args) {
    return Buffer.concat([Buffer.from([IB.REQ]), dvInt(cmd)].concat(args || [], [Buffer.from([IB.EOM])]));
}

// ---- decodificacao ----------------------------------------------------------
function Incomplete() {}
function Reader(buf) { this.buf = buf; this.off = 0; }
Reader.prototype.need = function (n) {
    if (this.off + n > this.buf.length) throw new Incomplete();
};
Reader.prototype.bytes = function (n) {
    this.need(n);
    var s = this.buf.slice(this.off, this.off + n);
    this.off += n;
    return s;
};
Reader.prototype.u8 = function () { this.need(1); return this.buf[this.off++]; };
Reader.prototype.dvalue = function () {
    var b = this.u8();
    if (b <= 0x04) return { t: ['eom', 'req', 'rep', 'err', 'nfy'][b] };
    if (b === 0x10) return { t: 'int', v: this.bytes(4).readInt32BE(0) };
    if (b === 0x11) return { t: 'str', v: this.bytes(this.bytes(4).readUInt32BE(0)).toString('utf8') };
    if (b === 0x12) return { t: 'str', v: this.bytes(this.bytes(2).readUInt16BE(0)).toString('utf8') };
    if (b === 0x13) return { t: 'buf', v: this.bytes(this.bytes(4).readUInt32BE(0)).toString('hex') };
    if (b === 0x14) return { t: 'buf', v: this.bytes(this.bytes(2).readUInt16BE(0)).toString('hex') };
    if (b === 0x15) return { t: 'unused' };
    if (b === 0x16) return { t: 'undefined' };
    if (b === 0x17) return { t: 'null' };
    if (b === 0x18) return { t: 'bool', v: true };
    if (b === 0x19) return { t: 'bool', v: false };
    if (b === 0x1a) return { t: 'num', v: this.bytes(8).readDoubleBE(0) };
    if (b === 0x1b) {  // OBJECT <classnum u8> <ptrlen u8> <ptr>
        var cls = this.u8();
        this.bytes(this.u8());
        return { t: 'obj', v: cls };
    }
    if (b === 0x1c) { this.bytes(this.u8()); return { t: 'ptr' }; }
    if (b === 0x1d) { this.bytes(2); this.bytes(this.u8()); return { t: 'lfunc' }; }  // flags u16, ptr
    if (b === 0x1e) { this.bytes(this.u8()); return { t: 'heapptr' }; }
    if (b >= 0x60 && b <= 0x7f) return { t: 'str', v: this.bytes(b - 0x60).toString('utf8') };
    if (b >= 0x80 && b <= 0xbf) return { t: 'int', v: b - 0x80 };
    if (b >= 0xc0) return { t: 'int', v: ((b - 0xc0) << 8) + this.u8() };
    return { t: 'invalido', v: b };
};

// DUK_HOBJECT_CLASS_* (nomes uteis no REPL)
var CLASSES = ['none', 'Object', 'Array', 'Function', 'Arguments', 'Boolean', 'Date', 'Error',
               'JSON', 'Math', 'Number', 'RegExp', 'String', 'global', 'Symbol', 'ObjEnv',
               'DecEnv', 'Pointer', 'Thread', 'ArrayBuffer', 'DataView'];
function render(dv) {
    if (!dv) return '?';
    switch (dv.t) {
        case 'int': case 'num': case 'bool': return String(dv.v);
        case 'str': return JSON.stringify(dv.v);
        case 'obj': return '[object ' + (CLASSES[dv.v] || 'class ' + dv.v) + ']';
        case 'undefined': case 'null': return dv.t;
        case 'unused': return '<unused>';
        default: return '<' + dv.t + (dv.v !== undefined ? ' ' + dv.v : '') + '>';
    }
}
function plain(dv) { return dv && dv.v !== undefined ? dv.v : render(dv); }

// ---- fonte local (para mostrar a linha da pausa) -----------------------------
var srcCache = {};
function localSource(devFile) {
    if (!devFile) return null;
    if (srcCache[devFile] !== undefined) return srcCache[devFile];
    var base = path.basename(devFile);
    var m = /\/apps\/([^/]+)\//.exec(devFile);
    var cands = [];
    if (OPT.src) {
        cands.push(fs.existsSync(OPT.src) && fs.statSync(OPT.src).isDirectory()
                   ? path.join(OPT.src, base) : OPT.src);
    }
    if (m) {
        cands.push(path.join(REPO, 'data/apps', m[1], base), path.join(REPO, 'hub_apps', m[1], base));
        try {
            fs.readdirSync(path.join(REPO, 'boards')).forEach(function (b) {
                cands.push(path.join(REPO, 'boards', b, 'data/apps', m[1], base));
            });
        } catch (e) { /* sem boards/ */ }
    }
    var lines = null;
    for (var i = 0; i < cands.length && !lines; i++) {
        try { lines = fs.readFileSync(cands[i], 'utf8').split('\n'); } catch (e) { /* proximo */ }
    }
    srcCache[devFile] = lines;
    return lines;
}
function showSource(file, line, ctx) {
    var lines = localSource(file);
    if (!lines || !line) return false;
    var from = Math.max(1, line - ctx), to = Math.min(lines.length, line + ctx);
    for (var i = from; i <= to; i++) {
        console.log((i === line ? '=> ' : '   ') + ('    ' + i).slice(-4) + '  ' + lines[i - 1]);
    }
    return true;
}

// ---- estado da sessao --------------------------------------------------------
var state = { paused: false, file: OPT.file, func: '', line: 0, level: -1 };
var replyQ = [];   // um callback por request, na ordem (o target responde em ordem)
var handshaked = false;
var sessions = 0;
var firstPause = false;  // handshake feito, Status da pausa do attach a caminho
var inBuf = Buffer.alloc(0);

function out(s) {
    // imprime sem atropelar a linha que o usuario esta digitando
    if (process.stdout.isTTY) { readline.clearLine(process.stdout, 0); readline.cursorTo(process.stdout, 0); }
    console.log(s);
    prompt();
}

var sock = net.connect(parseInt(OPT.port, 10), OPT.host, function () {
    // handshake UNILATERAL: o target manda a linha de versao no attach e o
    // cliente nao escreve nada antes dela (byte extra vira initial byte
    // invalido no Duktape e a sessao cai com Detaching 1)
    console.log('dbg: conectado em ' + OPT.host + ':' + OPT.port + ' (aguardando o app attachar...)');
});
sock.on('error', function (e) {
    console.error('dbg: conexao falhou (' + e.message + ') — o celerctl debug esta no ar?');
    process.exit(1);
});
sock.on('data', function (chunk) {
    inBuf = Buffer.concat([inBuf, chunk]);
    // o loop alterna handshake e mensagens: um Detaching seguido da linha
    // de versao do proximo app pode chegar no mesmo pedaco
    while (inBuf.length > 0) {
        if (!handshaked) {
            var nl = inBuf.indexOf(0x0a);
            if (nl < 0) return;
            var hello = inBuf.slice(0, nl).toString('utf8').trim();
            inBuf = inBuf.slice(nl + 1);
            if (!/^2 /.test(hello)) {
                console.error('dbg: handshake inesperado (' + JSON.stringify(hello) + '): protocolo 2 esperado');
                process.exit(1);
            }
            handshaked = true;
            sessions++;
            out('dbg: target ' + hello.split(' ').slice(2).join(' ') + ' (protocolo 2)' +
                (sessions > 1 ? ' — sessao ' + sessions : ''));
            // o attach sempre pausa: a fila espera o Status dessa pausa (que
            // traz o arquivo do app para os breakpoints) antes do 1o comando
            firstPause = true;
            var sess = sessions;
            setTimeout(function () {
                if (sess === sessions && firstPause) { firstPause = false; pump(); }
            }, 3000);
            continue;
        }
        // mensagens completas terminam em EOM; uma incompleta espera mais bytes
        var r = new Reader(inBuf), msg = [];
        try {
            for (;;) {
                var dv = r.dvalue();
                if (dv.t === 'eom') break;
                if (dv.t === 'invalido') throw new Error('dvalue invalido 0x' + dv.v.toString(16));
                msg.push(dv);
            }
        } catch (e) {
            if (e instanceof Incomplete) return;
            out('dbg: ' + e.message + ' — fluxo dessincronizado, encerrando');
            sock.destroy();
            return;
        }
        inBuf = inBuf.slice(r.off);
        dispatch(msg);
    }
});
sock.on('close', function () {
    console.log('\ndbg: conexao encerrada');
    process.exit(0);
});

function dispatch(msg) {
    var head = msg.shift();
    if (!head) return;
    if (head.t === 'nfy') return notify(msg.length ? msg.shift().v : -1, msg);
    if (head.t === 'rep' || head.t === 'err') {
        var cb = replyQ.shift();
        if (head.t === 'err') {
            var code = msg.length ? msg[0].v : 0;
            return out('erro (' + (ERRS[code] || code) + '): ' + (msg.length > 1 ? plain(msg[1]) : ''));
        }
        if (cb) cb(msg);
        return;
    }
    // REQ do target (AppRequest reverso): nao suportado, responde erro
    sock.write(Buffer.from([IB.ERR, 0x81, IB.EOM]));
}

function notify(cmd, a) {
    if (cmd === CMD.STATUS) {
        // NFY STATUS <paused> <file> <func> <line> <pc>
        var paused = a.length && a[0].v === 1;
        var wasPaused = state.paused;
        state.paused = paused;
        if (paused) {
            if (a[1] && a[1].t === 'str') state.file = a[1].v;
            state.func = a[2] && a[2].t === 'str' ? a[2].v : '';
            state.line = a[3] ? a[3].v : 0;
            state.level = -1;
            if (firstPause) { firstPause = false; setImmediate(pump); }
            var where = state.file ? state.file + ':' + state.line : '(fora de funcao JS)';
            out('[pausado] ' + where + (state.func ? ' em ' + state.func + '()' : ''));
            showSource(state.file, state.line, 0);
        } else if (wasPaused) {
            out('[rodando]');
        }
    } else if (cmd === CMD.THROW) {
        // NFY THROW <fatal> <msg> <file> <line>. A saida do app (X da topbar,
        // System.exitApp, celerctl exit) e um throw interno do runtime
        if (a[1] && a[1].v === 'Error: app exit') return out('[app encerrado]');
        out('[throw' + (a[0] && a[0].v ? ' FATAL' : '') + '] ' + plain(a[1]) +
            (a[2] ? '  em ' + plain(a[2]) + ':' + plain(a[3]) : ''));
    } else if (cmd === CMD.DETACHING) {
        // o app saiu (ou a sessao caiu): o proxy segue no ar e o PROXIMO app
        // lancado attacha de novo com outra linha de versao no mesmo socket
        var reason = a.length ? a[0].v : 0;
        handshaked = false;
        state.paused = false;
        replyQ = [];
        if (scriptWait) { clearInterval(scriptWait); scriptWait = null; }
        out('[detach] ' + (DETACH_REASONS[reason] || 'motivo ' + reason) +
            (a.length > 1 ? ': ' + plain(a[1]) : '') + ' — aguardando o proximo app attachar');
    } else if (cmd === CMD.APPNOTIFY) {
        out('[app] ' + a.map(render).join(' '));
    } else {
        out('[notify 0x' + Number(cmd).toString(16) + '] ' + a.map(render).join(' '));
    }
}

function send(cmd, args, cb) {
    replyQ.push(cb || function () {});
    sock.write(request(cmd, args));
}

// ---- REPL -------------------------------------------------------------------
// Linhas passam por uma fila com cancela: nada roda antes do handshake e, com
// stdin scriptado (nao-TTY), cada comando espera a resposta do anterior.
var cmdQ = [];
var rlClosed = false;
var rl = readline.createInterface({ input: process.stdin, output: process.stdout });
rl.setPrompt('dbg> ');
function prompt() { if (!rlClosed && handshaked && process.stdin.isTTY) rl.prompt(true); }
rl.on('line', function (line) {
    cmdQ.push(line);
    pump();
});
var scriptWait = null;  // stdin scriptado: espera a resposta (e a pausa, p/ c/s/n/o)
function pump() {
    while (handshaked && !firstPause && cmdQ.length > 0 && !scriptWait) {
        var line = cmdQ.shift();
        if (!process.stdin.isTTY) console.log('dbg> ' + line);
        handleLine(line);
    }
}
// No modo scriptado, comandos que soltam a execucao seguram a fila ate a
// proxima pausa (breakpoint/step) — assim `b 7` / `c` / `v n` funciona em
// arquivo sem sleeps magicos.
function holdUntil(pred, ms) {
    if (process.stdin.isTTY) return;
    var t0 = Date.now();
    scriptWait = setInterval(function () {
        if (pred() || Date.now() - t0 > ms) {
            clearInterval(scriptWait);
            scriptWait = null;
            pump();
        }
    }, 20);
}
function holdReply() {
    var n = replyQ.length;
    holdUntil(function () { return replyQ.length < n; }, 10000);
}
function holdPause() {
    var n = replyQ.length;
    state.paused = false;
    holdUntil(function () { return replyQ.length < n && state.paused; }, 10000);
}

function needPaused() {
    if (state.paused) return true;
    out('o app esta rodando: `p` pausa (ou espere um breakpoint)');
    return false;
}

function handleLine(raw) {
    var line = raw.trim();
    if (!line) return prompt();
    var sp = line.indexOf(' ');
    var verb = sp < 0 ? line : line.slice(0, sp);
    var arg = sp < 0 ? '' : line.slice(sp + 1).trim();
    switch (verb) {
        case 'b': {
            var mb = /^(\d+)(?:\s+(\S+))?$/.exec(arg);
            if (!mb) return out('uso: b <linha> [arquivo]');
            var file = mb[2] || state.file;
            if (!file) return out('arquivo ainda desconhecido: espere a 1a pausa ou use `b <linha> /local/apps/X/main.js`');
            if (file.indexOf('/') < 0 && state.file) file = path.posix.join(path.posix.dirname(state.file), file);
            send(CMD.ADDBREAK, [dvStr(file), dvInt(parseInt(mb[1], 10))], function (m) {
                out('breakpoint #' + plain(m[0]) + ' em ' + file + ':' + mb[1]);
            });
            return holdReply();
        }
        case 'B':
            send(CMD.LISTBREAK, [], function (m) {
                if (!m.length) return out('(sem breakpoints)');
                var s = [];
                for (var i = 0; i + 1 < m.length; i += 2) s.push('#' + i / 2 + '  ' + plain(m[i]) + ':' + plain(m[i + 1]));
                out(s.join('\n'));
            });
            return holdReply();
        case 'd':
            if (!/^\d+$/.test(arg)) return out('uso: d <indice do B>');
            send(CMD.DELBREAK, [dvInt(parseInt(arg, 10))], function () { out('removido'); });
            return holdReply();
        case 'i':
            send(CMD.BASICINFO, [], function (m) {
                out('duktape ' + plain(m[0]) + ' ' + plain(m[1]) + ' | ' + plain(m[2]) +
                    ' | ponteiro ' + plain(m[4]) + ' B');
            });
            return holdReply();
        case 'c': send(CMD.RESUME, []); return holdPause();
        case 'p': send(CMD.PAUSE, []); return holdPause();
        case 's': if (!needPaused()) return; send(CMD.STEPINTO, []); return holdPause();
        case 'n': if (!needPaused()) return; send(CMD.STEPOVER, []); return holdPause();
        case 'o': if (!needPaused()) return; send(CMD.STEPOUT, []); return holdPause();
        case 'l': {
            var ctx = parseInt(arg || '5', 10);
            if (!state.file) return out('sem pausa ainda');
            if (!showSource(state.file, state.line, ctx)) {
                out('fonte local de ' + state.file + ' nao encontrado (use --src)');
            }
            return prompt();
        }
        case 'e':
            if (!arg) return out('uso: e <expressao JS>');
            // pausado: eval direto no frame (ve locais); rodando: eval global
            send(CMD.EVAL, [state.paused ? dvInt(state.level) : Buffer.from([0x17]), dvStr(arg)], function (m) {
                out((m[0] && m[0].v ? '! ' : '= ') + render(m[1]));
            });
            return holdReply();
        case 'v':
            if (!arg) return out('uso: v <nome>');
            if (!needPaused()) return;
            send(CMD.GETVAR, [dvInt(state.level), dvStr(arg)], function (m) {
                out(m[0] && m[0].v ? arg + ' = ' + render(m[1]) : arg + ': nao definida neste escopo');
            });
            return holdReply();
        case 'set': {
            var ms = /^(\S+)\s+(.+)$/.exec(arg);
            if (!ms) return out('uso: set <nome> <valor JSON>');
            if (!needPaused()) return;
            var val;
            try { val = dvValue(JSON.parse(ms[2])); } catch (e) { return out('valor invalido: ' + e.message); }
            send(CMD.PUTVAR, [dvInt(state.level), dvStr(ms[1]), val], function () { out(ms[1] + ' atualizado'); });
            return holdReply();
        }
        case 'cs':
            send(CMD.GETCALLSTACK, [], function (m) {
                var s = [];
                for (var i = 0; i + 3 < m.length; i += 4) {
                    s.push((-1 - i / 4 === state.level ? '* ' : '  ') + (-1 - i / 4) + '  ' +
                           (plain(m[i + 1]) || '<anon>') + '()  ' + plain(m[i]) + ':' + plain(m[i + 2]));
                }
                out(s.length ? s.join('\n') : '(pilha vazia)');
            });
            return holdReply();
        case 'lc':
            if (!needPaused()) return;
            send(CMD.GETLOCALS, [dvInt(state.level)], function (m) {
                var s = [];
                for (var i = 0; i + 1 < m.length; i += 2) s.push(plain(m[i]) + ' = ' + render(m[i + 1]));
                out(s.length ? s.join('\n') : '(sem locais: frame global usa `v`/`e`)');
            });
            return holdReply();
        case 'up': case 'down':
            if (!needPaused()) return;
            state.level += verb === 'up' ? -1 : 1;
            if (state.level > -1) state.level = -1;
            return out('frame ' + state.level + ' (`cs` mostra a pilha)');
        case 'q':
            send(CMD.DETACH, [], function () { sock.end(); });
            setTimeout(function () { process.exit(0); }, 1500);
            return;
        case 'h': case 'help': case '?':
            return out('b <linha> [arq] | B | d <idx> | c | p | s | n | o | l [n] | e <expr> | v <var> | ' +
                       'set <var> <json> | cs | lc | up | down | i | q');
        default:
            return out('comando desconhecido: ' + verb + ' (h = ajuda)');
    }
}

rl.on('close', function () {
    rlClosed = true;
    // stdin scriptado acabou: espera a fila esvaziar e sai com detach limpo
    // (sem isso o app ficaria pausado ate o proxy fechar)
    if (process.stdin.isTTY) {
        try { send(CMD.DETACH, []); } catch (e) { /* ja fechou */ }
        setTimeout(function () { sock.end(); process.exit(0); }, 300);
        return;
    }
    var t = setInterval(function () {
        if (cmdQ.length || scriptWait) return;
        clearInterval(t);
        if (!handshaked) {
            if (sessions > 0) process.exit(0);  // o app ja saiu (detach)
            return;  // ainda sem app: o close do socket encerra
        }
        send(CMD.DETACH, [], function () { sock.end(); });
        setTimeout(function () { process.exit(0); }, 1500);
    }, 50);
});
