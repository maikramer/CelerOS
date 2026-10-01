// CelerOS Touch Test — valida display (barras RGB + rampa de cinza), touch
// (rastro continuo, coords vivas, min/max) e a topbar custom da API 6.
// Card de informacoes do aparelho ao vivo. Toque no X (canto sup. direito)
// para sair.

var T = System.theme();
var W = System.screenWidth();
var H = System.screenHeight();

if (typeof System.keepAwake === "function") {
    try { System.keepAwake(300000); } catch (e) {}
}

var touches = 0;
var samples = 0;
var minX = 9999, maxX = -1, minY = 9999, maxY = -1;
var last = null;      // ultimo ponto do rastro
var rate = 0;
var lastPt = { x: 0, y: 0 };
var YELLOW = System.color(255, 255, 0);
var BLACK = System.color(0, 0, 0);
var WHITE = System.color(255, 255, 255);

function fmtBar() {
    return "Toques: " + touches + "  amostras/s: " + rate;
}

function info() {
    var i = System.getInfo();
    var kb = function (n) { return n >= 1024 ? Math.round(n / 1024) + "KB" : n + "B"; };
    return [
        "CelerOS " + System.getOSVersion() + "  API " + System.getAPILevel(),
        i.chipModel + " x" + i.chipCores + " @" + i.cpuFreqMHz + "MHz",
        "RAM " + kb(i.freeRAM) + "/" + kb(i.totalRAM) +
            (i.totalPSRAM ? "  PSRAM " + kb(i.freePSRAM) : ""),
        i.idfVersion
    ];
}

function draw() {
    System.fillScreen(BLACK);

    // Barras verticais no topo: R | G | B | branco (confere ordem e niveis)
    System.fillRect(0, 26, W / 4, 54, System.color(255, 0, 0));
    System.fillRect(W / 4, 26, W / 4, 54, System.color(0, 255, 0));
    System.fillRect((2 * W) / 4, 26, W / 4, 54, System.color(0, 0, 255));
    System.fillRect((3 * W) / 4, 26, W - (3 * W) / 4, 54, WHITE);

    // Rampa de cinza em 16 niveis (2 linhas de 8): confere banding/quantizacao
    for (var g = 0; g < 16; g++) {
        var v = Math.round((g * 255) / 15);
        System.fillRect((g % 8) * (W / 8), 84 + Math.floor(g / 8) * 12,
                        W / 8, 12, System.color(v, v, v));
    }

    // card de informacoes
    System.fillRoundRect(6, 118, W - 12, 62, 8, T.card);
    System.setTextColor(T.textDim, T.card);
    System.drawString("dispositivo", 16, 124, 1);
    var inf = info();
    System.setTextColor(T.text, T.card);
    for (var i = 0; i < inf.length && i < 4; i++) {
        System.drawString(inf[i], 16, 136 + i * 11, 1);
    }

    System.setTextColor(WHITE, BLACK);
    System.drawString("arraste na area abaixo", 8, 192, 2);
    System.setTextColor(T.textDim, BLACK);
    System.drawString("chips na faixa de cima testam a API", 8, 214, 1);
    System.print("TouchTest: tela " + W + "x" + H);
}

// painel de estatisticas vivas (redesenhado a cada toque novo)
function drawStats() {
    System.fillRect(0, H - 44, W, 44, BLACK);
    System.setTextColor(T.accent, BLACK);
    System.drawString("x " + lastPt.x + "  y " + lastPt.y, 10, H - 40, 2);
    System.setTextColor(T.textDim, BLACK);
    var range = "faixa x " + (minX === 9999 ? "-" : minX + "-" + maxX) +
                "  y " + (minY === 9999 ? "-" : minY + "-" + maxY);
    System.drawString(range, 10, H - 22, 1);
}

// faixa do sistema: texto custom + chips (API 6)
System.topbarButtons(["+", "Limpar"]);
draw();
System.topbarText(fmtBar());
drawStats();

var rateWin = System.millis();
var down = false;
while (true) {
    var t = System.getTouch();
    if (t.touched) {
        samples++;
        if (last) System.drawLine(last.x, last.y, t.x, t.y, YELLOW);
        System.fillCircle(t.x, t.y, 3, YELLOW);
        last = { x: t.x, y: t.y };
        lastPt.x = t.x; lastPt.y = t.y;
        if (t.x < minX) minX = t.x;
        if (t.x > maxX) maxX = t.x;
        if (t.y < minY) minY = t.y;
        if (t.y > maxY) maxY = t.y;
        if (!down) {
            down = true;
            touches++;
            System.topbarText(fmtBar());
            System.print("TouchTest: x=" + t.x + " y=" + t.y);
            drawStats();
        }
    } else {
        down = false;
        last = null;
    }

    var now = System.millis();
    if (now - rateWin >= 1000) {
        rate = samples;          // amostras no ultimo segundo
        samples = 0;
        rateWin = now;
        System.topbarText(fmtBar());
    }

    // chips da faixa: "+" soma 10, "Limpar" zera tudo e redesenha o fundo
    var id = System.topbarPop();
    if (id === "+") {
        touches += 10;
        System.topbarText(fmtBar());
    } else if (id === "Limpar") {
        touches = 0;
        minX = minY = 9999; maxX = maxY = -1;
        draw();
        System.topbarText(fmtBar());
        drawStats();
    }

    System.delay(10);
}
