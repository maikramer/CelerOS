// test.js - bateria do totem CelerPark Saida no harness
// (node test/js_harness/run.js).
//
// O wire monta o leitor de QR como teclado USB sintetico (classe 3/1/1,
// interrupt IN 0x81) e "digita" cada codigo via reports de boot; a nuvem
// fake responde a edge function com os mesmos status do agente Rust. O
// run.js confere as recusas, a abertura no rele e o comando remoto.

var CODE_PAID = 'CP1-01ARZ3NDEKTSV4RRFFQ69G5FAV';
var CODE_OPEN = 'CP1-01ARZ3NDEKTSV4RRFFQ69G5FBW';
var CODE_EXIT = 'CP1-01ARZ3NDEKTSV4RRFFQ69G5FCX';
var CODE_VOID = 'CP1-01ARZ3NDEKTSV4RRFFQ69G5FDZ';
var CODE_NEW = 'CP1-01ARZ3NDEKTSV4RRFFQ69G5FE1';

var cloud = null;

exports.cloud = function () { return cloud; };
exports.codes = { paid: CODE_PAID, open: CODE_OPEN };

exports.wire = function (env) {
    var h = env.__harness;
    var sys = env.System;

    var baseInfo = sys.getInfo;
    sys.getInfo = function () {
        var i = baseInfo();
        i.hasDisplay = false;
        i.hasUsbHost = true;
        i.board = 'devkit-s3';
        return i;
    };

    env.FS.writeTextFile('/local/celerpark.json', JSON.stringify({
        deviceId: 'lane-test-saida',
        secret: 'segredo-do-teste',
        key: 'sb_publishable_teste',
        relayPin: 5,
        pulseMs: 40,
        pollMs: 200,
        lane: 'Saida Teste'
    }));

    // leitor de QR como teclado boot (3/1/1), interrupt IN 0x81
    var kbd = h.addUsb({
        vid: 0x1234, pid: 0x5678, speed: 1,
        ifs: [{ n: 0, alt: 0, cls: 3, subcls: 1, proto: 1,
                eps: [{ addr: 0x81, type: 3, mps: 8, ivl: 10 }] }]
    });

    cloud = {
        netDown: false,
        tickets: {},
        cmds: [],
        acked: [],
        marks: [],
        relay: []
    };
    cloud.tickets[CODE_PAID] = 'paid';
    cloud.tickets[CODE_OPEN] = 'paid';
    cloud.tickets[CODE_EXIT] = 'exited';
    cloud.tickets[CODE_VOID] = 'void';

    env.Net.isConnected = function () { return !cloud.netDown; };
    env.Net.request = function (opts) {
        if (cloud.netDown) return null;
        var body = JSON.parse(opts.body);
        var resp = { ok: true };
        if (body.action === 'lookup_ticket') {
            var st = cloud.tickets[body.code];
            if (!st) return { status: 200, body: '{"ok":false,"error":"not_found"}' };
            resp.status = st;
        } else if (body.action === 'mark_exited') {
            var cur = cloud.tickets[body.code];
            cloud.marks.push(body.code);
            if (cur === 'paid') {
                cloud.tickets[body.code] = 'exited';
            } else if (cur === 'issued') {
                return { status: 200, body: '{"ok":false,"error":"not_paid"}' };
            } else if (cur === 'exited') {
                return { status: 200, body: '{"ok":false,"error":"already_exited"}' };
            } else if (cur === 'void') {
                return { status: 200, body: '{"ok":false,"error":"void"}' };
            }
        } else if (body.action === 'pull_commands') {
            resp.commands = cloud.cmds.splice(0, cloud.cmds.length);
        } else if (body.action === 'ack_command') {
            cloud.acked.push(body.command_id);
        }
        return { status: 200, body: JSON.stringify(resp) };
    };

    sys.gpio.pinMode = function () { };
    sys.gpio.digitalWrite = function (pin, level) {
        cloud.relay.push({ pin: pin, level: level });
    };

    // digitar no "leitor": 1 report por press + release, Enter no fim
    function usageOf(ch) {
        var A = 'A'.charCodeAt(0), Z = 'Z'.charCodeAt(0);
        var c = ch.toUpperCase().charCodeAt(0);
        if (c >= A && c <= Z) return 0x04 + (c - A);
        if (ch >= '1' && ch <= '9') return 0x1D + parseInt(ch, 10);
        if (ch === '0') return 0x27;
        if (ch === '-') return 0x2D;
        if (ch === '/') return 0x38;
        if (ch === ':') return 0x33;
        if (ch === '.') return 0x37;
        throw new Error('sem usage para ' + ch);
    }

    function type(text) {
        for (var i = 0; i < text.length; i++) {
            var u = usageOf(text.charAt(i));
            h.usbPushIn(kbd.addr, 0x81, [0, 0, u, 0, 0, 0, 0, 0]);
            h.usbPushIn(kbd.addr, 0x81, [0, 0, 0, 0, 0, 0, 0, 0]);
        }
        h.usbPushIn(kbd.addr, 0x81, [0, 0, 0x28, 0, 0, 0, 0, 0]);  // Enter
        h.usbPushIn(kbd.addr, 0x81, [0, 0, 0, 0, 0, 0, 0, 0]);
    }

    // ---- linha do tempo do teste -------------------------------------------
    env.setTimeout(function () { type(CODE_PAID); }, 100);              // paga: abre
    env.setTimeout(function () { type(CODE_EXIT); }, 400);              // ja usada: recusa
    env.setTimeout(function () { type(CODE_VOID); }, 700);              // anulada: recusa
    env.setTimeout(function () { type(CODE_NEW); }, 1000);              // desconhecida: recusa
    env.setTimeout(function () {
        cloud.netDown = true;
        type(CODE_OPEN);                                                 // sem rede: recusa
    }, 1300);
    env.setTimeout(function () { cloud.netDown = false; }, 2500);
    env.setTimeout(function () {
        cloud.cmds.push({ id: 'cmd-open', command: 'open' });            // portal remoto
    }, 3000);
    env.setTimeout(function () { type('https://pay.teste/' + CODE_OPEN); }, 3600);  // URL + paga: abre

    return cloud;
};
