// CelerOS LED Controller — controla um LED/fita por GPIO direto.
// Pino escolhivel, liga/desliga, brilho PWM (analogWrite) e modo piscar.
// Requer permissao gpio no app.json. ES5 (Duktape).

var T = System.theme();
var W = 240;
var G = System.gpio;

var PIN_MIN = 2, PIN_MAX = 48;
var pin = 2;                    // LED onboard classico do ESP32
var on = false;
var duty = 100;                 // 0..100 (PWM quando < 100)
var mode = 0;                   // 0 fixo | 1 pisca lento | 2 pisca rapido
var MODES = [["fixo", 0], ["pisca", 700], ["rapido", 220]];

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

function ctext(s, cx, y, f, col, bg) {
    System.setTextColor(col, bg || T.bg);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), y, f);
}

function draw() {
    System.fillScreen(T.bg);

    // seletor de pino
    System.fillRoundRect(8, 34, W - 16, 36, 10, T.card);
    System.drawRoundRect(8, 34, W - 16, 36, 10, T.stroke);
    System.fillRoundRect(16, 42, 28, 20, 6, T.raised);
    ctext("-", 30, 47, 2, T.text, T.raised);
    System.setTextColor(T.textDim, T.card);
    System.drawString("GPIO", 56, 47, 1);
    System.setTextColor(T.accent, T.card);
    System.drawString(String(pin), 88, 44, 2);
    System.fillRoundRect(196, 42, 28, 20, 6, T.raised);
    ctext("+", 210, 47, 2, T.text, T.raised);

    // botao grande
    var bg = on ? T.ok : T.err;
    System.fillRoundRect(40, 84, 160, 100, 16, bg);
    System.drawRoundRect(40, 84, 160, 100, 16, T.stroke);
    ctext(on ? "LIGADO" : "DESLIGADO", 120, 116, 3, T.bg, bg);
    ctext("toque para alternar", 120, 152, 1, T.bg, bg);

    // brilho (PWM)
    System.setTextColor(T.textDim, T.bg);
    System.drawString("brilho " + duty + "%", 16, 200, 1);
    System.fillRoundRect(16, 212, 208, 10, 5, T.card);
    System.drawRoundRect(16, 212, 208, 10, 5, T.stroke);
    var fw = Math.round(208 * duty / 100);
    if (fw > 3) System.fillRoundRect(16, 212, fw, 10, 5, T.accent);
    System.fillCircle(16 + fw, 217, 6, T.text);

    // modo (toque cicla)
    System.fillRoundRect(16, 236, 208, 30, 8, T.card);
    System.drawRoundRect(16, 236, 208, 30, 8, T.stroke);
    ctext("modo: " + MODES[mode][0], 120, 245, 1, T.text, T.card);

    ctext("GPIO " + pin + (on ? " - duty " + duty + "%" : " - nivel 0"),
          120, 282, 1, T.textDim);
    ctext("confira o pino antes de ligar", 120, 296, 1, T.textDim);
}

setupPin();
draw();

var lastTouch = false;
var blinkOn = true;
var lastBlink = System.millis();
while (true) {
    var t = System.getTouch();
    var tap = t.touched && !lastTouch;
    var hold = t.touched && t.y >= 205 && t.y <= 228;   // arrastar no trilho

    if (hold) {
        var d = Math.round((t.x - 16) * 100 / 208);
        if (d < 0) d = 0;
        if (d > 100) d = 100;
        if (d !== duty) {
            duty = d;
            if (on && mode === 0) apply(duty);
            draw();
        }
    } else if (tap) {
        if (t.y >= 40 && t.y <= 64) {
            if (t.x < 50 && pin > PIN_MIN) { pin--; setupPin(); }
            else if (t.x > 190 && pin < PIN_MAX) { pin++; setupPin(); }
            draw();
        } else if (t.y >= 84 && t.y <= 184 && t.x >= 40 && t.x <= 200) {
            on = !on;
            apply(on ? duty : 0);
            draw();
        } else if (t.y >= 236 && t.y <= 266) {
            mode = (mode + 1) % MODES.length;
            if (mode === 0) apply(on ? duty : 0);
            draw();
        }
    }
    lastTouch = t.touched;

    // modo piscar: alterna o nivel no ritmo do modo
    if (on && mode > 0 && System.millis() - lastBlink >= MODES[mode][1]) {
        lastBlink = System.millis();
        blinkOn = !blinkOn;
        apply(blinkOn ? duty : 0);
    }

    System.delay(20);
}
