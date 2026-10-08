// OtaBench — OTA pelo "hub" de bancada (tools/ota_server.py), sem tela.
// NÃO é app de fábrica. Lê a URL do firmware em /local/otabench_url.txt,
// chama System.otaStart registrando o progresso e grava o resultado em
// /local/otabench_out.txt. Não reinicia: o celerctl reinicia e confere a
// partição (celerctl info → app_part). Teste de retomada: o ota_server com
// --drop-at derruba a conexão no meio; o firmware tem que continuar do byte
// onde parou (Range) e terminar.
var OUT = "/local/otabench_out.txt";
var lines = [];
function L(s) {
    lines.push("t=" + System.millis() + " " + s);
    FS.writeTextFile(OUT, lines.join("\n") + "\n");
}
var url = (FS.readTextFile("/local/otabench_url.txt") || "").replace(/\s+$/, "");
System.keepAwake(true);
if (!url) {
    L("sem URL em /local/otabench_url.txt");
    System.exitApp();
}
L("inicio " + url);
var t0 = System.millis();
var last = -10;
var r = System.otaStart(url, function (pct) {
    if (pct - last >= 10 || pct === 100) {
        last = pct;
        L("progresso " + pct + "%");
    }
});
L("fim ok=" + r.ok + (r.ok ? "" : " erro=" + r.error) + " em " + (System.millis() - t0) + " ms");
while (true) {
    UI.begin();
    if (UI.header("OtaBench", { back: true })) System.exitApp();
    UI.text(lines.length ? lines[lines.length - 1] : "", 10, 60, { w: 220, lines: 4, id: "l" });
    UI.end();
}
