// HTTP Demo — exercita a API Net.* do CelerOS. Duas fontes publicas
// (USD/BRL e BTC/USD) com latencia medida de cada requisicao, atualizacao
// automatica a cada 60s e botao para atualizar ja. Toolkit UI (API 22).

var T = System.theme();
var URL_USD = "https://economia.awesomeapi.com.br/json/last/USD-BRL";
var URL_BTC = "https://api.coinbase.com/v2/prices/BTC-USD/spot";
var AUTO_MS = 60000;
var LX = 8, LW = 224;

// estado por fonte: {state: idle|loading|ok|fail, ...dados}
var usd = { state: "idle" };
var btc = { state: "idle" };

function card(y, titulo, st, linhas) {
    UI.card(LX, y, LW, 96);
    UI.text(titulo, LX + 12, y + 10, { role: "caption", color: T.textDim });
    if (st.state === "ok") UI.badge(st.ms + " ms", LX + LW - 64, y + 8);
    if (st.state === "loading") {
        UI.spinner(120, y + 52, 14);
    } else if (st.state === "fail") {
        UI.text("falhou", 120, y + 34, { role: "title", align: "center", color: T.err });
        if (st.why) UI.text(st.why, 120, y + 66, { role: "caption", align: "center", color: T.textDim });
    } else if (st.state === "ok") {
        UI.text(st.big, 120, y + 30, { role: "display", align: "center", color: T.ok });
        for (var i = 0; i < linhas.length; i++) {
            UI.text(linhas[i], 120, y + 66 + i * 14, { role: "caption", align: "center", color: T.textDim, id: i });
        }
    } else {
        UI.text("-", 120, y + 40, { align: "center", color: T.textDim });
    }
    UI.cardEnd();
}

function screen(nextIn) {
    UI.begin(T.bg);
    var on = Net.isConnected();
    UI.card(LX, 8, LW, 32);
    UI.text("Wi-Fi", LX + 12, 16, { role: "caption", color: T.textDim });
    UI.text(on ? "conectado  " + System.getIPAddress() : "desligado", LX + LW - 12, 16,
            { role: "caption", align: "right", color: on ? T.ok : T.warn });
    UI.cardEnd();
    card(48, "USD/BRL  (awesomeapi)", usd,
         usd.state === "ok" ? ["max " + usd.high + "  min " + usd.low] : []);
    card(152, "BTC/USD  (coinbase)", btc,
         btc.state === "ok" ? ["spot " + btc.usd + " USD"] : []);
    var go = UI.button(nextIn === "-" ? "Atualizando..." : "Atualizar (auto em " + nextIn + " s)", LX, 262, LW, 44,
                       { disabled: nextIn === "-" });
    return go;
}
// frame avulso mostrando o "buscando" antes da chamada bloqueante
function show() {
    UI.invalidate();
    screen("-");
    UI.end();
}

function fetchUsd() {
    usd = { state: "loading" };
    show();
    if (!Net.isConnected()) { usd = { state: "fail", why: "sem internet" }; return; }
    var t0 = System.millis();
    var data = null;
    try { data = Net.getJSON(URL_USD); } catch (e) { data = null; }
    var ms = System.millis() - t0;
    if (data && data.USDBRL) {
        var q = data.USDBRL;
        usd = {
            state: "ok", ms: ms,
            big: "R$ " + String(q.bid).substring(0, 5),
            high: String(q.high).substring(0, 5), low: String(q.low).substring(0, 5)
        };
    } else {
        usd = { state: "fail", ms: ms, why: "sem resposta" };
    }
}

function fetchBtc() {
    btc = { state: "loading" };
    show();
    if (!Net.isConnected()) { btc = { state: "fail", why: "sem internet" }; return; }
    var t0 = System.millis();
    var data = null;
    try { data = Net.getJSON(URL_BTC); } catch (e) { data = null; }
    var ms = System.millis() - t0;
    if (data && data.data && data.data.amount) {
        var a = String(data.data.amount);
        var dot = a.indexOf(".");
        btc = { state: "ok", ms: ms, big: "$" + (dot > 0 ? a.substring(0, dot) : a), usd: a };
    } else {
        btc = { state: "fail", ms: ms, why: "sem resposta" };
    }
}

function fetchAll() {
    fetchUsd();
    fetchBtc();
    UI.invalidate();
}

fetchAll();
var nextAt = System.millis() + AUTO_MS;
while (true) {
    var now = System.millis();
    var inS = Math.max(0, Math.ceil((nextAt - now) / 1000));
    var tap = screen(inS);
    UI.end(10);
    if (tap || now >= nextAt) {
        fetchAll();
        nextAt = System.millis() + AUTO_MS;
    }
}
