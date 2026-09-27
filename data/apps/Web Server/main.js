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
function header(title) {
    System.fillRoundRect(0, 0, 240, 40, 0, T.card);
    System.setTextColor(T.text, T.card);
    System.drawString(title, 12, 12, 2);
    System.fillRect(0, 40, 240, 3, T.accent);
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

    System.fillScreen(T.bg);
    header("Web Server");

    var y = 56;
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
        System.fillRoundRect(8, y, 224, 84, 10, T.card);
        System.drawRoundRect(8, y, 224, 84, 10, T.stroke);
        System.setTextColor(T.textDim, T.card);
        System.drawString("Status", 20, y + 8, 1);
        System.setTextColor(T.ok, T.card);
        System.drawString("rodando", 188 - System.textWidth("rodando", 1) + 12, y + 8, 1);
        System.setTextColor(T.textDim, T.card);
        System.drawString("IP", 20, y + 26, 2);
        System.setTextColor(T.accent, T.card);
        System.drawString(st.ip, 70, y + 26, 2);
        System.setTextColor(T.textDim, T.card);
        System.drawString("Porta", 20, y + 46, 2);
        System.setTextColor(T.text, T.card);
        System.drawString("80", 70, y + 46, 2);
        y += 84 + 12;

        System.setTextColor(T.textDim, T.bg);
        System.drawString("Abra o IP no navegador para", 12, y, 1); y += 14;
        System.drawString("gerenciar arquivos e enviar firmware.", 12, y, 1);
    }

    // botao toggle
    if (st.webServer) {
        System.fillRoundRect(60, 235, 120, 36, 10, T.err);
        ctext("Desligar", 120, 253, 2, T.text, T.err);
    } else {
        System.fillRoundRect(60, 235, 120, 36, 10, T.accent);
        ctext("Ligar", 120, 253, 2, T.onAccent, T.accent);
    }

    // voltar
    System.fillRoundRect(8, 282, 84, 30, 8, T.raised);
    System.drawRoundRect(8, 282, 84, 30, 8, T.stroke);
    ctext("< Voltar", 50, 297, 2, T.text, T.raised);
}

draw();
var lastDraw = 0;
while (true) {
    var t = System.getTouch();  // canto sup. direito => OS_EXIT automatico
    if (t.touched) {
        if (hit(t, 60, 235, 120, 36)) {
            System.webSetActive(!st.webServer);
            waitRelease();
            draw();
        } else if (hit(t, 8, 282, 84, 30)) {
            System.exitApp();
        }
    }
    if (System.millis() - lastDraw > 1000) {  // IP pode chegar async
        lastDraw = System.millis();
        draw();
    }
    System.delay(20);
}
