// CelerOS Touch Test — valida display (barras RGB), touch (pontos/coords) e
// a topbar custom da API 6 (topbarText + topbarButtons/topbarPop).
// Toque no X (canto superior direito) para sair.

var T = System.theme();
var W = System.screenWidth();
var H = System.screenHeight();

var touches = 0;
var YELLOW = System.color(255, 255, 0);
var BLACK = System.color(0, 0, 0);
var WHITE = System.color(255, 255, 255);

function fmtBar() {
    return "Toques: " + touches;
}

function draw() {
    System.fillScreen(BLACK);

    // Barras verticais no topo: R | G | B | branco (confere ordem e niveis)
    System.fillRect(0, 30, W / 4, 70, System.color(255, 0, 0));
    System.fillRect(W / 4, 30, W / 4, 70, System.color(0, 255, 0));
    System.fillRect((2 * W) / 4, 30, W / 4, 70, System.color(0, 0, 255));
    System.fillRect((3 * W) / 4, 30, W - (3 * W) / 4, 70, WHITE);

    System.setTextColor(WHITE, BLACK);
    System.drawString("toque na tela", 8, 120, 2);
    System.setTextColor(T.textDim, BLACK);
    System.drawString("chips na faixa de cima testam a API", 8, 142, 1);
    System.print("TouchTest: tela " + W + "x" + H);
}

// faixa do sistema: texto custom + chips (API 6)
System.topbarButtons(["+", "Limpar"]);
draw();
System.topbarText(fmtBar());

var last = 0;
while (true) {
    var t = System.getTouch();
    if (t.touched) {
        System.fillCircle(t.x, t.y, 4, YELLOW);
        if (System.millis() - last > 300) {
            touches++;
            System.topbarText(fmtBar());
            System.print("TouchTest: x=" + t.x + " y=" + t.y);
            last = System.millis();
        }
    }

    // chips da faixa: "+" soma 10, "Limpar" zera
    var id = System.topbarPop();
    if (id === "+") {
        touches += 10;
        System.topbarText(fmtBar());
    } else if (id === "Limpar") {
        touches = 0;
        System.topbarText(fmtBar());
    }

    System.delay(10);
}
