// CelerOS LED Controller — controla um LED/fita por GPIO direto.
// Pino escolhivel, liga/desliga, brilho PWM (analogWrite) e modo piscar.
// Requer permissao gpio no app.json. ES5 (Duktape), toolkit UI (API 22).

var T = System.theme();
var W = 240;
var G = System.gpio;

var PIN_MIN = 2, PIN_MAX = 48;
var pin = 2;                    // LED onboard classico do ESP32
var on = false;
var duty = 100;                 // 0..100 (PWM quando < 100)
var mode = 0;                   // 0 fixo | 1 pisca lento | 2 pisca rapido
var MODES = [["Fixo", 0], ["Pisca", 700], ["Rápido", 220]];

function apply(level) {         // level 0..100 no pino
    if (level <= 0) {
        G.digitalWrite(pin, G.LOW);
    } else if (level >= 100) {
        G.digitalWrite(pin, G.HIGH);
    } else if (G.analogWrite) {
        G.analogWrite(pin, Math.round(level * 255 / 100));
    } else {
        G.digitalWrite(pin, G.HIGH);
    }
}

function setupPin() {
    G.pinMode(pin, G.OUTPUT);
    apply(on ? duty : 0);
}

setupPin();

var blinkOn = true;
var lastBlink = System.millis();
while (true) {
    UI.begin(T.bg);

    // seletor de pino
    UI.card(8, 8, W - 16, 48);
    if (UI.button("-", 16, 16, 40, 32, { style: "ghost", role: "title", disabled: pin <= PIN_MIN }) && pin > PIN_MIN) {
        pin--;
        setupPin();
    }
    UI.text("GPIO " + pin, W / 2, 20, { role: "title", align: "center", color: T.accent });
    if (UI.button("+", W - 56, 16, 40, 32, { style: "ghost", role: "title", disabled: pin >= PIN_MAX }) && pin < PIN_MAX) {
        pin++;
        setupPin();
    }
    UI.cardEnd();

    // botao grande liga/desliga
    if (UI.button(on ? "LIGADO" : "DESLIGADO", 24, 68, W - 48, 92,
                  { color: on ? T.ok : T.raised, textColor: on ? T.onAccent : T.textDim, role: "title" })) {
        on = !on;
        apply(on ? duty : 0);
    }

    // brilho (PWM): aplica ao vivo no modo fixo
    UI.card(8, 172, W - 16, 74);
    UI.text("Brilho", 20, 182);
    UI.text(duty + "%", W - 20, 182, { align: "right", color: T.accent });
    var d = UI.slider(24, 208, W - 48, duty);
    if (d !== duty) {
        duty = d;
        if (on && mode === 0) apply(duty);
    }
    UI.cardEnd();

    // modo
    var labels = [MODES[0][0], MODES[1][0], MODES[2][0]];
    var m = UI.tabs(8, 256, W - 16, 32, labels, mode);
    if (m !== mode) {
        mode = m;
        if (mode === 0) apply(on ? duty : 0);
    }
    UI.text("confira o pino antes de ligar", W / 2, 300, { role: "caption", align: "center", color: T.textDim });

    // modo piscar: alterna o nivel no ritmo do modo
    if (on && mode > 0 && System.millis() - lastBlink >= MODES[mode][1]) {
        lastBlink = System.millis();
        blinkOn = !blinkOn;
        apply(blinkOn ? duty : 0);
    }
    UI.end();
}
