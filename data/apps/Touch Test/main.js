// KryonOS Touch Test — valida display (barras RGB) e touch (alvos + coords no serial)
var W = System.screenWidth();
var H = System.screenHeight();

System.fillScreen(System.color(0, 0, 0));

// Barras verticais: R | G | B | branco (confere ordem e niveis de cor)
System.fillRect(0, 0, W / 4, H, System.color(255, 0, 0));
System.fillRect(W / 4, 0, W / 4, H, System.color(0, 255, 0));
System.fillRect((2 * W) / 4, 0, W / 4, H, System.color(0, 0, 255));
System.fillRect((3 * W) / 4, 0, W - (3 * W) / 4, H, System.color(255, 255, 255));

System.setTextColor(System.color(255, 255, 255), System.color(0, 0, 0));
System.drawString("Touch Test " + W + "x" + H, 8, 8, 2);
System.drawString("Toque na tela. X para sair.", 8, 28, 2);

System.print("TouchTest: tela " + W + "x" + H + " — toque para ver as coordenadas");

var last = 0;
while (true) {
    var t = System.getTouch();
    if (t.touched) {
        System.fillCircle(t.x, t.y, 4, System.color(255, 255, 0));
        if (System.millis() - last > 400) {
            System.print("TouchTest: x=" + t.x + " y=" + t.y);
            last = System.millis();
        }
    }
    System.delay(10);
}
