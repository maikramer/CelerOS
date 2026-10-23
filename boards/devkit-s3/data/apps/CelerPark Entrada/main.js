// CelerPark Entrada - totem de ENTRADA do estacionamento (devkit-s3, API 49).
//
// Botao BOOT (curto) = entrada: gera o ticket CP1-ULID, registra na nuvem
// (fila local se estiver offline), imprime na Epson TM-T20X (ESC/POS pela
// dep celeros.usb.escpos sobre o USB Host) e abre a cancela no rele.
// Segurar o BOOT sai do app (o OS cuida; o autostart relanca).
//
// Sem tela: a "interface" do totem e o logcat (celerctl logcat), o papel e
// o relé. Config: /local/celerpark.json (modelo celerpark.example.json).
//
// Setup da bancada (uma vez por totem):
//   celerctl shell "wifi <ssid> <senha>"        (NTP precisa de rede)
//   celerctl push celerpark.json /local/celerpark.json
//   celerctl shell "grant CelerPark Entrada"    (consentimento headless)
//   celerctl push autostart.txt /local/autostart.txt   (conteudo: o packageName)

var park = require("celerpark");
var usb = require("celeros.usb");
var esc = require("celeros.usb.escpos");

park.LOG_TAG = "CelerPark Entrada";

var CONFIG_PATH = "/local/celerpark.json";
var cfg = park.loadConfig(CONFIG_PATH);

var EPSON_VID = 0x04B8;
var lastPrintOk = false;
var lastErrLogged = "";     // evita spam do mesmo erro a cada poll
var lastPoll = 0;
var lastBeat = 0;
var pressArmed = true;

function p2(n) { return (n < 10 ? "0" : "") + n; }

function bootReport() {
    park.log("totem de entrada. device " + cfg.deviceId + " (" + cfg.lane + ")");
    if (System.epoch() < 1767225600) {
        park.log("aviso: relogio nao sincronizado (sem NTP?) - carimbo pode estar errado");
    }
    if (!cfg.secret) park.log("aviso: device_secret vazio na config");
    if (!cfg.key) park.log("aviso: publishable key vazia na config (o gateway exige apikey)");
    if (typeof USB === "undefined") {
        park.log("este firmware/placa nao tem USB Host; so nuvem e cancela");
    }
}

// Cupom identico ao do agente Rust (hw/printer.rs build_ticket): tudo
// centralizado (o ESC a 1 do cabecalho seguia valendo), WPC1252, QR nativo
// modelo 2 modulo 6 correcao M, 4 linhas de avanco e corte total.
function printTicket(code) {
    var dev = usb.find({ vid: EPSON_VID }) || usb.find({ cls: 7 });
    if (!dev) {
        park.log("nenhuma impressora (vid " + EPSON_VID.toString(16) + " ou classe 7) conectada");
        return false;
    }
    var p = esc.open(dev);
    if (!p) {
        park.log("impressora em uso por outro app/servico");
        return false;
    }
    var ok = true;
    try {
        var st = p.status();
        if (st) {
            if (!st.online) { park.log("impressora off-line"); ok = false; }
            else if (st.papel) { park.log("impressora sem papel"); ok = false; }
        }
        if (ok) {
            p.init().codepage(16);
            p.align(1);
            p.bold(true).size(2, 2).line(cfg.header);
            p.size(1, 1).bold(false);
            p.line("ENTRADA");
            p.line("------------------------------");
            p.line("Data: " + System.getDate());
            p.line("Hora: " + System.getTime() + ":" + p2(System.getSeconds()));
            p.line("Faixa: " + cfg.lane);
            p.line("------------------------------");
            p.qr(park.qrPayload(cfg.qrTemplate, code), 6, 1);
            p.line(code);
            p.line("Apresente este ticket na saida");
            p.line("Pague antes de sair");
            p.feed(4).cut();
        }
    } catch (e) {
        ok = false;
        park.log("erro de impressao: " + e);
    }
    p.close();
    lastPrintOk = ok;
    return ok;
}

// Session::entry do Rust: cria na nuvem (offline -> fila), imprime e abre.
// Erro de nuvem que nao seja offline aborta ANTES de imprimir (a cancela
// nao abre); impressora que falha tambem deixa a cancela fechada.
function entry() {
    var code = park.newCode();
    var enteredAt = park.rfc3339(System.epoch());
    var res = park.createTicket(cfg, code, enteredAt);
    var queued = false;
    if (res.error === "offline") {
        if (!park.outboxEnqueue(cfg.outboxPath, code, enteredAt)) {
            park.log("entrada abortada: sem rede e sem fila local (" + res.detail + ")");
            return;
        }
        queued = true;
    } else if (res.error) {
        park.log("entrada abortada: nuvem " + res.error + " " + (res.detail || ""));
        return;
    }
    if (!printTicket(code)) {
        park.log("entrada " + code + " sem cupom: cancela fechada");
        return;
    }
    park.gateOpen(cfg);
    park.log("ticket " + code + (queued
        ? " impresso. Nuvem indisponivel: codigo na fila local. Cancela aberta."
        : " impresso e registrado. Cancela aberta."));
}

// Session::poll do Rust, adaptado ao totem: pull_commands + ack, flush da
// fila e heartbeat (este a cada 60 s - cada Net.request abre TLS novo, um
// heartbeat por segundo travaria o botao).
function logOnce(kind, msg) {
    if (lastErrLogged !== kind) {
        lastErrLogged = kind;
        park.log(msg);
    }
}

function pollCycle() {
    var r = park.pullCommands(cfg);
    if (r.error) {
        if (r.error !== "offline") logOnce("cmd:" + r.error, "portal: " + r.error + " " + (r.detail || ""));
    } else {
        lastErrLogged = "";
        for (var i = 0; i < r.commands.length; i++) {
            var c = r.commands[i];
            if (c.command === "open") park.gateOpen(cfg);
            else if (c.command === "close") park.gateClose(cfg);
            park.ackCommand(cfg, c.id);
            park.log("portal: comando " + c.command + " aplicado");
        }
    }
    flushOutbox();

    var now = System.millis();
    if (now - lastBeat >= 60000) {
        lastBeat = now;
        park.heartbeat(cfg);
    }
}

// Session::flush_outbox: reenvia na ordem; conflict = outra faixa ja criou,
// remove tambem; offline para no primeiro (a rede caiu de novo).
function flushOutbox() {
    var list = park.outboxLoad(cfg.outboxPath);
    if (!list.length) return;
    var sent = 0;
    for (var i = 0; i < list.length; i++) {
        var t = list[i];
        var r = park.createTicket(cfg, t.code, t.entered_at);
        if (r.error === "offline") break;
        if (!r.error || r.error === "conflict") {
            if (r.error === "conflict") {
                park.log("fila: " + t.code + " ja existe em outra faixa; removendo da fila local");
            }
            list.splice(i, 1);
            i--;
            sent++;
            continue;
        }
        logOnce("flush:" + r.error, "fila: " + r.error + " " + (r.detail || ""));
        break;
    }
    if (sent) {
        park.outboxSave(cfg.outboxPath, list);
        park.log("fila: " + sent + " ticket(s) enviado(s) a nuvem");
    }
}

// ---- laco principal ---------------------------------------------------------

bootReport();

while (true) {
    // botao BOOT: borda de descida->subida dispara UMA entrada
    if (typeof System.button === "function") {
        var b = System.button();
        if (b === 1 && pressArmed) {
            pressArmed = false;
            entry();
        } else if (b === 0) {
            pressArmed = true;
        }
    }

    // hotplug da impressora (e de qualquer device)
    if (typeof USB !== "undefined") {
        while (usb.poll() !== null) { }
    }

    var now = System.millis();
    if (now - lastPoll >= (cfg.pollMs || 5000)) {
        lastPoll = now;
        if (Net.isConnected()) pollCycle();
    }

    System.delay(30);
}
