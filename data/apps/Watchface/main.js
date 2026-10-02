// CelerOS Watchface — relogio de pulso / relogio de mesa (API 13)
// ES5 puro (Duktape). Tela cheia (topbar:false): o canvas virtual 240x320
// escala para o vidro de cada placa (412x502 do watch, 480x480 da 4", ou a
// faixa da CYD) — o layout vive na banda central, longe dos cantos.
//
// Wallpaper: se existir /sd/wallpaper.png (tools/make_wallpapers.py gera e
// a copia vai para o SD da placa), ele e desenhado UMA vez e os elementos
// dinamicos redesenham em "pills" opacas por cima (estilo glass do firmware
// Rust do watch) — sem apagar o fundo. Sem wallpaper (CYD sem SD): caminho
// classico de redesenho total por segundo no quadro automatico da PSRAM.
//
// Swipe pra cima abre o launcher (alem do canto sup-direito, gatilho do OS).
// API 13: dia da semana (getWeekday), passos (Sensors.steps) e bateria.

var T = System.theme();
var WALL = "/sd/wallpaper.png";
var hasWall = false;
try { hasWall = FS.exists(WALL) && System.drawPNG(WALL, 0, 0); } catch (e) { hasWall = false; }

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

// "Pill" glass: retangulo arredondado escuro e opaco — redesenhar por cima
// do wallpaper nao deixa fantasma do texto anterior.
function pill(x, y, w, h) { System.fillRoundRect(x, y, w, h, 10, T.bg); }

function drawBattery() {
    if (lastBatt < 3000 || lastBatt > 5000) return;
    pill(78, 14, 84, 22);
    ctext((lastBatt / 1000).toFixed(1) + "V", 18, 1, T.textDim);
}

function drawClock() {
    pill(30, 80, 180, 70);
    ctext(System.getTime(), 100, 4, T.text);
}

function drawDate() {
    pill(48, 164, 144, 26);
    ctext(DIA[System.getWeekday()] + "  " + System.getDate().substring(0, 5), 170, 2, T.accent);
}

function drawSteps() {
    if (!hasImu) return;
    pill(40, 200, 160, 26);
    ctext(lastSteps + " passos", 206, 2, T.textDim);
}

function drawBar() {
    var sec = System.getSeconds();
    System.fillRoundRect(70, 248, 100, 10, 5, T.bg);
    var prog = Math.round(96 * sec / 60);
    if (prog > 0) System.fillRect(72, 250, prog, 6, T.accent);
}

function drawAll() {
    if (!hasWall) System.fillRect(0, 0, 240, 320, T.bg);
    drawBattery();
    drawClock();
    drawDate();
    drawSteps();
    drawBar();
}

drawAll();

// swipe pra cima (mais de 25 px virtuais) = launcher
var touchStartY = -1;
var touchLastY = -1;

while (true) {
    var t = System.getTime();
    var sec = System.getSeconds();
    var dateStr = System.getDate();
    var batt = System.battery();
    var steps = hasImu ? Sensors.steps() : -1;

    if (t !== lastTime || sec !== lastSec || batt !== lastBatt || steps !== lastSteps) {
        if (!hasWall) {
            // caminho classico: redesenho total (quadro automatico nao pisca)
            lastTime = t; lastSec = sec; lastBatt = batt; lastSteps = steps;
            drawAll();
        } else {
            // com wallpaper: so o que mudou, em pills opacas
            if (t !== lastTime) { drawClock(); drawDate(); lastTime = t; }
            if (sec !== lastSec) { drawBar(); lastSec = sec; }
            if (batt !== lastBatt) { drawBattery(); lastBatt = batt; }
            if (steps !== lastSteps) { drawSteps(); lastSteps = steps; }
            lastTime = t;  // date muda com o minuto (junto com o relogio)
        }
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
