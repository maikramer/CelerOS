// CelerPark Saida - totem de SAIDA do estacionamento (devkit-s3, API 49).
//
// O leitor de QR e um teclado USB (HID boot): a dep celeros.usb.hid abre a
// interface 3/1/1, decodifica os reports e entrega eventos de tecla. A
// leitura termina no Enter (leitor configurado com sufixo CR/LF); o codigo
// CP1-ULID e procurado na nuvem (edge function parking-gate):
//   pago    -> mark_exited + pulso de abertura no rele
//   em aberto / ja usado / anulado / desconhecido -> cancela fechada + log
//   sem rede -> cancela fechada (o totem de saida nao conhece os tickets
//               criados pelo totem de ENTRADA; comportamento do agente Rust
//               quando a fila local nao tinha o codigo)
//
// Sem tela: a "interface" e o logcat. Config: /local/celerpark.json.
// IMPORTANTE: se o servico de fabrica "USB HID" estiver ativo ele briga pela
// claim do leitor - deixe desligado (celerctl shell "svc stop celeros.usbhid").
//
// Setup da bancada: igual ao totem de entrada (wifi, celerpark.json, grant
// "CelerPark Saida", autostart.txt com o packageName).

var park = require("celerpark");
var usb = require("celeros.usb");
var hid = require("celeros.usb.hid");

park.LOG_TAG = "CelerPark Saida";

var CONFIG_PATH = "/local/celerpark.json";
var cfg = park.loadConfig(CONFIG_PATH);

var kbd = null;
var line = "";
var lastScanAt = 0;
var lastPoll = 0;
var lastBeat = 0;
var lastErrLogged = "";

// ---- LED de veredicto (WS2812 do devkit-s3, GPIO38) ------------------------
// verde = saida liberada, vermelho = ticket em aberto (nao pago), amarelo =
// invalido (desconhecido/ja usado/anulado/codigo ruim/sem rede). Cada cor
// fica 3 s e o loop apaga (o app nao bloqueia enquanto acende).
var LED_GREEN = 0x007000;
var LED_RED = 0x700000;
var LED_YELLOW = 0x605000;
var LED_MS = 3000;
var ledOffAt = 0;

function ledFlash(rgb) {
    if (typeof System.neopixel !== "function") return;
    if (!System.neopixel(0, [rgb])) return;  // sem fita no perfil (v1.0): nada
    ledOffAt = System.millis() + LED_MS;
}

function ledIdle() {
    if (ledOffAt && System.millis() >= ledOffAt) {
        ledOffAt = 0;
        System.neopixel(0, [0]);
    }
}

// ---- teclado HID -> texto (usage ID -> ASCII) ------------------------------
// Tabela US completa o bastante para QR (URLs, maiusculas, digitos, pontuacao).

var KMAP = {
    4: "a", 5: "b", 6: "c", 7: "d", 8: "e", 9: "f", 10: "g", 11: "h", 12: "i",
    13: "j", 14: "k", 15: "l", 16: "m", 17: "n", 18: "o", 19: "p", 20: "q",
    21: "r", 22: "s", 23: "t", 24: "u", 25: "v", 26: "w", 27: "x", 28: "y", 29: "z",
    30: "1", 31: "2", 32: "3", 33: "4", 34: "5", 35: "6", 36: "7", 37: "8",
    38: "9", 39: "0",
    43: "\t", 44: " ", 45: "-", 46: "=", 47: "[", 48: "]", 49: "\\", 50: "#",
    51: ";", 52: "'", 53: "`", 54: ",", 55: ".", 56: "/",
    89: "1", 90: "2", 91: "3", 92: "4", 93: "5", 94: "6", 95: "7", 96: "8",
    97: "9", 98: "0", 84: "/", 85: "*", 86: "-", 87: "+", 88: "\n"
};

var KMAP_S = {
    4: "A", 5: "B", 6: "C", 7: "D", 8: "E", 9: "F", 10: "G", 11: "H", 12: "I",
    13: "J", 14: "K", 15: "L", 16: "M", 17: "N", 18: "O", 19: "P", 20: "Q",
    21: "R", 22: "S", 23: "T", 24: "U", 25: "V", 26: "W", 27: "X", 28: "Y", 29: "Z",
    30: "!", 31: "@", 32: "#", 33: "$", 34: "%", 35: "^", 36: "&", 37: "*",
    38: "(", 39: ")",
    45: "_", 46: "+", 47: "{", 48: "}", 49: "|", 50: "~",
    51: ":", 52: "\"", 53: "~", 54: "<", 55: ">", 56: "?"
};

function charOf(key, mods) {
    var shifted = (mods & 0x22) !== 0;  // shift esq (0x02) | dir (0x20)
    var c = shifted ? KMAP_S[key] : KMAP[key];
    return c === undefined ? "" : c;
}

// ---- leitor: hotplug + drenagem ----------------------------------------------

function readerAlive() {
    if (!kbd) return false;
    var devs = usb.devices();
    for (var i = 0; i < devs.length; i++) {
        if (devs[i].addr === kbd.h.addr) return true;
    }
    return false;
}

function scanReader(force) {
    var now = System.millis();
    if (!force && now - lastScanAt < 2000) return;
    lastScanAt = now;
    if (kbd && !readerAlive()) {
        kbd.close();
        kbd = null;
        park.log("leitor desconectado");
    }
    if (!kbd) {
        var opened = null;
        try {
            var dev = hid.findKeyboard();          // 1o cls 3/sub 1/proto 1
            opened = dev ? hid.openKeyboard(dev) : null;
        } catch (e) {
            opened = null;
        }
        if (opened) {
            kbd = opened;
            park.log("leitor conectado (iface " + kbd.iface.n + ")");
        } else if (force) {
            // so reclama em hotplug/boot: tem teclado, mas a claim falhou
            var any = false;
            var devs = usb.devices();
            for (var i = 0; i < devs.length; i++) {
                if (usb.ifaceOf(devs[i], 3, 1, 1)) any = true;
            }
            if (any) {
                park.log("teclado presente mas claim falhou (servico USB HID ativo? svc stop celeros.usbhid)");
            }
        }
    }
}

function pollReader() {
    if (!kbd) return;
    var evs = kbd.poll();
    for (var i = 0; i < evs.length; i++) {
        var ev = evs[i];
        if (!ev.down) continue;
        if (ev.key === 0x28) {          // Enter: fim da leitura do QR
            if (line) {
                var raw = line;
                line = "";
                scan(raw);
            }
            continue;
        }
        if (ev.key === 0x2A) {          // Backspace
            if (line) line = line.substring(0, line.length - 1);
            continue;
        }
        var ch = charOf(ev.key, ev.mods);
        if (ch && ch !== "\n" && ch !== "\t") {
            line += ch;
            if (line.length > 256) {
                line = "";
                park.log("leitura longa demais: descartada");
            }
        }
    }
}

// ---- validacao do ticket (Session::scan do Rust) ------------------------------

function refuse(code, reason, unpaid) {
    park.log("saida recusada (" + code + "): " + reason);
    ledFlash(unpaid ? LED_RED : LED_YELLOW);  // vermelho = em aberto; amarelo = invalido
}

function scan(raw) {
    var code = park.extractCode(raw);
    if (!code) {
        park.log("codigo invalido: " + raw);
        return;
    }
    var r = park.lookupTicket(cfg, code);
    if (r.error === "offline") {
        refuse(code, "sem rede, cancela permanece fechada");
        return;
    }
    if (r.error === "notfound") {
        refuse(code, "ticket desconhecido");
        return;
    }
    if (r.error) {
        refuse(code, "nuvem " + r.error + " " + (r.detail || ""));
        return;
    }
    if (r.status === "issued") {
        refuse(code, "em aberto, pagamento nao confirmado", true);
        return;
    }
    if (r.status === "exited") {
        refuse(code, "ticket ja usado na saida");
        return;
    }
    if (r.status === "void") {
        refuse(code, "ticket anulado");
        return;
    }
    if (r.status !== "paid") {
        refuse(code, "status desconhecido: " + r.status);
        return;
    }

    // pago: marca a saida e abre. Se a rede cair ENTRE o lookup e o mark, o
    // lookup ja comprovou o pagamento - abre mesmo assim (o Rust fazia o
    // mesmo com a fila local; aqui so registramos o aviso).
    var m = park.markExited(cfg, code);
    if (m.error === "rejected" && m.detail === "not_paid") {
        refuse(code, "em aberto, pagamento nao confirmado");
        return;
    }
    if (m.error === "rejected" && m.detail === "already_exited") {
        refuse(code, "ticket ja usado na saida");
        return;
    }
    if (m.error === "rejected" && m.detail === "void") {
        refuse(code, "ticket anulado");
        return;
    }
    if (m.error === "notfound") {
        refuse(code, "ticket desconhecido");
        return;
    }
    if (m.error) {
        park.log("aviso: saida nao marcada na nuvem (" + m.error + "); abrindo com o lookup pago");
    }
    park.gateOpen(cfg);
    ledFlash(LED_GREEN);
    park.log("saida " + code + " paga. Cancela aberta.");
}

// ---- portal + heartbeat --------------------------------------------------------

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
    var now = System.millis();
    if (now - lastBeat >= 60000) {
        lastBeat = now;
        park.heartbeat(cfg);
    }
}

// ---- laco principal ------------------------------------------------------------

park.log("totem de saida. device " + cfg.deviceId + " (" + cfg.lane + ")");
if (typeof System.neopixel === "function") System.neopixel(0, [0]);  // LED comeca apagado
if (!cfg.secret) park.log("aviso: device_secret vazio na config");
if (!cfg.key) park.log("aviso: publishable key vazia na config (o gateway exige apikey)");
if (typeof USB === "undefined") {
    park.log("este firmware/placa nao tem USB Host; nada a ler");
}
scanReader(true);

while (true) {
    ledIdle();
    var hotplug = false;
    if (typeof USB !== "undefined") {
        while (usb.poll() !== null) hotplug = true;
    }
    scanReader(hotplug);
    pollReader();

    var now = System.millis();
    if (now - lastPoll >= (cfg.pollMs || 5000)) {
        lastPoll = now;
        if (Net.isConnected()) pollCycle();
    }

    System.delay(20);
}
