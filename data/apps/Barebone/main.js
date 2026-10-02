// Barebone — app de referencia headless (API 17).
//
// Nao desenha NADA: roda em qualquer placa e e a "cara" natural do devkit
// barebone (ESP32 sem vidro). Interface inteira = LED (System.led) +
// botao BOOT (System.button) + logcat (System.print).
//
//   curto no BOOT: proximo padrao de LED (persistido no Storage)
//   segurar ~1,2 s: sai do app (o SO cuida — igual o X da topbar)
//
// Para virar a casa do devkit: celerctl push autostart.txt /local/autostart.txt
// (conteudo: celeros.barebone) ou escreva /local/autostart.txt pelo navegador.

var PATTERNS = [
  { name: "apagado",      seq: [[0, 600]] },
  { name: "aceso",        seq: [[255, 600]] },
  { name: "pisca lento",  seq: [[255, 500], [0, 500]] },
  { name: "pisca rapido", seq: [[255, 120], [0, 120]] },
  { name: "heartbeat",    seq: [[255, 60], [0, 100], [255, 60], [0, 800]] },
  { name: "sos",          seq: [[255, 80], [0, 80], [255, 80], [0, 80], [255, 80], [0, 240],
                                [255, 240], [0, 80], [255, 240], [0, 80], [255, 240], [0, 240],
                                [255, 80], [0, 80], [255, 80], [0, 600]] }
];

var info = System.getInfo();
var headless = !info.hasDisplay;

System.print("Barebone: board=" + info.board +
      (headless ? " (headless)" : "") +
      " LED=" + (info.hasLed ? "sim" : "nao"));
if (!info.hasLed) {
  System.print("Barebone: esta placa nao tem LED no perfil; nada a piscar.");
}

var idx = parseInt(Storage.get("padrao", "0"), 10);
if (!(idx >= 0 && idx < PATTERNS.length)) idx = 0;
System.print("Barebone: padrao inicial '" + PATTERNS[idx].name +
      "' (curto troca, segurar sai)");

while (true) {
  var seq = PATTERNS[idx].seq;
  var i = 0;
  var trocou = false;
  while (!trocou) {
    var step = seq[i];
    // LED de canal unico (devkit): so o canal R existe — o nivel dele e o
    // brilho. Em placas RGB o padrao acende em vermelho, o que tambem serve.
    System.led(step[0], 0, 0);
    var t0 = System.millis();
    while (System.millis() - t0 < step[1]) {
      if (System.button() === 1) {
        idx = (idx + 1) % PATTERNS.length;
        if (!Storage.set("padrao", String(idx))) {
          System.print("Barebone: Storage cheio: padrao nao persiste");
        }
        System.print("Barebone: padrao '" + PATTERNS[idx].name + "'");
        trocou = true;
        break;
      }
      System.delay(20);
    }
    if (!trocou) i = (i + 1) % seq.length;
  }
  System.led(0, 0, 0);
}
