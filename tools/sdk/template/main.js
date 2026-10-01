// {{APP_NAME}} — app CelerOS (ES5/Duktape: sem let/const/arrow/Promise).
// Modelo do OS: o main.js e UMA funcao bloqueante — o loop roda ate o usuario
// tocar no X da barra do sistema (o OS cuida da saida; voce nao fecha nada).
// Regras de ouro:
//   - todo loop precisa de System.delay() (o garbage collector roda dentro);
//   - coordenadas no espaco virtual 240x320 (o OS escala por placa);
//   - estado persistente em FS.appData(), nunca na pasta de outro app.

var T = System.theme();   // cores do sistema (veja celer.d.ts: CelerTheme)
var W = 240, H = 320;

var taps = 0;

function center(s, cy, font, col, bg) {
    System.setTextColor(col, bg);
    System.drawString(s, (W - System.textWidth(s, font)) / 2, cy, font);
}

function draw() {
    System.fillScreen(T.bg);
    center("Ola, CelerOS!", 70, 2, T.text, T.bg);
    // o circulo cresce um pouco a cada toque
    System.fillCircle(W / 2, 170, 12 + (taps % 20), T.accent);
    center("toque na tela", 230, 1, T.textDim, T.bg);
    center("toques: " + taps, 255, 2, T.accent, T.bg);
}

draw();

while (true) {
    var t = System.getTouch();
    if (t.touched) {
        taps++;
        draw();
        // espera o dedo soltar (evita contar o mesmo toque N vezes)
        while (System.getTouch().touched) System.delay(10);
    }
    System.delay(30);   // ~33fps e o GC respira
}
