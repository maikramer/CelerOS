// {{APP_NAME}} — app CelerOS (ES5/Duktape: sem let/const/arrow/Promise).
// Modelo do OS: o main.js e UMA funcao bloqueante — o loop roda ate o usuario
// tocar no X da barra do sistema (o OS cuida da saida; voce nao fecha nada).
// Regras de ouro:
//   - o loop abre com UI.begin() e fecha com UI.end() (present + ~30fps + GC);
//   - widgets UI.* desenham e testam o toque sozinhos (visual do sistema);
//   - desenho proprio (primitivas System.*) vai dentro de `if (full)`;
//   - coordenadas no espaco virtual 240x320 (o OS escala por placa);
//   - estado persistente em FS.appData()/Storage, nunca na pasta de outro app.

var T = System.theme();   // cores do sistema (veja celer.d.ts: CelerTheme)

var taps = 0;
var som = true;

while (true) {
    var full = UI.begin(T.bg);   // true = redesenho total (fundo ja pintado)

    UI.header("{{APP_NAME}}", { sub: "API " + System.getAPILevel() });

    UI.card(16, 56, 208, 128);
    UI.text("Ola, CelerOS!", 120, 72, { role: "title", align: "center" });
    // UI.text se redesenha sozinho quando o texto muda
    UI.text("toques: " + taps, 120, 108, { color: T.accent, align: "center" });
    UI.text("Som no toque", 32, 146);
    som = UI.toggle(164, 142, som);
    UI.cardEnd();

    if (full) {
        // desenho proprio do app: so no frame total
        System.fillSmoothCircle(120, 222, 18, T.accentD);
    }

    if (UI.button("Tocar", 16, 258, 208, 44)) {
        taps++;
        if (som) System.beep(880, 40);
    }

    UI.end();
}
