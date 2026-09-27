// KryonOS Calculator — expressao com precedencia e parenteses (API 3)
// ES5 puro (Duktape). Preview do resultado enquanto digita; = confirma e
// encadeia (o resultado vira o inicio da proxima expressao).

var T = System.theme();
var W = 240, H = 320;
var MAXLEN = 42;

var expr = "";        // expressao crua (digitos . + - * / ( ))
var typed = "";       // expressao digitada (exibida apos =)
var result = null;    // resultado confirmado pelo =
var showErr = 0;      // timestamp do aviso "Erro"
var flash = -1;       // indice da tecla com feedback visual
var flashAt = 0;

// ------------------------------------------------------- avaliador (ES5) ---
function lex(s) {
    var toks = [], i = 0;
    while (i < s.length) {
        var ch = s.charAt(i);
        if ((ch >= "0" && ch <= "9") || ch === ".") {
            var num = "";
            while (i < s.length && ((s.charAt(i) >= "0" && s.charAt(i) <= "9") || s.charAt(i) === ".")) {
                num += s.charAt(i);
                i++;
            }
            var v = parseFloat(num);
            if (isNaN(v)) throw "numero invalido";
            toks.push(v);
        } else if ("+-*/()".indexOf(ch) >= 0) {
            toks.push(ch);
            i++;
        } else if (ch === " ") {
            i++;
        } else throw "caractere: " + ch;
    }
    return toks;
}

function evaluate(s) {
    var toks = lex(s);
    var pos = 0;
    function peek() { return toks[pos]; }
    function next() { return toks[pos++]; }
    function parseExpr() {
        var v = parseTerm();
        while (peek() === "+" || peek() === "-") {
            var op = next();
            var r = parseTerm();
            v = (op === "+") ? v + r : v - r;
        }
        return v;
    }
    function parseTerm() {
        var v = parseFactor();
        while (peek() === "*" || peek() === "/") {
            var op = next();
            var r = parseFactor();
            v = (op === "*") ? v * r : v / r;
        }
        return v;
    }
    function parseFactor() {
        var t = peek();
        if (t === "-") { next(); return -parseFactor(); }   // unario
        if (t === "+") { next(); return parseFactor(); }
        if (t === "(") {
            next();
            var v = parseExpr();
            if (peek() !== ")") throw "fecha )";
            next();
            return v;
        }
        var n = next();
        if (typeof n !== "number") throw "expressao";
        return n;
    }
    var r = parseExpr();
    if (pos !== toks.length) throw "expressao incompleta";
    if (typeof r !== "number" || isNaN(r) || !isFinite(r)) throw "fora de faixa";
    return r;
}

function fmtResult(r) {
    var x = Math.round(r * 1e10) / 1e10;   // mata ruido de ponto flutuante
    var s = String(x);
    if (s.length > 14) s = x.toPrecision(10);
    return s;
}

function preview() {
    if (!expr) return null;
    try { return evaluate(expr); } catch (e) { return null; }
}

// ------------------------------------------------------------- regras de ---
// entrada: operador substitui operador; "." so um por numero; parenteses
// balanceados.
function lastCh() { return expr.length ? expr.charAt(expr.length - 1) : ""; }
function isOp(c) { return c === "+" || c === "-" || c === "*" || c === "/"; }
function openCount() {
    var n = 0;
    for (var i = 0; i < expr.length; i++) {
        if (expr.charAt(i) === "(") n++;
        else if (expr.charAt(i) === ")") n--;
    }
    return n;
}

function pressDigit(d) {
    if (expr.length >= MAXLEN) return;
    var last = lastCh();
    if (last === ")") expr += "*";   // 2(3+1) -> 2*(3+1)
    expr += d;
}

function pressDot() {
    if (expr.length >= MAXLEN) return;
    var last = lastCh();
    if (!(last >= "0" && last <= "9")) {
        if (last === "." ) return;
        if (last === ")") expr += "*";
        expr += "0.";
        return;
    }
    var seg = "";
    for (var i = expr.length - 1; i >= 0; i--) {
        var c = expr.charAt(i);
        if (!(((c >= "0" && c <= "9")) || c === ".")) break;
        seg = c + seg;
    }
    if (seg.indexOf(".") < 0) expr += ".";
}

function pressOp(op) {
    if (expr.length >= MAXLEN) return;
    var last = lastCh();
    if (!expr.length) {
        if (op === "-") expr = "-";  // unario na frente
        return;
    }
    if (last === "(") { if (op === "-") expr += op; return; }
    if (isOp(last)) { expr = expr.substring(0, expr.length - 1) + op; return; }
    expr += op;
}

function pressOpen() {
    if (expr.length >= MAXLEN) return;
    var last = lastCh();
    if (!expr.length || isOp(last) || last === "(") expr += "(";
    else expr += "*(";
}

function pressClose() {
    if (expr.length >= MAXLEN) return;
    if (openCount() > 0 && (expr.length && !isOp(lastCh()) && lastCh() !== "(")) expr += ")";
}

function pressBack() {
    if (expr.length) expr = expr.substring(0, expr.length - 1);
}

function pressClear() {
    expr = "";
    typed = "";
    result = null;
}

function pressEq() {
    if (!expr) return;
    try {
        var v = evaluate(expr);
        typed = expr;
        result = v;
        expr = fmtResult(v);          // encadeia: resultado vira expressao
    } catch (e) {
        showErr = System.millis();
    }
}

// ---------------------------------------------------------------- teclas ---
var M = 8, GAP = 6;
var BW = 51, BH = 30;
var GX = Math.floor((W - (4 * BW + 3 * GAP)) / 2);   // 9
var GY = 136;

// kind: num | op | fn (C, back, parens) | eq | sign
var KEYS = [
    { l: "C", k: "fn" },  { l: "<", k: "fn" }, { l: "(", k: "fn" }, { l: ")", k: "fn" },
    { l: "7", k: "num" }, { l: "8", k: "num" }, { l: "9", k: "num" }, { l: "/", k: "op" },
    { l: "4", k: "num" }, { l: "5", k: "num" }, { l: "6", k: "num" }, { l: "x", k: "op" },
    { l: "1", k: "num" }, { l: "2", k: "num" }, { l: "3", k: "num" }, { l: "-", k: "op" },
    { l: "0", k: "num" }, { l: ".", k: "num" }, { l: "=", k: "eq" }, { l: "+", k: "op" }
];

function keyRect(i) {
    var col = i % 4, row = Math.floor(i / 4);
    return { x: GX + col * (BW + GAP), y: GY + row * (BH + GAP), w: BW, h: BH };
}

function pretty(s) {
    var r = "";
    for (var i = 0; i < s.length; i++) {
        var c = s.charAt(i);
        if (c === "*") r += "x";
        else if (c === "/") r += "/";
        else r += c;
    }
    return r;
}

function keyColors(k) {
    if (k === "eq") return { bg: T.accent, fg: T.onAccent, bd: T.accent };
    if (k === "op") return { bg: T.accentD, fg: T.text, bd: T.stroke };
    if (k === "fn") return { bg: T.raised, fg: T.text, bd: T.stroke };
    return { bg: T.card, fg: T.text, bd: T.stroke };  // num / sign
}

function drawKey(i) {
    var r = keyRect(i);
    var c = keyColors(KEYS[i].k);
    var pressed = (i === flash);
    if (pressed) c = { bg: T.accent, fg: T.onAccent, bd: T.accent };
    System.fillRoundRect(r.x, r.y, r.w, r.h, 8, c.bg);
    System.drawRoundRect(r.x, r.y, r.w, r.h, 8, c.bd);
    var label = KEYS[i].l;
    var font = 2;
    System.setTextColor(c.fg, c.bg);
    System.drawString(label, r.x + (r.w - System.textWidth(label, font)) / 2, r.y + (r.h - 8 * font) / 2 + 1, font);
}

function drawGrid() {
    for (var i = 0; i < KEYS.length; i++) drawKey(i);
}

function drawDisplay() {
    System.fillRect(0, 43, W, 85, T.card);
    System.drawFastHLine(0, 43, W, T.stroke);
    // expressao (cauda, alinhada a direita): apos = mostra a que foi digitada
    var p = pretty(result !== null ? typed : expr);
    var shown = p;
    if (p.length > 26) shown = p.substring(p.length - 26);
    System.setTextColor(T.text, T.card);
    System.drawString(shown, W - 10 - System.textWidth(shown, 2), 52, 2);
    // linha de resultado
    if (showErr && System.millis() - showErr < 900) {
        System.setTextColor(T.err, T.card);
        System.drawString("Erro", W - 10 - System.textWidth("Erro", 2), 84, 2);
        return;
    }
    if (result !== null) {
        var rs = "= " + fmtResult(result);
        if (rs.length > 14) rs = rs.substring(0, 14);
        System.setTextColor(T.ok, T.card);
        System.drawString(rs, W - 10 - System.textWidth(rs, 2), 80, 2);
    } else {
        var pv = preview();
        if (pv !== null && expr) {
            var ps = "= " + fmtResult(pv);
            if (ps.length > 14) ps = ps.substring(0, 14);
            System.setTextColor(T.textDim, T.card);
            System.drawString(ps, W - 10 - System.textWidth(ps, 2), 84, 2);
        }
    }
}

function header() {
    System.fillRoundRect(0, 0, W, 40, 0, T.card);
    System.setTextColor(T.text, T.card);
    System.drawString("Calculadora", 12, 12, 2);
    System.fillRect(0, 40, W, 3, T.accent);
}

function drawAll() {
    System.fillScreen(T.bg);
    header();
    drawDisplay();
    drawGrid();
}

function waitRelease() {
    var guard = System.millis();
    while (System.getTouch().touched) {
        if (System.millis() - guard > 3000) break;
        System.delay(10);
    }
}

function apply(i) {
    var l = KEYS[i].l;
    if (l !== "=") result = null;   // qualquer tecla nova sai do modo resultado
    if (l >= "0" && l <= "9") pressDigit(l);
    else if (l === ".") pressDot();
    else if (l === "x") pressOp("*");
    else if (l === "+") pressOp("+");
    else if (l === "-") pressOp("-");
    else if (l === "/") pressOp("/");
    else if (l === "(") pressOpen();
    else if (l === ")") pressClose();
    else if (l === "<") pressBack();
    else if (l === "C") pressClear();
    else if (l === "=") pressEq();
}

// ------------------------------------------------------------------ main ---
drawAll();

while (true) {
    var t = System.getTouch();
    if (t.touched) {
        var hit = -1;
        for (var i = 0; i < KEYS.length; i++) {
            var r = keyRect(i);
            if (t.x >= r.x && t.x <= r.x + r.w && t.y >= r.y && t.y <= r.y + r.h) { hit = i; break; }
        }
        waitRelease();
        if (hit >= 0) {
            flash = hit;
            drawKey(hit);
            System.delay(90);
            flash = -1;
            apply(hit);
            if (KEYS[hit].l === "=") drawAll();
            else { drawDisplay(); drawKey(hit); }
            if (showErr && System.millis() - showErr >= 900) { showErr = 0; drawDisplay(); }
        }
    }
    if (showErr && System.millis() - showErr >= 900) {
        showErr = 0;
        drawDisplay();
    }
    System.delay(20);
}
