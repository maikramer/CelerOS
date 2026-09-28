// CelerOS Web Server — app de sistema (W8). Liga/desliga o servidor web e
// mostra o IP de acesso (porta 80). UI no tema do OS (System.theme), toggle
// ao vivo via System.webSetActive (sem reboot). X no canto sup. direito sai.

var T = System.theme();

// ---- helpers de UI (padrao dos apps de sistema) ---------------------------
function ctext(s, cx, cy, f, col, bg) {
    System.setTextColor(col, bg);
    // centro vertical pela altura real da fonte (API 3+: System.fontHeight)
    var fh = System.fontHeight ? System.fontHeight(f) : (f >= 2 ? 16 : 10);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), cy - (fh >> 1), f);
}
function hit(t, x, y, w, h) {
    return t.x >= x && t.x <= x + w && t.y >= y && t.y <= y + h;
}
function waitRelease() {
    var guard = 0;
    while (guard < 200) {
        var t = System.getTouch();
        if (!t.touched) return;
        System.delay(10);
        guard++;
    }
}

var st;  // status atual (wifi/ip/servidor)

function draw() {
    st = System.wifiStatus();

    // o titulo vive na faixa do sistema; o app comeca direto no conteudo
    System.fillScreen(T.bg);

    var y = 12;
    var sp = 22;

    if (!st.connected) {
        System.setTextColor(T.warn, T.bg);
        System.drawString("WiFi desconectado", 12, y, 2); y += sp + 4;
        System.setTextColor(T.textDim, T.bg);
        System.drawString("Conecte o WiFi para usar", 12, y, 2); y += sp;
        System.drawString("o gerenciador web.", 12, y, 2);
    } else if (!st.webServer) {
        System.setTextColor(T.warn, T.bg);
        System.drawString("Servidor desligado", 12, y, 2); y += sp + 4;
        System.setTextColor(T.textDim, T.bg);
        System.drawString("Ligue para acessar arquivos", 12, y, 2); y += sp;
        System.drawString("e OTA pelo navegador.", 12, y, 2);
    } else {
        // card de status
        var url = "http://" + st.ip;
        System.fillRoundRect(8, y, 224, 92, 10, T.card);
        System.drawRoundRect(8, y, 224, 92, 10, T.stroke);
        System.fillCircle(22, y + 16, 4, T.ok);
        System.setTextColor(T.textDim, T.card);
        System.drawString("rodando - porta 80", 34, y + 11, 1);
        System.setTextColor(T.accent, T.card);
        var show = url;
        while (show.length > 3 && System.textWidth(show, 2) > 200) show = show.substring(1);
        System.drawString(show, 20, y + 30, 2);
        System.setTextColor(T.textDim, T.card);
        System.drawString("abra no navegador do PC/celular", 20, y + 56, 1);
        System.drawString("para arquivos e firmware (OTA)", 20, y + 70, 1);
        y += 92 + 12;
    }

    // botao toggle (o X da faixa do sistema sai do app)
    if (st.webServer) {
        System.fillRoundRect(30, 262, 180, 40, 10, T.err);
        ctext("Desligar", 120, 282, 2, T.text, T.err);
    } else {
        System.fillRoundRect(30, 262, 180, 40, 10, T.accent);
        ctext("Ligar", 120, 282, 2, T.onAccent, T.accent);
    }
}

draw();
var lastDraw = 0;
while (true) {
    var t = System.getTouch();  // canto sup. direito => OS_EXIT automatico
    if (t.touched) {
        if (hit(t, 30, 262, 180, 40)) {
            System.webSetActive(!st.webServer);
            waitRelease();
            draw();
        }
    }
    if (System.millis() - lastDraw > 1000) {  // IP pode chegar async
        lastDraw = System.millis();
        draw();
    }
    System.delay(20);
}
