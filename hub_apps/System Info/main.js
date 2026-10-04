// CelerOS System Info — painel do aparelho no tema do OS.
// Cards de dispositivo, memoria, flash/armazenamento, rede, energia e
// sensores (quando a placa tem). Uptime e RAM ao vivo (1s). ES5 (Duktape),
// toolkit UI (API 22): UI.text redesenha so o valor que mudou.

var T = System.theme();
var W = 240, LX = 8, LW = W - 16;

var kb = function (n) {
    if (n >= 1048576) return (n / 1048576).toFixed(1) + " MB";
    if (n >= 1024) return Math.round(n / 1024) + " KB";
    return n + " B";
};
function fmtUptime(ms) {
    var s = Math.floor(ms / 1000);
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
    return (h > 0 ? h + "h " : "") + m + "m " + (s % 60) + "s";
}

function card(y, h, titulo) {
    UI.card(LX, y, LW, h);
    UI.text(titulo, LX + 10, y + 6, { role: "caption", color: T.accent });
    return y + 24;              // y da primeira linha de dados
}
function row(y, label, value, col) {
    UI.text(label, LX + 10, y, { role: "caption", color: T.textDim });
    UI.text(String(value), LX + LW - 10, y, { role: "caption", color: col || T.text, align: "right", w: 140 });
}

var i = System.getInfo();
var lastPoll = 0;
var hasSensors = typeof Sensors !== "undefined" && !!Sensors.accel;
while (true) {
    UI.begin(T.bg);
    if (System.millis() - lastPoll > 1000) {
        lastPoll = System.millis();
        i = System.getInfo();
    }

    // dispositivo
    var y = card(6, 70, "Dispositivo");
    row(y, "chip", i.chipModel + " r" + i.chipRevision);
    row(y + 14, "CPU", i.chipCores + "x " + i.cpuFreqMHz + " MHz");
    row(y + 28, "firmware", "CelerOS " + System.getOSVersion() + " · API " + System.getAPILevel());
    UI.cardEnd();

    // memoria: barra de uso da RAM interna
    y = card(82, 80, "Memória");
    var used = i.totalRAM ? Math.round((i.totalRAM - i.freeRAM) * 100 / i.totalRAM) : 0;
    row(y, "RAM livre", kb(i.freeRAM) + " de " + kb(i.totalRAM), i.freeRAM < 20000 ? T.warn : T.text);
    UI.progress(LX + 10, y + 16, LW - 20, 8, used);
    row(y + 28, "maior bloco", kb(i.maxAllocRAM));
    row(y + 42, "PSRAM", i.totalPSRAM ? kb(i.freePSRAM) + " de " + kb(i.totalPSRAM) : "não");
    UI.cardEnd();

    // armazenamento
    y = card(168, 52, "Armazenamento");
    row(y, "flash", kb(i.flashSize));
    row(y + 14, "/local livre", kb(FS.getFreeSpace("/local")));
    UI.cardEnd();

    // rede + energia
    y = card(226, 52, "Rede e energia");
    var w = System.wifiStatus();
    row(y, "Wi-Fi", w.connected ? System.getIPAddress() : "desligado", w.connected ? T.ok : T.warn);
    var energia = "--";
    if (typeof System.battery === "function") {
        var mv = System.battery();
        energia = mv >= 0 ? (mv / 1000).toFixed(2) + " V" : "sem sensor";
    }
    row(y + 14, "bateria", energia);
    UI.cardEnd();

    // rodape: sensores (watch, API 13) ou uptime ao vivo
    if (hasSensors) {
        var a = Sensors.accel();
        UI.text("acel " + (a ? a.x.toFixed(1) + ", " + a.y.toFixed(1) + ", " + a.z.toFixed(1) : "--") +
                "  ·  passos " + (Sensors.steps ? Sensors.steps() : "--"), 120, 290,
                { role: "caption", align: "center", color: T.textDim });
    } else {
        UI.text("ligado há " + fmtUptime(i.uptimeMs), 120, 290, { role: "caption", align: "center", color: T.textDim });
    }
    UI.end(10);
}
