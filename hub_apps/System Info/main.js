// CelerOS System Info — painel do aparelho no tema do OS.
// Cards de dispositivo, memoria, flash/armazenamento, rede, energia e
// sensores (quando a placa tem). Uptime e RAM ao vivo (1s). ES5 (Duktape).

var T = System.theme();
var W = 240;

function ctext(s, cx, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), y, f);
}

var kb = function (n) {
    if (n >= 1048576) return (n / 1048576).toFixed(1) + "MB";
    if (n >= 1024) return Math.round(n / 1024) + "KB";
    return n + "B";
};

function card(y, h, titulo) {
    System.fillRoundRect(8, y, W - 16, h, 10, T.card);
    System.drawRoundRect(8, y, W - 16, h, 10, T.stroke);
    System.setTextColor(T.textDim, T.card);
    System.drawString(titulo, 16, y + 7, 1);
    return y + 22;              // y da primeira linha de dados
}

function row(y, label, value, col) {
    System.setTextColor(T.textDim, T.card);
    System.drawString(label, 16, y, 1);
    System.setTextColor(col || T.text, T.card);
    var v = String(value);
    while (v.length > 0 && System.textWidth(v, 1) > 136) v = v.substring(1);
    System.drawString(v, 224 - System.textWidth(v, 1), y, 1);
}

function draw() {
    var i = System.getInfo();
    System.fillScreen(T.bg);

    // dispositivo
    var y = card(34, 56, "dispositivo");
    row(y, "chip", i.chipModel + " r" + i.chipRevision);
    row(y + 12, "CPU", i.chipCores + "x " + i.cpuFreqMHz + " MHz");
    row(y + 24, "firmware", "CelerOS " + System.getOSVersion() + " (API " + System.getAPILevel() + ")");

    // memoria
    y = card(96, 68, "memoria");
    row(y, "RAM livre", kb(i.freeRAM) + " de " + kb(i.totalRAM), i.freeRAM < 20000 ? T.warn : T.text);
    row(y + 12, "pico alocado", kb(i.maxAllocRAM));
    row(y + 24, "menor livre", kb(i.minFreeRAM));
    row(y + 36, "PSRAM", i.totalPSRAM ? kb(i.freePSRAM) + " de " + kb(i.totalPSRAM) : "nao");

    // armazenamento
    y = card(170, 44, "armazenamento");
    row(y, "flash", kb(i.flashSize));
    row(y + 12, "/local livre", kb(FS.getFreeSpace("/local")));

    // rede + energia
    y = card(220, 46, "rede e energia");
    var w = System.wifiStatus();
    row(y, "wifi", w.connected ? System.getIPAddress() : "desligado",
        w.connected ? T.ok : T.warn);
    var energia = "--";
    if (typeof System.battery === "function") {
        var mv = System.battery();
        energia = mv >= 0 ? (mv / 1000).toFixed(2) + "V" : "sem sensor";
    }
    row(y + 12, "bateria", energia);
}

// sensores: so na placa watch (API 13) — painel proprio embaixo quando ha
function drawSensors() {
    if (typeof Sensors === "undefined" || !Sensors.accel) {
        ctext("uptime " + Math.floor(System.getInfo().uptimeMs / 1000) +
              "s - atualiza ao vivo", 120, 300, 1, T.textDim);
        return;
    }
    var y = card(272, 44, "sensores");
    var a = Sensors.accel();
    row(y, "acel", a ? (a.x.toFixed(1) + ", " + a.y.toFixed(1) + ", " + a.z.toFixed(1)) : "--");
    row(y + 12, "passos", Sensors.steps ? Sensors.steps() : "--");
}

draw();
drawSensors();

var lastDraw = System.millis();
while (true) {
    var t = System.getTouch();
    if (t.touched && t.y < 30) break;   // X da faixa sai sozinho; toque no topo tambem
    if (System.millis() - lastDraw > 1000) {
        lastDraw = System.millis();
        draw();
        drawSensors();
    }
    System.delay(50);   // cede: GC e watchdog
}
