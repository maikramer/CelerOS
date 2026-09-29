// HTTP Demo — exercita a API Net.* do CelerOS (API level 2)
// Busca a cotacao USD/BRL em uma API publica e desenha na tela. Toque na
// tela para atualizar.

var T = System.theme();
var URL = "https://economia.awesomeapi.com.br/json/last/USD-BRL";

function ctext(s, cx, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), y, f);
}

// Tela inteira redesenhada a cada estado (sem texto novo por cima do velho)
function draw(state, q) {
    System.fillScreen(T.bg);
    System.fillRect(0, 0, 240, 40, T.card);
    System.fillRect(0, 40, 240, 3, T.accent);
    System.setTextColor(T.text);
    System.drawString("HTTP Demo", 12, 12, 2);

    System.fillRoundRect(8, 54, 224, 40, 8, T.card);
    System.setTextColor(T.textDim);
    System.drawString("WiFi", 20, 60, 1);
    System.drawString("IP", 20, 76, 1);
    var on = Net.isConnected();
    System.setTextColor(on ? T.ok : T.warn);
    System.drawString(on ? "conectado" : "desligado", 70, 60, 1);
    System.setTextColor(T.text);
    System.drawString(on ? System.getIPAddress() : "--", 70, 76, 1);

    if (state === "loading") {
        ctext("Buscando USD/BRL...", 120, 150, 2, T.textDim);
    } else if (state === "fail") {
        ctext("Falha na requisição", 120, 136, 2, T.err);
        ctext("Confira o WiFi / internet.", 120, 162, 1, T.textDim);
    } else if (state === "ok") {
        ctext("USD/BRL", 120, 110, 2, T.textDim);
        ctext("R$ " + String(q.bid).substring(0, 5), 120, 134, 4, T.ok);
        ctext("max " + String(q.high).substring(0, 5) + "   min " + String(q.low).substring(0, 5),
              120, 180, 1, T.textDim);
        var d = String(q.create_date);
        ctext("awesomeapi  " + d.substring(0, 10) + " " + d.substring(11, 16), 120, 198, 1, T.textDim);
    }
    ctext("Toque para atualizar", 120, 290, 1, T.textDim);
}

function fetchQuote() {
    draw("loading");
    if (!Net.isConnected()) {
        draw("fail");
        return;
    }
    var data = null;
    try {
        data = Net.getJSON(URL);
    } catch (e) {
        data = null;
    }
    if (data && data.USDBRL) draw("ok", data.USDBRL);
    else draw("fail");
}

fetchQuote();
var down = false;
while (true) {
    var t = System.getTouch();
    if (t.touched) {
        down = true;
    } else if (down) {
        down = false;
        fetchQuote();  // soltou: atualiza
    }
    System.delay(30);
}
