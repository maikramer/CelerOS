// HTTP Demo — exercita a API Net.* do CelerOS (API level 2)
// Busca a cotacao USD/BRL em uma API publica HTTP e desenha na tela.

var W = System.screenWidth();
var H = System.screenHeight();

var DIM = System.color(139, 152, 169);
var TXT = System.color(241, 245, 249);
var ACC = System.color(34, 211, 238);
var OK = System.color(34, 197, 94);
var ERR = System.color(239, 68, 68);

System.fillScreen(0);
System.setTextColor(ACC, 0);
System.drawString("HTTP Demo", 10, 10, 2);
System.setTextColor(DIM, 0);
System.drawString("Net.* API (level 2)", 10, 30, 1);

System.setTextColor(TXT, 0);
System.drawString("WiFi: " + (Net.isConnected() ? "connected" : "OFF"), 10, 55, 2);
System.drawString("IP: " + System.getIPAddress(), 10, 75, 2);

System.setTextColor(DIM, 0);
System.drawString("Fetching USD/BRL...", 10, 110, 2);

var url = "http://economia.awesomeapi.com.br/json/last/USD-BRL";
var data = null;
var failed = false;

if (Net.isConnected()) {
    try {
        data = Net.getJSON(url);
    } catch (e) {
        failed = true;
    }
    if (data === null) failed = true;
} else {
    failed = true;
}

if (failed) {
    System.setTextColor(ERR, 0);
    System.drawString("Request failed.", 10, 110, 2);
    System.setTextColor(DIM, 0);
    System.drawString("Check WiFi / internet and", 10, 132, 1);
    System.drawString("try again.", 10, 144, 1);
} else {
    var q = data.USDBRL;
    System.setTextColor(DIM, 0);
    System.drawString("USD/BRL", 10, 105, 2);
    System.setTextColor(OK, 0);
    System.drawString("R$ " + String(q.bid).substring(0, 5), 10, 128, 4);
    System.setTextColor(DIM, 0);
    System.drawString("high " + String(q.high).substring(0, 5) +
                      "  low " + String(q.low).substring(0, 5), 10, 170, 2);
    var day = String(q.create_date).substring(0, 10);
    var tim = String(q.create_date).substring(11, 16);
    System.drawString("source: awesomeapi " + day + " " + tim, 10, 190, 1);
}

System.setTextColor(DIM, 0);
System.drawString("Tap X (top-right) to exit", 10, H - 16, 1);

while (true) {
    System.getTouch();  // o toque no X dispara o OS_EXIT
    System.delay(50);
}
