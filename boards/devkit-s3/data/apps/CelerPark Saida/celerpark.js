// celerpark.js - nucleo compartilhado dos totems CelerPark no CelerOS
// (migracao do agente Rust GitClones/CelerPark).
//
// Aqui vive tudo que nao e hardware: a conversa com a nuvem (edge function
// parking-gate do Supabase, mesma API do cliente Rust), o codigo do ticket
// (CP1- + ULID Crockford base32), a fila local de entradas offline e o pulso
// do rele da cancela. Cada totem cuida do proprio periferico no main.js:
//   CelerPark Entrada - impressora ESC/POS (deps celeros.usb/celeros.usb.escpos)
//   CelerPark Saida   - leitor de QR como teclado USB (dep celeros.usb.hid)
//
// Configuracao: /local/celerpark.json (modelo em
// boards/devkit-s3/celerpark.example.json); sem arquivo valem os defaults.

var park = { version: "1.0.0" };

park.LOG_TAG = "CelerPark";

park.log = function (msg) {
    System.print(park.LOG_TAG + ": " + msg);
};

// ---- configuracao ----------------------------------------------------------

park.DEFAULTS = {
    url: "https://supabase.locatelli.app.br",
    key: "",               // publishable key (header apikey + Bearer)
    deviceId: "lane-01",
    secret: "",            // segredo do dispositivo (fica no aparelho)
    tenantId: "",
    header: "CelerPark",
    lane: "Cancela 1",
    qrTemplate: "{code}",  // "{code}" vira o codigo do ticket
    relayPin: -1,          // GPIO do rele da cancela; -1 = sem cancela
    relayPinClose: -1,     // linha opcional de fechamento (mesmo rele se -1)
    relayActiveLow: false,
    pulseMs: 500,
    pollMs: 5000,          // cadencia de pull_commands/heartbeat/flush
    outboxPath: "/local/celerpark_outbox.json"
};

park.loadConfig = function (path) {
    var cfg = {};
    var k;
    for (k in park.DEFAULTS) cfg[k] = park.DEFAULTS[k];
    var raw = null;
    try {
        if (FS.exists(path)) raw = FS.readTextFile(path);
    } catch (e) { raw = null; }
    if (raw) {
        var file = null;
        try {
            file = JSON.parse(raw);
        } catch (e) {
            park.log(path + " invalido (" + e + "); usando defaults");
        }
        if (file !== null && typeof file === "object") {
            for (k in cfg) {
                if (file[k] !== undefined && file[k] !== null) cfg[k] = file[k];
            }
        }
    } else {
        park.log("sem " + path + "; usando defaults (veja celerpark.example.json)");
    }
    return cfg;
};

// ---- codigo do ticket (ticket.rs) ------------------------------------------
// CP1- + ULID (26 chars base32 Crockford: 10 do timestamp ms UTC + 16 de
// entropia). O guard monotonic increments a parte aleatoria quando duas
// entradas caem no mesmo milissegundo (a especificacao do ULID manda).

park.PREFIX = "CP1-";
park.CROCK = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

var lastMs = -1;
var lastRnd = "";

function randChars(n) {
    var s = "";
    for (var i = 0; i < n; i++) s += park.CROCK.charAt(Math.floor(Math.random() * 32));
    return s;
}

function encodeTs(ms) {
    var s = "";
    var t = Math.floor(ms);
    for (var i = 0; i < 10; i++) {
        s = park.CROCK.charAt(t % 32) + s;
        t = Math.floor(t / 32);
    }
    return s;
}

function bumpBase32(s) {
    var a = s.split("");
    var i = a.length - 1;
    while (i >= 0) {
        var d = park.CROCK.indexOf(a[i]);
        if (d < 0) return null;
        if (d < 31) {
            a[i] = park.CROCK.charAt(d + 1);
            return a.join("");
        }
        a[i] = "0";
        i--;
    }
    return null;  // estourou os 16 chars (nao acontece num totem)
}

park.newCode = function () {
    var ms = System.epoch() * 1000;
    var rnd = randChars(16);
    if (ms === lastMs) {
        var bumped = bumpBase32(lastRnd);
        if (bumped !== null) rnd = bumped;
    }
    lastMs = ms;
    lastRnd = rnd;
    return park.PREFIX + encodeTs(ms) + rnd;
};

park.isUlidBody = function (body) {
    if (body.length !== 26) return false;
    for (var i = 0; i < 26; i++) {
        if (park.CROCK.indexOf(body.charAt(i)) < 0) return false;
    }
    return true;
};

// Aceita o codigo cru ou uma URL cujo texto contem CP1- + ULID (a leitura
// do QR vem com o_payload inteiro do template).
park.extractCode = function (raw) {
    var s = String(raw).trim().toUpperCase();
    var need = park.PREFIX.length + 26;
    if (s.length < need) return null;
    var last = s.length - need;
    for (var start = 0; start <= last; start++) {
        if (s.substr(start, park.PREFIX.length) !== park.PREFIX) continue;
        var body = s.substr(start + park.PREFIX.length, 26);
        if (park.isUlidBody(body)) return park.PREFIX + body;
    }
    return null;
};

park.qrPayload = function (template, code) {
    if (template && template.indexOf("{code}") >= 0) {
        return template.replace("{code}", code);
    }
    return code;
};

// RFC3339 UTC a partir do epoch (o Rust usava chrono to_rfc3339). O caminho
// pelas funcoes UTC do Date ignora o fuso do aparelho.
park.rfc3339 = function (epochSec) {
    var d = new Date(epochSec * 1000);
    function p2(n) { return (n < 10 ? "0" : "") + n; }
    return d.getUTCFullYear() + "-" + p2(d.getUTCMonth() + 1) + "-" + p2(d.getUTCDate()) +
        "T" + p2(d.getUTCHours()) + ":" + p2(d.getUTCMinutes()) + ":" + p2(d.getUTCSeconds()) + "Z";
};

// ---- nuvem (cloud/mod.rs HttpCloud) ----------------------------------------
// Um POST por acao em /functions/v1/parking-gate com apikey+Bearer (a chave
// publishable do Supabase) e device_id/device_secret/tenant_id no corpo.
// Respostas {ok, error, status, commands}; erros classificados como no Rust:
//   transporte caiu ............ offline    (re tentar depois)
//   401/403 .................... unauthorized
//   409 ....................... conflict   (codigo em outra faixa)
//   404/408/429/5xx ............ offline
//   outro 4xx / ok:false ....... rejected (decisao de negocio: not_paid...)
//   ok:false + "not_found" ..... notfound

park.call = function (cfg, action, extra) {
    var body = {};
    var k;
    for (k in extra) body[k] = extra[k];
    body.action = action;
    body.device_id = cfg.deviceId;
    body.device_secret = cfg.secret;
    if (cfg.tenantId) body.tenant_id = cfg.tenantId;

    var headers = {};
    if (cfg.key) {
        headers.apikey = cfg.key;
        headers.Authorization = "Bearer " + cfg.key;
    }

    var url = String(cfg.url).replace(/\/+$/, "") + "/functions/v1/parking-gate";
    var r = null;
    try {
        r = Net.request({
            url: url,
            method: "POST",
            body: JSON.stringify(body),
            contentType: "application/json",
            headers: headers
        });
    } catch (e) {
        // sem WiFi o Net lanca antes de tocar o radio
        return { error: "offline", detail: String(e) };
    }
    if (r === null || r === undefined) return { error: "offline", detail: "transporte" };

    if (r.status === 401 || r.status === 403) return { error: "unauthorized" };
    if (r.status === 409) return { error: "conflict" };
    if (r.status === 404 || r.status === 408 || r.status === 429 || r.status >= 500) {
        return { error: "offline", detail: "http " + r.status };
    }
    var v = null;
    try {
        v = JSON.parse(r.body);
    } catch (e) {
        v = null;
    }
    if (v === null || typeof v !== "object") v = { ok: false };
    if (r.status >= 400) {
        return { error: "rejected", detail: (v && v.error) ? v.error : ("http " + r.status) };
    }
    if (!v.ok && v.error === "not_found") return { error: "notfound" };
    if (!v.ok) return { error: "rejected", detail: (v && v.error) ? v.error : "recusado" };
    return v;
};

park.createTicket = function (cfg, code, enteredAt) {
    return park.call(cfg, "create_ticket", {
        code: code,
        entered_at: enteredAt || park.rfc3339(System.epoch())
    });
};

park.lookupTicket = function (cfg, code) {
    var r = park.call(cfg, "lookup_ticket", { code: code });
    if (r.error) return r;
    if (typeof r.status !== "string") return { error: "rejected", detail: "status ausente" };
    return { status: r.status };
};

park.markExited = function (cfg, code) {
    return park.call(cfg, "mark_exited", { code: code });
};

park.pullCommands = function (cfg) {
    var r = park.call(cfg, "pull_commands", {});
    if (r.error) return r;
    if (Object.prototype.toString.call(r.commands) !== "[object Array]") {
        return { commands: [] };
    }
    return { commands: r.commands };
};

park.ackCommand = function (cfg, id) {
    return park.call(cfg, "ack_command", { command_id: id });
};

park.heartbeat = function (cfg) {
    return park.call(cfg, "heartbeat", {});
};

// ---- fila local de entradas offline (cloud/outbox.rs, em JSON) -------------
// So o totem de ENTRADA usa: tickets impressos sem rede esperam aqui pelo
// reenvio (entered_at original preservado). Teto de 200 entradas.

park.outboxLoad = function (path) {
    try {
        if (!FS.exists(path)) return [];
        var v = JSON.parse(FS.readTextFile(path));
        if (Object.prototype.toString.call(v) === "[object Array]") return v;
    } catch (e) { /* arquivo corrompido: comeca de novo */ }
    return [];
};

park.outboxSave = function (path, list) {
    if (list.length > 200) list = list.slice(list.length - 200);
    try {
        return FS.writeTextFile(path, JSON.stringify(list));
    } catch (e) {
        park.log("fila local nao gravou: " + e);
        return false;
    }
};

park.outboxEnqueue = function (path, code, enteredAt) {
    var list = park.outboxLoad(path);
    var found = false;
    for (var i = 0; i < list.length; i++) {
        if (list[i].code === code) {
            list[i].entered_at = enteredAt;
            found = true;
            break;
        }
    }
    if (!found) list.push({ code: code, entered_at: enteredAt, status: "issued" });
    return park.outboxSave(path, list);
};

park.outboxRemove = function (path, code) {
    var list = park.outboxLoad(path);
    for (var i = 0; i < list.length; i++) {
        if (list[i].code === code) {
            list.splice(i, 1);
            break;
        }
    }
    return park.outboxSave(path, list);
};

// ---- cancela (hw/gate.rs): pulso no rele ------------------------------------

park.gatePulse = function (cfg, pin) {
    if (!(pin >= 0) || pin > 48) return false;
    var on = cfg.relayActiveLow ? System.gpio.LOW : System.gpio.HIGH;
    var off = cfg.relayActiveLow ? System.gpio.HIGH : System.gpio.LOW;
    try {
        System.gpio.pinMode(pin, System.gpio.OUTPUT);
        System.gpio.digitalWrite(pin, on);
        System.delay(cfg.pulseMs || 500);
        System.gpio.digitalWrite(pin, off);
        return true;
    } catch (e) {
        park.log("rele " + pin + " falhou: " + e);
        return false;
    }
};

park.gateOpen = function (cfg) {
    return park.gatePulse(cfg, cfg.relayPin);
};

park.gateClose = function (cfg) {
    return park.gatePulse(cfg, cfg.relayPinClose >= 0 ? cfg.relayPinClose : cfg.relayPin);
};

module.exports = park;
