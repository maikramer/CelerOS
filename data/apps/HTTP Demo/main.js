// HTTP Demo — exercita a API Net.* do CelerOS (API level 7)
// Duas fontes publicas (USD/BRL e BTC/USD) com latencia medida de cada
// requisicao, atualizacao automatica a cada 60s e toque para atualizar ja.

var T = System.theme();
var URL_USD = "https://economia.awesomeapi.com.br/json/last/USD-BRL";
var URL_BTC = "https://api.coinbase.com/v2/prices/BTC-USD/spot";
var AUTO_MS = 60000;

function ctext(s, cx, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), y, f);
}
function ltext(s, x, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, x, y, f);
}

// estado por fonte: {state: idle|loading|ok|fail, ...dados}
var usd = { state: "idle" };
var btc = { state: "idle" };

function drawCard(x, y, w, h, titulo, st, linhas) {
    System.fillRoundRect(x, y, w, h, 8, T.card);
    System.drawRoundRect(x, y, w, h, 8, T.stroke);
    ltext(titulo, x + 10, y + 8, 1, T.textDim);
    var cy = y + 24;
    if (st.state === "loading") {
        ctext("buscando...", x + w / 2, cy + 6, 2, T.textDim);
    } else if (st.state === "fail") {
        ctext("falhou", x + w / 2, cy + 2, 2, T.err);
        if (st.why) ctext(st.why, x + w / 2, cy + 20, 1, T.textDim);
    } else if (st.state === "ok") {
        ctext(st.big, x + w / 2, cy, 3, T.ok);
        for (var i = 0; i < linhas.length; i++) {
            ctext(linhas[i], x + w / 2, cy + 26 + i * 12, 1, T.textDim);
        }
        ctext(st.ms + " ms", x + w - 8, y + 8, 1, T.accent);
    } else {
        ctext("-", x + w / 2, cy + 6, 2, T.textDim);
    }
}

function draw(nextIn) {
    System.fillScreen(T.bg);
    System.fillRect(0, 0, 240, 34, T.card);
    System.fillRect(0, 34, 240, 3, T.accent);
    ltext("HTTP Demo", 12, 10, 2, T.text);

    // linha de rede
    var on = Net.isConnected();
    System.fillRoundRect(8, 44, 224, 26, 8, T.card);
    ltext("WiFi", 18, 52, 1, T.textDim);
    System.setTextColor(on ? T.ok : T.warn);
    System.drawString(on ? "conectado" : "desligado", 62, 52, 1);
    if (on) ltext(System.getIPAddress(), 150, 52, 1, T.text);

    drawCard(8, 78, 224, 84, "USD/BRL  (awesomeapi)", usd,
             usd.state === "ok" ? ["max " + usd.high + "  min " + usd.low, usd.date] : []);
    drawCard(8, 168, 224, 84, "BTC/USD  (coinbase)", btc,
             btc.state === "ok" ? ["spot " + btc.usd + " USD", btc.date] : []);

    ctext("atualiza em " + nextIn + "s - toque para atualizar ja", 120, 292, 1, T.textDim);
}

function fetchUsd() {
    usd = { state: "loading" };
    draw("-");
    if (!Net.isConnected()) { usd = { state: "fail", why: "sem internet" }; draw("-"); return; }
    var t0 = System.millis();
    var data = null;
    try { data = Net.getJSON(URL_USD); } catch (e) { data = null; }
    var ms = System.millis() - t0;
    if (data && data.USDBRL) {
        var q = data.USDBRL;
        usd = {
            state: "ok", ms: ms,
            big: "R$ " + String(q.bid).substring(0, 5),
            high: String(q.high).substring(0, 5), low: String(q.low).substring(0, 5),
            date: String(q.create_date).substring(0, 16).replace("T", " ")
        };
    } else {
        usd = { state: "fail", ms: ms, why: "sem resposta" };
    }
}

function fetchBtc() {
    btc = { state: "loading" };
    draw("-");
    if (!Net.isConnected()) { btc = { state: "fail", why: "sem internet" }; draw("-"); return; }
    var t0 = System.millis();
    var data = null;
    try { data = Net.getJSON(URL_BTC); } catch (e) { data = null; }
    var ms = System.millis() - t0;
    if (data && data.data && data.data.amount) {
        var a = String(data.data.amount);
        var dot = a.indexOf(".");
        btc = {
            state: "ok", ms: ms,
            big: "$" + (dot > 0 ? a.substring(0, dot) : a),
            usd: a, date: "agora"
        };
    } else {
        btc = { state: "fail", ms: ms, why: "sem resposta" };
    }
}

function fetchAll() {
    fetchUsd();
    draw("-");
    fetchBtc();
    draw("-");
}

fetchAll();
var nextAt = System.millis() + AUTO_MS;
var down = false;
while (true) {
    var t = System.getTouch();
    if (t.touched) {
        down = true;
    } else if (down) {
        down = false;
        fetchAll();              // soltou: atualiza
        nextAt = System.millis() + AUTO_MS;
    }

    var now = System.millis();
    var inS = Math.max(0, Math.ceil((nextAt - now) / 1000));
    if (now >= nextAt) {
        fetchAll();              // auto-refresh
        nextAt = now + AUTO_MS;
        inS = Math.round(AUTO_MS / 1000);
    }
    if (now - (draw._at || 0) > 1000) {   // conta de 1s no rodape
        draw._at = now;
        draw(inS);
    }
    System.delay(30);
}
