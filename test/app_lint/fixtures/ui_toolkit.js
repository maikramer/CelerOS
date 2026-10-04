// Fixture: toolkit UI (API 22). O loop cede pelo UI.end (sem aviso de loop);
// botao sem altura avisa aridade; app.json com api 21 avisa o nivel.
var T = System.theme();
while (true) {
    var full = UI.begin(T.bg);
    if (UI.button("OK", 10, 10, 100)) UI.invalidate();
    UI.end();
}
