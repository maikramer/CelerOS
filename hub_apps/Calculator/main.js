// CelerOS Calculator — calculadora de toque no tema do OS.
// Previa ao vivo enquanto digita, historico das ultimas contas, ANS reutiliza
// o ultimo resultado (persistido no FS do app). ES5 (Duktape).

var T = System.theme();
var SW = System.screenWidth();
var SH = System.screenHeight();

var expression = "";
var result = "";
var history = [];              // ultimas contas resolvidas (na memoria)
var MAXHIST = 3;
var ANS = 0;

// ---- persistencia leve: ultimo resultado sobrevive ao app --------------------
var ANS_FILE = (FS.appData ? FS.appData() : "/local/") + "ans.txt";
(function () {
    var raw = FS.readTextFile(ANS_FILE);
    if (raw) {
        var v = parseFloat(raw);
        if (!isNaN(v)) ANS = v;
    }
})();
function saveAns() {
    FS.writeTextFile(ANS_FILE, String(ANS));
}

// ---- botoes -------------------------------------------------------------------
var buttons = [
    { l: "C",   r: 0, c: 0, type: "clear" },
    { l: "(",   r: 0, c: 1, type: "op" },
    { l: ")",   r: 0, c: 2, type: "op" },
    { l: "/",   r: 0, c: 3, type: "op" },

    { l: "7",   r: 1, c: 0, type: "num" },
    { l: "8",   r: 1, c: 1, type: "num" },
    { l: "9",   r: 1, c: 2, type: "num" },
    { l: "*",   r: 1, c: 3, type: "op" },

    { l: "4",   r: 2, c: 0, type: "num" },
    { l: "5",   r: 2, c: 1, type: "num" },
    { l: "6",   r: 2, c: 2, type: "num" },
    { l: "-",   r: 2, c: 3, type: "op" },

    { l: "1",   r: 3, c: 0, type: "num" },
    { l: "2",   r: 3, c: 1, type: "num" },
    { l: "3",   r: 3, c: 2, type: "num" },
    { l: "+",   r: 3, c: 3, type: "op" },

    { l: "DEL", r: 4, c: 0, type: "del" },
    { l: "0",   r: 4, c: 1, type: "num" },
    { l: ".",   r: 4, c: 2, type: "num" },
    { l: "=",   r: 4, c: 3, type: "eq" }
];

var btnW = 52;
var btnH = 38;
var spacing = 5;
var startX = 8;
var startY = 118;

function fmtNum(v) {
    if (typeof v !== "number" || isNaN(v) || !isFinite(v)) return String(v);
    var s = String(Math.round(v * 1e8) / 1e8);
    if (s.length > 14) s = v.toExponential(6);
    return s;
}

function btnRect(b) {
    return {
        x: startX + b.c * (btnW + spacing),
        y: startY + b.r * (btnH + spacing)
    };
}

// ---- desenho -------------------------------------------------------------------
function drawDisplay() {
    System.fillRoundRect(8, 34, SW - 16, 76, 10, T.card);
    System.drawRoundRect(8, 34, SW - 16, 76, 10, T.stroke);

    // historico: ultimas contas em cima, apagando
    System.setTextColor(T.textDim, T.card);
    var hy = 42;
    for (var i = Math.max(0, history.length - MAXHIST); i < history.length; i++) {
        System.drawString(history[i], 16, hy, 1);
        hy += 10;
    }

    // expressao corrente (fonte 2, cauda visivel)
    var e = expression;
    while (e.length > 0 && System.textWidth(e, 2) > SW - 40) e = e.substring(1);
    System.setTextColor(T.text, T.card);
    System.drawString(e || "0", 16, 66, 2);

    // resultado: previa viva (dim) ou fechado (accent, fonte grande)
    if (result !== "") {
        var rTxt = result;
        while (rTxt.length > 0 && System.textWidth(rTxt, 3) > SW - 40) rTxt = rTxt.substring(1);
        var isErr = result === "erro";
        System.setTextColor(isErr ? T.err : T.accent, T.card);
        System.drawString(rTxt, 16, 84, 3);
    } else {
        var prev = preview();
        if (prev !== null && expression !== "") {
            System.setTextColor(T.textDim, T.card);
            var p2 = "= " + fmtNum(prev);
            while (p2.length > 0 && System.textWidth(p2, 2) > SW - 40) p2 = p2.substring(1);
            System.drawString(p2, 16, 90, 2);
        }
    }
}

function preview() {
    if (expression === "") return null;
    // so previa se a expressao fecha (par balanceado e sem operador no fim)
    var bal = 0, lastCh = "";
    for (var i = 0; i < expression.length; i++) {
        var ch = expression.charAt(i);
        if (ch === "(") bal++;
        if (ch === ")") bal--;
        lastCh = ch;
    }
    if (bal !== 0 || "+-*/.".indexOf(lastCh) >= 0) return null;
    try {
        var v = eval(expression);
        return (typeof v === "number" && !isNaN(v) && isFinite(v)) ? v : null;
    } catch (e) { return null; }
}

function drawUI() {
    System.fillScreen(T.bg);
    drawDisplay();
    for (var i = 0; i < buttons.length; i++) {
        var b = buttons[i];
        var p = btnRect(b);
        var bg, fg;
        if (b.type === "eq") { bg = T.accent; fg = T.onAccent; }
        else if (b.type === "op") { bg = T.raised; fg = T.accent; }
        else if (b.type === "clear" || b.type === "del" || b.type === "ans") { bg = T.raised; fg = T.warn; }
        else { bg = T.card; fg = T.text; }
        System.fillRoundRect(p.x, p.y, btnW, btnH, 8, bg);
        System.drawRoundRect(p.x, p.y, btnW, btnH, 8, T.stroke);
        var f = b.l.length > 1 ? 2 : 3;
        System.setTextColor(fg, bg);
        System.drawString(b.l, p.x + (btnW - System.textWidth(b.l, f)) / 2,
                          p.y + (btnH - (f === 3 ? 20 : 14)) / 2, f);
    }
}

// ---- logica --------------------------------------------------------------------
function handleButton(b) {
    if (b.type === "num" || b.type === "op") {
        // comecar por operador encadeia no resultado anterior (ex.: "+5")
        if (b.type === "op" && expression === "" && ANS !== 0) {
            expression = fmtNum(ANS) + b.l;
        } else {
            expression += b.l;
        }
        result = "";
    } else if (b.type === "clear") {
        expression = "";
        result = "";
    } else if (b.type === "del") {
        if (expression.length > 0) expression = expression.substring(0, expression.length - 1);
        result = "";
    } else if (b.type === "eq") {
        var v = preview();
        if (v !== null) {
            result = fmtNum(v);
            ANS = v;
            saveAns();
            history.push(expression + " = " + result);
            if (history.length > 9) history.shift();
        } else if (expression === "") {
            result = fmtNum(ANS);       // "=" vazio mostra o ultimo resultado
        } else {
            result = "erro";
        }
    }
    drawDisplay();
}

drawUI();

var lastTouch = false;
while (true) {
    var t = System.getTouch();
    var isTapped = t.touched && !lastTouch;

    if (isTapped && t.y >= startY) {
        for (var i = 0; i < buttons.length; i++) {
            var b = buttons[i];
            var p = btnRect(b);
            if (t.x >= p.x && t.x <= p.x + btnW && t.y >= p.y && t.y <= p.y + btnH) {
                // flash de toque: realca e devolve a moldura do tema
                System.drawRoundRect(p.x, p.y, btnW, btnH, 8, T.text);
                System.delay(60);
                System.drawRoundRect(p.x, p.y, btnW, btnH, 8, T.stroke);
                handleButton(b);
                break;
            }
        }
    }

    lastTouch = t.touched;
    System.delay(15);
}
