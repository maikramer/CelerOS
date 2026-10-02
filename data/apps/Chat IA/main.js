// CelerOS Chat IA — assistente DeepSeek (API 18: objeto AI)
// ES5 puro (Duktape). Teclado do OS ancorado no rodape; cada OK envia a
// mensagem para a IA e a resposta chega pelo callback enquanto o app
// bombeia o loop (o framework faz o HTTPS em task propria). A chave fica
// no aparelho (/local/deepseek_key.txt) — o app nunca a ve.

var T = System.theme();
var W = 240, H = 320;
var CHW = 6;                     // fonte 1 (GLCD): 6 px por coluna
var COLS = Math.floor(W / CHW) - 4;  // margem de 4 colunas
var LINE_H = 10;

var hasAI = (typeof AI !== "undefined") && (typeof AI.chat === "function");
var VER = System.getOSVersion();

// Personalidade curta: resposta curta gasta menos token, latencia e tela
var SYSTEM_PROMPT = "Você é o assistente do CelerOS, um sistema para " +
    "dispositivos ESP32 com tela pequena. Responda em português, de forma " +
    "curta e direta, sem usar emojis.";
var CTX_MSGS = 12;               // quantas mensagens enviar de contexto
var MAX_MSGS = 20;               // quantas guardar no historico

var msgs = [];                   // conversa: {r: "user"|"assistant", s: texto}
var lines = [];                  // transcript renderizavel: {s, c}
var MAXLINES = 150;
var busy = false;
var histFile = (typeof FS.appData === "function" ? FS.appData() : "");
histFile += "historico.json";

var kbTop = H;
var useKeypad = false;
var redrawAll = true;
var inputDirty = true;
var lastBlink = 0;
var cursorOn = true;
var hasChips = (typeof System.topbarButtons === "function");

// Rolagem do transcript: 0 = ao vivo (segue o fim); >0 = linhas de recuo.
// O teclado consome o touch dele via keypadPoll; acima de kbTop o touch
// fica livre para o app (getTouch e keypadPoll leem o controlador de
// forma independente).
var scrollBack = 0;
var dragY0 = -1;         // y do inicio do arrasto (-1 = sem arrasto)
var dragScroll0 = 0;

// ------------------------------------------------------------- historico ---
(function loadHist() {
    if (!histFile || histFile === "historico.json") return;
    var raw = FS.readTextFile(histFile);
    if (!raw) return;
    try {
        var arr = JSON.parse(raw);
        if (arr && arr.length) msgs = arr;
    } catch (e) { msgs = []; }
})();

function saveHist() {
    if (!histFile || histFile === "historico.json") return;
    try {
        FS.writeTextFile(histFile, JSON.stringify(msgs.slice(-MAX_MSGS)));
    } catch (e) { /* historico e melhor esforco: nunca derruba o chat */ }
}
// ---------------------------------------------------------------- saida ----
// As fonts so desenham Latin-1: emoji/simbolos da IA viram nada
function fit(s) {
    return String(s === null || s === undefined ? "" : s)
        .replace(/[^\u0000-\u00FF\n]/g, "");
}

function pushLine(s, c) {
    lines.push({ s: s, c: c });
    if (lines.length > MAXLINES) lines.shift();
}

// Quebra por palavras na coluna; palavra maior que a coluna e cortada seca
function wrapCols(s, cols) {
    var out = [];
    var paras = s.split("\n");
    for (var p = 0; p < paras.length; p++) {
        var words = paras[p].split(" ");
        var cur = "";
        for (var i = 0; i < words.length; i++) {
            var w = words[i];
            while (w.length > cols) {           // palavra gigante
                if (cur.length) { out.push(cur); cur = ""; }
                out.push(w.substring(0, cols));
                w = w.substring(cols);
            }
            var trial = cur.length ? cur + " " + w : w;
            if (trial.length > cols && cur.length) {
                out.push(cur);
                cur = w;
            } else {
                cur = trial;
            }
        }
        out.push(cur);
    }
    return out;
}

function appendMsgLines(role, text) {
    var prefix = role === "user" ? "você: " : "IA: ";
    var col = role === "user" ? T.accent : T.text;
    var wrapped = wrapCols(prefix + text, COLS);
    for (var i = 0; i < wrapped.length; i++) {
        pushLine(i === 0 ? wrapped[i] : "  " + wrapped[i], col);
    }
}

function pushMsg(role, text) {
    msgs.push({ r: role, s: text });
    if (msgs.length > MAX_MSGS) msgs.shift();
    appendMsgLines(role, text);
    saveHist();
}

// A conversa anterior aparece no boot: sem isso o transcript abre vazio
// mesmo com historico (e o arrasto nao tem o que rever)
(function renderHist() {
    for (var i = 0; i < msgs.length; i++) appendMsgLines(msgs[i].r, msgs[i].s);
})();

function pushWrapped(s, c) {
    var ws = wrapCols(s, COLS);
    for (var i = 0; i < ws.length; i++) pushLine(ws[i], c);
}

function pushHint(s) { pushWrapped(s, T.textDim); }
function pushErr(s) { pushWrapped(s, T.err); }

// ------------------------------------------------------------------ envio --
function send(text) {
    if (busy) { pushHint("(a IA ainda esta respondendo)"); return; }
    if (!hasAI) { pushErr("sem AI no firmware (requer API >= 18)"); return; }
    if (!AI.configured()) { pushErr("sem chave: rode tools/push_deepseek_key.py"); return; }
    if (!Net.isConnected()) { pushErr("sem WiFi"); return; }

    scrollBack = 0;  // nova pergunta: volta ao vivo
    pushMsg("user", text);
    var payload = [{ role: "system", content: SYSTEM_PROMPT }];
    var start = Math.max(0, msgs.length - CTX_MSGS);
    for (var i = start; i < msgs.length; i++) {
        payload.push({ role: msgs[i].r, content: msgs[i].s });
    }
    busy = true;
    redrawAll = true;
    var started = AI.chat({ messages: payload }, function (r) {
        busy = false;
        scrollBack = 0;  // resposta nova: segue o fim
        redrawAll = true;
        try { if (typeof System.beep === "function") System.beep(1320, 60); } catch (e) {}
        if (r && r.ok && r.content) {
            pushMsg("assistant", fit(r.content));
            if (r.usage && r.usage.total_tokens) {
                pushHint("(" + r.usage.total_tokens + " tokens)");
            }
        } else if (r && r.ok) {
            pushErr("IA: resposta vazia");
        } else {
            pushErr("IA: erro " + (r && r.error ? r.error : "HTTP " + (r ? r.status : 0)));
            if (r && r.detail) pushHint("detalhe: " + fit(r.detail));
        }
    });
    if (!started) {
        busy = false;
        pushErr("IA: ocupado, tente de novo");
    }
}

// -------------------------------------------------------------- desenho ----
function drawOut() {
    var bottom = kbTop - 16 - (busy ? 12 : 0);
    if (bottom <= 0) return;
    var vis = Math.max(1, Math.floor(bottom / LINE_H));
    var maxBack = Math.max(0, lines.length - vis);
    if (scrollBack > maxBack) scrollBack = maxBack;
    var end = lines.length - scrollBack;
    var start = Math.max(0, end - vis);
    System.fillRect(0, 0, W, bottom, T.bg);
    var y = 0;
    for (var i = start; i < end; i++) {
        System.setTextColor(lines[i].c, T.bg);
        System.drawString(lines[i].s, 4, y, 1);
        y += LINE_H;
    }
    if (!lines.length) {
        System.setTextColor(T.textDim, T.bg);
        System.drawString("Pergunte algo ao assistente...", 4, 4, 1);
    }
    if (maxBack > 0) {  // barra de posicao na borda direita
        System.fillRect(W - 3, 0, 3, bottom, T.stroke);
        var th = Math.max(8, Math.floor(bottom * vis / lines.length));
        var ty = Math.floor((bottom - th) * (maxBack - scrollBack) / maxBack);
        System.fillRect(W - 3, ty, 3, th, T.accent);
    }
}

function drawStatus() {
    if (!busy) return;
    var y = kbTop - 28;
    System.fillRect(0, y, W, 12, T.bg);
    var dots = "";
    var t = Math.floor(System.millis() / 400) % 4;
    for (var i = 0; i < t; i++) dots += ".";
    System.setTextColor(T.accent, T.bg);
    System.drawString("IA pensando" + dots, 4, y + 2, 1);
}

function drawInput() {
    var y = kbTop - 14;
    System.fillRect(0, y, W, 14, T.bg);
    var s = "você: " + System.keypadText();
    if (s.length > COLS - 1) s = s.substring(s.length - (COLS - 1));  // cauda
    System.setTextColor(T.accent, T.bg);
    System.drawString(s, 4, y + 3, 1);
    if (cursorOn) {
        var cx = 4 + System.textWidth(s, 1);
        System.fillRect(cx + 1, y + 2, 5, 9, T.text);
    }
}

function drawAll() {
    drawOut();
    drawStatus();
    drawInput();
    if (typeof __harness !== "undefined") {
        __harness.chatScroll = scrollBack;
        if (scrollBack > (__harness.chatScrollMax || 0)) {
            __harness.chatScrollMax = scrollBack;
        }
    }
}

// ------------------------------------------------------------------ main ---
useKeypad = System.keypadOpen({ field: false, maxLen: 256 });
if (useKeypad) kbTop = System.keypadRect().y;

pushLine("Chat IA " + VER, T.accent);
if (hasAI) {
    pushHint("DeepSeek; historico local; arraste p/ rever");
} else {
    pushErr("requer firmware com API >= 18");
}

// chips da faixa (API 6): Limpar zera a conversa; Cancelar esquece a
// requisicao em curso
if (useKeypad && hasChips) System.topbarButtons(["Limpar", "Cancelar"]);

while (true) {
    if (useKeypad) {
        var ev = System.keypadPoll();
        if (ev) {
            if (ev.type === "enter") {
                var line = ev.text;
                cursorOn = true;
                if (line) send(line);
                redrawAll = true;
            } else if (ev.type === "change") {
                inputDirty = true;
                cursorOn = true;
            } else if (ev.type === "cancel") {
                redrawAll = true;
                System.keypadOpen({ field: false, maxLen: 256 });
                kbTop = System.keypadRect().y;
            }
        }

        var chip = System.topbarPop ? System.topbarPop() : null;
        if (chip === "Limpar") {
            msgs = [];
            lines = [];
            try { FS.deleteFile(histFile); } catch (e) {}
            pushHint("conversa apagada");
            redrawAll = true;
        } else if (chip === "Cancelar") {
            if (busy && hasAI && AI.cancel()) {
                busy = false;
                pushHint("cancelado");
            }
            redrawAll = true;
        } else if (chip !== null) {
            redrawAll = true;
        }
    }

    // Arrasto acima do teclado: rever o transcript (passo de 1 linha; novo
    // toque comeca um arrasto novo). Toque no teclado (>= kbTop) e do teclado.
    var t = System.getTouch();
    if (t && t.touched && t.y < kbTop - 4) {
        if (dragY0 < 0) {
            dragY0 = t.y;
            dragScroll0 = scrollBack;
        }
        var visD = Math.max(1, Math.floor((kbTop - 28) / LINE_H));
        var maxBackD = Math.max(0, lines.length - visD);
        var want = dragScroll0 + Math.floor((dragY0 - t.y) / LINE_H);
        if (want < 0) want = 0;
        if (want > maxBackD) want = maxBackD;
        if (want !== scrollBack) {
            scrollBack = want;
            redrawAll = true;
        }
    } else {
        dragY0 = -1;
    }

    if (redrawAll) {
        drawAll();
        redrawAll = false;
        inputDirty = false;
        lastBlink = System.millis();
    } else if (inputDirty) {
        drawInput();
        inputDirty = false;
        lastBlink = System.millis();
    } else if (busy && System.millis() - lastBlink > 400) {
        drawStatus();  // anima os pontos enquanto a IA responde
        lastBlink = System.millis();
    } else if (!busy && System.millis() - lastBlink > 500) {
        cursorOn = !cursorOn;
        drawInput();
        lastBlink = System.millis();
    }

    System.delay(20);
}
