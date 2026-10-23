// test.js - bateria do totem CelerPark Entrada no harness
// (node test/js_harness/run.js).
//
// O wire monta a bancada inteira: devkit-s3 headless com USB Host, impressora
// Epson sintetica (classe 7, bulk 0x01/0x82), nuvem fake que roteia a edge
// function por acao (com queda de rede), rele gravado no gpio e botao BOOT
// roteirizado por timers. O run.js confere logs, bytes ESC/POS, a fila local
// e os cabecalhos apikey/Bearer que chegaram ao gateway.

var cloud = null;

exports.cloud = function () { return cloud; };

exports.wire = function (env) {
    var h = env.__harness;
    var sys = env.System;

    // placa: devkit-s3 headless com USB Host
    var baseInfo = sys.getInfo;
    sys.getInfo = function () {
        var i = baseInfo();
        i.hasDisplay = false;
        i.hasUsbHost = true;
        i.board = 'devkit-s3';
        return i;
    };

    // config do teste: poll curto para a linha do tempo andar
    env.FS.writeTextFile('/local/celerpark.json', JSON.stringify({
        deviceId: 'lane-test',
        secret: 'segredo-do-teste',
        key: 'sb_publishable_teste',
        relayPin: 4,
        pulseMs: 40,
        pollMs: 200,
        header: 'CelerPark',
        lane: 'Cancela Teste',
        qrTemplate: 'https://pay.teste/{code}'
    }));

    // impressora sintetica: TM-T20X (04B8:0E27), iface 0 classe 7 bidirecional
    var printer = h.addUsb({
        vid: 0x04B8, pid: 0x0E27, speed: 1,
        ifs: [{ n: 0, alt: 0, cls: 7, subcls: 1, proto: 2,
                eps: [{ addr: 0x01, type: 2, mps: 64, ivl: 0 },
                      { addr: 0x82, type: 2, mps: 64, ivl: 0 }] }]
    });

    cloud = {
        netDown: false,
        tickets: {},
        created: [],
        cmds: [],
        acked: [],
        beats: 0,
        lastHeaders: null,
        lastUrl: '',
        printer: printer,
        relay: [],
        press: function () { pendingPresses++; }
    };
    var pendingPresses = 0;

    env.Net.isConnected = function () { return !cloud.netDown; };
    env.Net.request = function (opts) {
        cloud.lastHeaders = opts.headers;
        cloud.lastUrl = opts.url;
        if (cloud.netDown) return null;
        var body = JSON.parse(opts.body);
        var resp = { ok: true };
        if (body.action === 'create_ticket') {
            cloud.created.push(body.code);
            cloud.tickets[body.code] = 'issued';
        } else if (body.action === 'pull_commands') {
            resp.commands = cloud.cmds.splice(0, cloud.cmds.length);
        } else if (body.action === 'ack_command') {
            cloud.acked.push(body.command_id);
        } else if (body.action === 'heartbeat') {
            cloud.beats++;
        }
        return { status: 200, body: JSON.stringify(resp) };
    };

    // botao BOOT: press enfileirada pelo roteiro
    sys.button = function () {
        if (pendingPresses > 0) {
            pendingPresses--;
            return 1;
        }
        return 0;
    };

    // rele no gpio 4: registra os niveis escritos
    sys.gpio.pinMode = function () { };
    sys.gpio.digitalWrite = function (pin, level) {
        cloud.relay.push({ pin: pin, level: level });
    };

    cloud.usbTx = h.usbTx;

    // ---- linha do tempo do teste -------------------------------------------
    env.setTimeout(function () { cloud.press(); }, 100);          // 1: online
    env.setTimeout(function () { cloud.netDown = true; }, 300);
    env.setTimeout(function () { cloud.press(); }, 500);          // 2: offline -> fila
    env.setTimeout(function () {
        cloud.cmds.push({ id: 'cmd-open', command: 'open' });     // portal na janela offline
    }, 900);
    env.setTimeout(function () { cloud.netDown = false; }, 2000); // rede volta -> flush + cmd
    env.setTimeout(function () { h.removeUsb(printer.addr); }, 3500);
    env.setTimeout(function () { cloud.press(); }, 4000);         // 3: sem impressora

    return cloud;
};
