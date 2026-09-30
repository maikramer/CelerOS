// Fixture: app limpo — nao deve produzir diagnostico nenhum.
var T = System.theme();
var W = System.screenWidth();
var H = System.screenHeight();

function bg(c) { System.fillScreen(c || T.bg); }

bg(T.card);
System.fillRect(10, 10, 50, 20, T.accent);
System.drawString("Olá, aparelho!", 12, 15, 2);
System.setTextColor(T.text, T.bg);
var cor = System.color(255, 128, 0);

var hist = [];
function tap() {
    var t = System.getTouch();
    if (t) hist.push(t);
}

if (typeof CelerLink !== "undefined") {
    CelerLink.send("ping");
}

while (true) {
    tap();
    if (hist.length > 100) hist = [];
    System.delay(20);
}
