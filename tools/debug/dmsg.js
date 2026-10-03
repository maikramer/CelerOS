// Protocolo do debugger do Duktape 2.7 (dmsg): codificacao/decodificacao de
// dvalues e o leitor de fluxo (linha de versao + mensagens ate EOM). Sem
// estado de sessao e sem I/O: usado pelo cliente (dbg.js) e pelo alvo falso
// dos testes (test/debug/run.js). Constantes conferidas contra o duktape.c
// (DUK_DBG_IB_* / DUK_DBG_CMD_* / DUK_DBG_ERR_*).
'use strict';

var IB = { EOM: 0x00, REQ: 0x01, REP: 0x02, ERR: 0x03, NFY: 0x04 };

var CMD = {
    STATUS: 0x01, THROW: 0x05, DETACHING: 0x06, APPNOTIFY: 0x07,
    BASICINFO: 0x10, TRIGGERSTATUS: 0x11, PAUSE: 0x12, RESUME: 0x13,
    STEPINTO: 0x14, STEPOVER: 0x15, STEPOUT: 0x16, LISTBREAK: 0x17,
    ADDBREAK: 0x18, DELBREAK: 0x19, GETVAR: 0x1a, PUTVAR: 0x1b,
    GETCALLSTACK: 0x1c, GETLOCALS: 0x1d, EVAL: 0x1e, DETACH: 0x1f,
    APPREQUEST: 0x22  // atendido pelo JsDebugger do firmware: "restart", "info"
};

var ERR = { UNKNOWN: 0, UNSUPPORTED: 1, TOOMANY: 2, NOTFOUND: 3, APPLICATION: 4 };

// ---- codificacao -------------------------------------------------------------
function int(n) {
    if (n >= 0 && n <= 0x3f) return Buffer.from([0x80 + n]);
    if (n >= 0 && n <= 0x3fff) return Buffer.from([0xc0 + (n >> 8), n & 0xff]);
    var b = Buffer.alloc(5);
    b[0] = 0x10;  // INT4 (negativos: niveis da pilha)
    b.writeInt32BE(n, 1);
    return b;
}

function str(s) {
    var bytes = Buffer.from(String(s), 'utf8');
    if (bytes.length <= 0x1f) return Buffer.concat([Buffer.from([0x60 + bytes.length]), bytes]);
    var head = Buffer.alloc(5);
    head[0] = 0x11;  // STR4
    head.writeUInt32BE(bytes.length, 1);
    return Buffer.concat([head, bytes]);
}

// valor JS primitivo -> dvalue (PutVar, respostas do alvo falso)
function value(v) {
    if (v === undefined) return Buffer.from([0x16]);
    if (v === null) return Buffer.from([0x17]);
    if (v === true) return Buffer.from([0x18]);
    if (v === false) return Buffer.from([0x19]);
    if (typeof v === 'string') return str(v);
    if (typeof v === 'number') {
        // inteiros pequenos viajam como int (como o Duktape faz nos campos
        // inteiros); o resto como double IEEE big-endian
        if (v === (v | 0) && v >= 0 && v <= 0x3fff && !(v === 0 && 1 / v < 0)) return int(v);
        var b = Buffer.alloc(9);
        b[0] = 0x1a;
        b.writeDoubleBE(v, 1);
        return b;
    }
    throw new Error('so numero, string, true/false/null/undefined');
}

function object(classNum) {
    // OBJECT <classnum u8> <ptrlen u8> <ptr>: ponteiro falso de 4 bytes
    return Buffer.from([0x1b, classNum, 4, 0, 0, 0, 1]);
}

// mensagem = [ib][cmd int][args...][EOM] (REP/ERR nao levam cmd: passe null)
function message(ib, cmd, args) {
    var parts = [Buffer.from([ib])];
    if (cmd !== null && cmd !== undefined) parts.push(int(cmd));
    return Buffer.concat(parts.concat(args || [], [Buffer.from([IB.EOM])]));
}

function request(cmd, args) { return message(IB.REQ, cmd, args); }

// ---- decodificacao -----------------------------------------------------------
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
    if (b === 0x1d) { this.bytes(2); this.bytes(this.u8()); return { t: 'lfunc' }; }  // flags u16 + ptr
    if (b === 0x1e) { this.bytes(this.u8()); return { t: 'heapptr' }; }
    if (b >= 0x60 && b <= 0x7f) return { t: 'str', v: this.bytes(b - 0x60).toString('utf8') };
    if (b >= 0x80 && b <= 0xbf) return { t: 'int', v: b - 0x80 };
    if (b >= 0xc0) return { t: 'int', v: ((b - 0xc0) << 8) + this.u8() };
    return { t: 'invalido', v: b };
};

// Fluxo do alvo: uma linha de versao (handshake unilateral) e depois
// mensagens de dvalues ate EOM. Um Detaching volta o fluxo para "esperando
// versao": o proximo app attacha no MESMO socket com outra linha.
// feed() devolve eventos [{hello: '...'} | {msg: [dvalues]} | {error}].
function Stream() {
    this.buf = Buffer.alloc(0);
    this.handshaked = false;
}
Stream.prototype.feed = function (chunk) {
    var events = [];
    this.buf = Buffer.concat([this.buf, chunk]);
    while (this.buf.length > 0) {
        if (!this.handshaked) {
            var nl = this.buf.indexOf(0x0a);
            if (nl < 0) break;
            var line = this.buf.slice(0, nl).toString('utf8').trim();
            this.buf = this.buf.slice(nl + 1);
            if (!/^2 /.test(line)) {
                events.push({ error: 'handshake inesperado ' + JSON.stringify(line) + ' (protocolo 2 esperado)' });
                break;
            }
            this.handshaked = true;
            events.push({ hello: line });
            continue;
        }
        var r = new Reader(this.buf), msg = [];
        try {
            for (;;) {
                var dv = r.dvalue();
                if (dv.t === 'eom') break;
                if (dv.t === 'invalido') throw new Error('dvalue invalido 0x' + dv.v.toString(16));
                msg.push(dv);
            }
        } catch (e) {
            if (e instanceof Incomplete) break;
            events.push({ error: e.message + ' — fluxo dessincronizado' });
            this.buf = Buffer.alloc(0);
            break;
        }
        this.buf = this.buf.slice(r.off);
        if (msg.length >= 2 && msg[0].t === 'nfy' && msg[1].v === CMD.DETACHING) this.handshaked = false;
        events.push({ msg: msg });
    }
    return events;
};

// Leitor de REQUESTS (lado do alvo falso dos testes): mesmas regras, sem
// linha de versao.
function RequestStream() { Stream.call(this); this.handshaked = true; }
RequestStream.prototype = Object.create(Stream.prototype);
RequestStream.prototype.feed = function (chunk) {
    var ev = Stream.prototype.feed.call(this, chunk);
    this.handshaked = true;
    return ev;
};

module.exports = {
    IB: IB, CMD: CMD, ERR: ERR,
    int: int, str: str, value: value, object: object,
    message: message, request: request,
    Reader: Reader, Incomplete: Incomplete, Stream: Stream, RequestStream: RequestStream
};
