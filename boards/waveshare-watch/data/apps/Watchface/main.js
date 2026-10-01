// CelerOS Watchface — relogio de pulso do waveshare-watch (API 13)
// ES5 puro (Duktape). Tela cheia (topbar:false): o canvas virtual 240x320
// escala para o vidro 412x502 e os glifos crescem uniformes — o layout
// vive na banda central, longe dos cantos do vidro arredondado.
// Redesenho por segundo no quadro automatico da PSRAM (sem flicker);
// swipe pra cima abre o launcher (alem do canto sup-direito, gatilho do OS).
//
// API 13: dia da semana (System.getWeekday) e passos do dia (Sensors.steps,
// pedometro do QMI8658 portado do firmware Rust).

var T = System.theme();
var DIA = ["DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"];
var META_PASSOS = 8000;

var lastTime = "";
var lastSec = -1;
var lastBatt = -1;
var lastSteps = -1;
var hasImu = false;
try { hasImu = (typeof Sensors !== "undefined") && Sensors && Sensors.steps() >= 0; } catch (e) { hasImu = false; }

function ctext(s, y, font, color) {
    System.setTextColor(color);
    System.drawString(s, Math.round((240 - System.textWidth(s, font)) / 2), y, font);
}

function draw() {
    System.fillRect(0, 0, 240, 320, T.bg);

    var t = System.getTime();      // "HH:MM" ou "HH:MM AM" (prefs do usuario)
    var d = System.getDate();      // "DD/MM/YYYY"
    var sec = System.getSeconds();

    // bateria (mV da celula) — so quando a board sabe medir
    if (lastBatt >= 3000 && lastBatt <= 5000) {
        ctext((lastBatt / 1000).toFixed(1) + "V", 22, 2, T.textDim);
    }

    ctext(t, 92, 4, T.text);
    ctext(DIA[System.getWeekday()] + "  " + d.substring(0, 5), 168, 2, T.accent);

    // passos do dia (QMI8658): numero + barra ate a meta
    if (hasImu) {
        ctext(lastSteps + " passos", 205, 2, T.textDim);
        System.fillRect(70, 222, 100, 3, T.stroke);
        var prog = lastSteps >= META_PASSOS ? 100 : Math.round(100 * lastSteps / META_PASSOS);
        System.fillRect(70, 222, prog, 3, T.ok);
    }

    // trilha de segundos: barra fina que cresce 0..100% dentro do minuto
    System.fillRect(70, 252, 100, 3, T.stroke);
    System.fillRect(70, 252, Math.round(100 * sec / 60), 3, T.accent);
}

// swipe pra cima (mais de 25 px virtuais) = launcher
var touchStartY = -1;
var touchLastY = -1;

while (true) {
    var t = System.getTime();
    var sec = System.getSeconds();
    var batt = System.battery();
    var steps = hasImu ? Sensors.steps() : -1;
    if (t !== lastTime || sec !== lastSec || batt !== lastBatt || steps !== lastSteps) {
        lastTime = t;
        lastSec = sec;
        lastBatt = batt;
        lastSteps = steps;
        draw();
    }

    var tp = System.getTouch();
    if (tp.touched) {
        if (touchStartY < 0) touchStartY = tp.y;
        touchLastY = tp.y;
    } else if (touchStartY >= 0) {
        if (touchStartY - touchLastY > 25) System.exitApp();
        touchStartY = -1;
        touchLastY = -1;
    }

    System.delay(150);
}
