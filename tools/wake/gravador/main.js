// Gravador de amostras do wake word pelo microfone do proprio cao.
// /local/rec/plano.txt = "<rotulo> <quantidade>" (ex.: "pos 25").
// Cada volta: bipe -> microfone grande na tela (FALE AGORA) -> 1,9 s
// gravados em /local/rec/<rotulo>_NN.wav -> pausa. Fim: duas notas descendo.
var PW = 128, PH = 64;
function PX(x) { return Math.round(x * 240 / PW); }
function PY(y) { return Math.round(y * 320 / PH); }
function PW_(w) { return Math.round(w * 240 / PW); }
function PH_(h) { return Math.round(h * 320 / PH); }

var plano = (FS.readTextFile("/local/rec/plano.txt") || "pos 20").replace(/\s+$/, "").split(" ");
var ROT = plano[0], N = parseInt(plano[1] || "20", 10);
try { FS.mkdir("/local/rec"); } catch (e) {}

function tela(gravando, lvl, feitos) {
    System.fillScreen(0);
    if (gravando) {
        System.fillRoundRect(PX(58), PY(8), PW_(12), PH_(24), PW_(6), 0xFFFF);
        System.drawRoundRect(PX(54), PY(20), PW_(20), PH_(18), PW_(9), 0xFFFF);
        System.fillRect(PX(63), PY(38), PW_(2), PH_(8), 0xFFFF);
        var h = Math.min(30, Math.round(3 + lvl * 0.6));
        System.fillRect(PX(36), PY(26 - h / 2), PW_(4), PH_(h), 0xFFFF);
        System.fillRect(PX(88), PY(26 - h / 2), PW_(4), PH_(h), 0xFFFF);
    } else {
        System.drawRoundRect(PX(58), PY(8), PW_(12), PH_(24), PW_(6), 0xFFFF);  // mic vazado = espere
    }
    System.drawRect(PX(4), PY(56), PW_(120), PH_(6), 0xFFFF);
    System.fillRect(PX(4), PY(56), PW_(Math.round(120 * feitos / N)), PH_(6), 0xFFFF);
}

System.delay(1500);
var salvos = 0;
for (var i = 0; i < N; i++) {
    tela(false, 0, i);
    System.delay(900);
    System.playTone([[1200, 70]]);
    System.delay(120);
    if (!Mic.start({ ms: 1900 })) { System.print("[rec] Mic.start falhou"); System.delay(500); continue; }
    while (Mic.recording()) { tela(true, Mic.level(), i); System.delay(40); }
    var wav = Mic.stop({ raw: true });
    var nome = "/local/rec/" + ROT + "_" + (i < 10 ? "0" : "") + i + ".wav";
    if (wav && FS.writeFile(nome, wav)) salvos++;
    System.print("[rec] " + nome + " " + (wav ? wav.length : 0) + " B");
}
tela(false, 0, N);
System.playTone([[900, 120], [600, 160]]);
System.print("[rec] fim " + ROT + " " + salvos + "/" + N);
System.delay(800);
System.exitApp();
