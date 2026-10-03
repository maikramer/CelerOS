// CelerOS Qwen — assistente de voz (API 19: Mic.* + provider openrouter do AI)
// ES5 puro (Duktape). Fale no microfone e leia a resposta: segurar o botao
// grava (Mic.start/stop devolve o WAV em base64), o pedido vai como content
// part input_audio e a resposta chega em texto pelo callback do AI.chat
// (HTTPS em task propria). A chave fica no aparelho (/local/openrouter_key.txt)
// — o app nunca a ve. topbar: false — o chrome e daqui (o cao nao tem touch
// para o X da faixa e cada face precisa do vidro inteiro).
//
// Duas caras:
//  - placas com touch: transcript rolavel + fileira de botoes + botao redondo
//    de segurar-para-falar + teclado ancorado; sem microfone vira chat de
//    texto puro (mesmo servidor).
//  - cao robotico (spotpear-dog): OLED 128x64 sem touch — o canvas virtual
//    240x320 escala x0.53/y0.20 e texto comum fica ilegivel, entao esta
//    versao desenha com fonte propria 4x6 em pixels FISICOS (helper P()),
//    como o Dog Face. Entrada: pad capacitivo (System.touchPad) — segurar
//    fala, tocar rola a resposta, 3 toques rapidos saem.

var T = System.theme();
var INFO = System.getInfo();
var W = 240, H = 320;
var IS_DOG = (INFO.board === "spotpear-dog");
var HAS_MIC = (typeof Mic !== "undefined" && typeof Mic.start === "function");
var hasAI = (typeof AI !== "undefined" && typeof AI.chat === "function");
var VER = System.getOSVersion();

var PROV = "openrouter";
var SYSTEM_PROMPT = "Você é o Qwen, assistente de voz do CelerOS, sistema " +
    "para dispositivos ESP32 com tela pequena. As perguntas podem chegar " +
    "como áudio: ouça e responda por texto em português, de forma curta e " +
    "direta, sem usar emojis.";
var CTX_MSGS = 12;               // mensagens de contexto enviadas
var MAX_MSGS = 20;               // mensagens guardadas no historico

var msgs = [];                   // conversa: {r: "user"|"assistant", s: texto}
var busy = false;                // requisicao em curso
var histFile = (typeof FS.appData === "function" ? FS.appData() : "");
histFile += "historico.json";

// ------------------------------------------------------------ historico ---
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

// Fonts do touch cobrem Latin-1: emoji/simbolos da IA viram nada
function fit(s) {
    return String(s === null || s === undefined ? "" : s)
        .replace(/[^\u0000-\u00FF\n]/g, "");
}

// ------------------------------------------------------------------ envio --
// Payload: system + contexto de texto; a pergunta atual pode ser texto ou o
// content part de audio (a voz nao entra no historico — vira "(voz)")
function buildPayload(audioB64) {
    var payload = [{ role: "system", content: SYSTEM_PROMPT }];
    var end = msgs.length;
    if (audioB64) end -= 1;  // a "(voz)" recem empilhada sai do contexto
    var start = Math.max(0, end - CTX_MSGS);
    for (var i = start; i < end; i++) {
        payload.push({ role: msgs[i].r, content: msgs[i].s });
    }
    if (audioB64) {
        payload.push({ role: "user", content: [
            { type: "input_audio", input_audio: { data: audioB64, format: "wav" } }
        ]});
    }
    return payload;
}

function cue(freq, ms) {
    try { if (typeof System.beep === "function") System.beep(freq, ms); } catch (e) {}
}

// Envia a pergunta empilhada em msgs (texto ou voz). answerSink(r) recebe o
// resultado no present — cada interface desenha do seu jeito.
var answerSink = null;

// 502/504/429 do gateway sao transientes: uma retentativa silenciosa evita
// o erro na cara do dono (o modelo omni responde em ~5-15 s, mas o gateway
// da API as vezes sobrecarrega — bancada 2026-10-02)
function request(audioB64, isRetry) {
    if (busy) return false;
    if (!hasAI) { answerSink({ ok: false, error: "sem AI no firmware (requer API >= 18)" }); return true; }
    if (!AI.configured(PROV)) {
        answerSink({ ok: false, error: "sem chave (tools/push_ai_key.py " + PROV + ")" });
        return true;
    }
    if (!Net.isConnected()) { answerSink({ ok: false, error: "sem WiFi" }); return true; }
    busy = true;
    var opts = {
        provider: PROV,
        messages: buildPayload(audioB64),
        max_tokens: 300,               // teto curto: resposta rapida, longe do timeout do gateway
        reasoning: { effort: "low" }   // omni e raciocinador: low corta latencia
    };
    var started = AI.chat(opts,
        function (r) {
            busy = false;
            if (!isRetry && r && !r.ok &&
                (r.status === 502 || r.status === 504 || r.status === 429)) {
                if (answerSink) answerSink({ ok: false, retrying: true });
                setTimeout(function () { request(audioB64, true); }, 2000);
                return;
            }
            cue(1320, 60);
            if (r && r.ok && r.content) {
                msgs.push({ r: "assistant", s: fit(r.content) });
                if (msgs.length > MAX_MSGS) msgs.shift();
                saveHist();
            }
            if (answerSink) answerSink(r || { ok: false, error: "sem resposta" });
        });
    if (!started) {
        busy = false;
        // slot ocupado: quase sempre uma requisicao orfa (app fechado no
        // meio) ou o retry da pergunta anterior ainda no ar — nao e erro do
        // dono, e "espere a resposta chegar"
        if (!isRetry) answerSink({ ok: false, error: "ainda processando a pergunta anterior..." });
    }
    return true;
}

// Onde a pergunta aparece (transcript no touch; o cao nao ecoa texto)
var questionSink = null;

function sendText(text) {
    if (!text) return;
    msgs.push({ r: "user", s: fit(text) });
    if (msgs.length > MAX_MSGS) msgs.shift();
    saveHist();
    if (questionSink) questionSink(text);
    request(null);
}

function sendRecording(b64) {
    // o stop do Mic devolve o base64 do WAV pronto para o input_audio
    if (!b64) { answerSink({ ok: false, error: "não capturou áudio" }); return; }
    msgs.push({ r: "user", s: "(voz)" });
    if (msgs.length > MAX_MSGS) msgs.shift();
    saveHist();
    request(b64);
}

// =============================================================================
// INTERFACE TOUCH (relogio, SmartDisplay, CYD): transcript + botao de voz
// =============================================================================
if (!IS_DOG) {
var CHW = 6;                     // fonte 1 (GLCD): 6 px por coluna
var COLS = Math.floor(W / CHW) - 4;
var LINE_H = 10;
var lines = [];                  // transcript renderizavel: {s, c}
var MAXLINES = 150;

var REC_MS = 8000;               // teto da captura no touch
var MIN_MS = 700;                // abaixo disso descarta (toque perdido)
var MIC_CY = H - 44;
var MIC_R = 30;

// Chrome proprio (topbar:false): 3 botoes na primeira faixa. No relogio
// redondo os cantos sao zona morta — fileira centrada com folga nas bordas.
var BTN_Y = 6, BTN_H = 20;
var BTNS = [
    { x: 14, w: 66 },    // Teclado / Voz
    { x: 86, w: 68 },    // Limpar / Cancelar
    { x: 160, w: 66 }    // Sair
];
var TOP = BTN_Y + BTN_H + 6;    // transcript comeca abaixo dos botoes

var kbOpen = false;
var kbTop = H;
var redrawAll = true;
var inputDirty = true;
var lastBlink = 0;
var cursorOn = true;
var rec = false;                 // gravando (botao segurado)
var recStart = 0;
var wasBtnTouch = false;

var scrollBack = 0;              // 0 = ao vivo; >0 = linhas de recuo
var dragY0 = -1;
var dragScroll0 = 0;

function pushLine(s, c) {
    lines.push({ s: s, c: c });
    if (lines.length > MAXLINES) lines.shift();
}

function wrapCols(s, cols) {
    var out = [];
    var paras = s.split("\n");
    for (var p = 0; p < paras.length; p++) {
        var words = paras[p].split(" ");
        var cur = "";
        for (var i = 0; i < words.length; i++) {
            var w = words[i];
            while (w.length > cols) {
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

function pushWrapped(s, c) {
    var ws = wrapCols(s, COLS);
    for (var i = 0; i < ws.length; i++) pushLine(ws[i], c);
}

function appendMsgLines(role, text) {
    var prefix = role === "user" ? "você: " : "Q: ";
    var col = role === "user" ? T.accent : T.text;
    var wrapped = wrapCols(prefix + text, COLS);
    for (var i = 0; i < wrapped.length; i++) {
        pushLine(i === 0 ? wrapped[i] : "  " + wrapped[i], col);
    }
}

(function renderHist() {
    for (var i = 0; i < msgs.length; i++) appendMsgLines(msgs[i].r, msgs[i].s);
})();

// resultado da IA: entra no transcript (o loop redesenha no proximo giro)
questionSink = function (text) { appendMsgLines("user", text); };
answerSink = function (r) {
    if (r && r.retrying) { pushWrapped("repetindo...", T.textDim); redrawAll = true; return; }
    if (r && r.ok && r.content) {
        appendMsgLines("assistant", r.content);
        if (r.usage && r.usage.total_tokens) pushWrapped("(" + r.usage.total_tokens + " tokens)", T.textDim);
    } else if (r && r.ok) {
        pushWrapped("Q: resposta vazia", T.err);
    } else {
        pushWrapped("erro: " + (r && r.error ? r.error : "HTTP " + (r ? r.status : 0)), T.err);
        if (r && r.detail) pushWrapped("detalhe: " + fit(r.detail), T.textDim);
    }
    scrollBack = 0;
    redrawAll = true;
};

function transcriptBottom() {
    return (kbOpen ? kbTop - 16 : H - 92) - (busy ? 12 : 0);
}

function btnLabel(i) {
    if (i === 0) return (kbOpen && HAS_MIC) ? "Voz" : "Teclado";
    if (i === 1) return busy ? "Cancelar" : "Limpar";
    return "Sair";
}

function drawChrome() {
    System.fillRect(0, 0, W, TOP, T.bg);
    System.setTextColor(T.accent, T.bg);
    System.fillRoundRect(BTNS[0].x, BTN_Y, BTNS[0].w, BTN_H, 5, T.stroke);
    System.fillRoundRect(BTNS[1].x, BTN_Y, BTNS[1].w, BTN_H, 5, T.stroke);
    System.fillRoundRect(BTNS[2].x, BTN_Y, BTNS[2].w, BTN_H, 5, T.stroke);
    for (var i = 0; i < 3; i++) {
        var s = btnLabel(i);
        System.drawString(s, BTNS[i].x + Math.floor((BTNS[i].w - System.textWidth(s, 1)) / 2),
                          BTN_Y + 6, 1);
    }
}

function drawOut() {
    var bottom = transcriptBottom();
    if (bottom <= TOP) return;
    var vis = Math.max(1, Math.floor((bottom - TOP) / LINE_H));
    var maxBack = Math.max(0, lines.length - vis);
    if (scrollBack > maxBack) scrollBack = maxBack;
    var end = lines.length - scrollBack;
    var start = Math.max(0, end - vis);
    System.fillRect(0, TOP, W, bottom - TOP, T.bg);
    var y = TOP;
    for (var i = start; i < end; i++) {
        System.setTextColor(lines[i].c, T.bg);
        System.drawString(lines[i].s, 4, y, 1);
        y += LINE_H;
    }
    if (!lines.length) {
        System.setTextColor(T.textDim, T.bg);
        var hint = HAS_MIC ? "Segure o botão e fale com o Qwen" : "Pergunte algo ao Qwen...";
        System.drawString(hint, 4, TOP + 4, 1);
    }
    if (maxBack > 0) {
        System.fillRect(W - 3, TOP, 3, bottom - TOP, T.stroke);
        var th = Math.max(8, Math.floor((bottom - TOP) * vis / lines.length));
        var ty = TOP + Math.floor((bottom - TOP - th) * (maxBack - scrollBack) / maxBack);
        System.fillRect(W - 3, ty, 3, th, T.accent);
    }
}

function drawStatus() {
    if (!busy) return;
    var y = transcriptBottom() + 2;
    System.fillRect(0, y, W, 12, T.bg);
    var dots = "";
    var t = Math.floor(System.millis() / 400) % 4;
    for (var i = 0; i < t; i++) dots += ".";
    System.setTextColor(T.accent, T.bg);
    System.drawString("Qwen pensando" + dots, 4, y + 2, 1);
}

// Botao de voz: circulo que enche pelo tempo + nucleo pulsando pelo nivel
function drawMic() {
    if (kbOpen || !HAS_MIC) return;
    System.fillRect(0, H - 92, W, 92, T.bg);
    if (rec) {
        var el = System.millis() - recStart;
        var frac = el / REC_MS;
        if (frac > 1) frac = 1;
        System.fillRect(20, H - 88, 200, 4, T.stroke);
        System.fillRect(20, H - 88, Math.round(200 * frac), 4, T.accent);
        System.fillCircle(120, MIC_CY, MIC_R, T.accent);
        var lvl = Mic.level();
        if (lvl < 0) lvl = 0;
        var rr = 4 + Math.round(20 * (lvl / 100));
        if (rr > 24) rr = 24;
        System.fillCircle(120, MIC_CY, rr, T.bg);
        var lab = (el / 1000).toFixed(1) + "s";
        System.setTextColor(T.bg, T.accent);
        System.drawString(lab, 120 - Math.floor(System.textWidth(lab, 1) / 2), MIC_CY - 4, 1);
        System.setTextColor(T.textDim, T.bg);
        System.drawString("solte para enviar", 120 - Math.floor(System.textWidth("solte para enviar", 1) / 2), H - 16, 1);
    } else {
        System.drawCircle(120, MIC_CY, MIC_R, T.accent);
        System.fillCircle(120, MIC_CY, 10, T.accent);
        System.fillCircle(112, MIC_CY - 12, 3, T.accent);
        System.fillCircle(120, MIC_CY - 15, 3, T.accent);
        System.fillCircle(128, MIC_CY - 12, 3, T.accent);
        System.setTextColor(T.textDim, T.bg);
        System.drawString("segure e fale", 120 - Math.floor(System.textWidth("segure e fale", 1) / 2), H - 16, 1);
    }
}

function drawInput() {
    if (!kbOpen) return;
    var y = kbTop - 14;
    System.fillRect(0, y, W, 14, T.bg);
    var s = "você: " + System.keypadText();
    if (s.length > COLS - 1) s = s.substring(s.length - (COLS - 1));
    System.setTextColor(T.accent, T.bg);
    System.drawString(s, 4, y + 3, 1);
    if (cursorOn) {
        var cx = 4 + System.textWidth(s, 1);
        System.fillRect(cx + 1, y + 2, 5, 9, T.text);
    }
}

function drawAll() {
    drawChrome();
    drawOut();
    drawStatus();
    drawMic();
    drawInput();
    if (typeof __harness !== "undefined") {
        __harness.qwenRec = rec;
        if (scrollBack > (__harness.chatScrollMax || 0)) __harness.chatScrollMax = scrollBack;
    }
}

// -------------------------------------------------------------- gravacao ---
function startRec() {
    if (rec || busy || !HAS_MIC) return;
    if (!Mic.start({ ms: REC_MS })) { pushWrapped("microfone ocupado ou sem RAM", T.err); redrawAll = true; return; }
    rec = true;
    recStart = System.millis();
    redrawAll = true;
}

// fim do dedo/teto de tempo: envia (ou descarta se curtinho demais)
function finishRec() {
    if (!rec) return;
    rec = false;
    var el = System.millis() - recStart;
    if (el < MIN_MS) {
        Mic.stop();  // descarta o pinguinho
        pushWrapped("(muito curto, tente de novo)", T.textDim);
        redrawAll = true;
        return;
    }
    appendMsgLines("user", "(voz)");  // a pergunta aparece no transcript
    sendRecording(Mic.stop());
    scrollBack = 0;
    redrawAll = true;
}

function cancelRec() {
    if (!rec) return;
    rec = false;
    Mic.stop();
    pushWrapped("(cancelado)", T.textDim);
    redrawAll = true;
}

function openKeypad() {
    if (rec) cancelRec();
    kbOpen = System.keypadOpen({ field: false, maxLen: 256 });
    if (kbOpen) kbTop = System.keypadRect().y;
    redrawAll = true;
}

function closeKepad() {
    if (typeof System.keypadClose === "function") System.keypadClose();
    kbOpen = false;
    kbTop = H;
    redrawAll = true;
}

function btnTap(i) {
    if (i === 0) {
        if (kbOpen && HAS_MIC) closeKepad();
        else openKeypad();
    } else if (i === 1) {
        if (busy && hasAI && AI.cancel()) {
            busy = false;
            pushWrapped("cancelado", T.textDim);
        } else if (!busy) {
            if (rec) cancelRec();
            msgs = [];
            lines = [];
            try { FS.deleteFile(histFile); } catch (e) {}
            pushWrapped("conversa apagada", T.textDim);
        }
        redrawAll = true;
    } else {
        if (rec) cancelRec();
        System.exitApp();
    }
}

// ------------------------------------------------------------------ main ---
pushLine("Qwen " + VER, T.accent);
if (!hasAI) pushWrapped("requer firmware com API >= 18", T.err);
else if (!AI.configured(PROV)) pushWrapped("sem chave: rode tools/push_ai_key.py " + PROV, T.err);
if (!HAS_MIC) pushWrapped("sem microfone aqui: use o teclado", T.textDim);
else pushWrapped("fale segurando o botao; arraste p/ rever", T.textDim);

// Sem microfone o teclado abre direto (chat de texto puro)
if (!HAS_MIC && typeof System.keypadOpen === "function") openKeypad();

while (true) {
    if (kbOpen) {
        var ev = System.keypadPoll();
        if (ev) {
            if (ev.type === "enter") {
                cursorOn = true;
                if (ev.text) sendText(ev.text);
                redrawAll = true;
            } else if (ev.type === "change") {
                inputDirty = true;
                cursorOn = true;
            } else if (ev.type === "cancel") {
                // voltar ao modo voz (se houver mic) em vez de reabrir o teclado
                if (HAS_MIC) closeKepad();
                else { System.keypadOpen({ field: false, maxLen: 256 }); kbTop = System.keypadRect().y; }
                redrawAll = true;
            }
        }
    }

    var t = System.getTouch();

    // botoes do chrome: toque (borda de descida) na faixa de cima
    var inRow = t && t.touched && t.y >= BTN_Y - 2 && t.y <= BTN_Y + BTN_H + 2;
    if (inRow && !wasBtnTouch) {
        for (var b = 0; b < 3; b++) {
            if (t.x >= BTNS[b].x - 4 && t.x <= BTNS[b].x + BTNS[b].w + 4) { btnTap(b); break; }
        }
    }
    wasBtnTouch = !!inRow;

    // botao de voz: segurar dentro do circulo grava; soltar envia; arrastar
    // para fora cancela
    if (HAS_MIC && !kbOpen) {
        var inBtn = t && t.touched &&
            (t.x - 120) * (t.x - 120) + (t.y - MIC_CY) * (t.y - MIC_CY) <=
            (MIC_R + 6) * (MIC_R + 6);
        if (!rec && inBtn && !busy) {
            startRec();
        } else if (rec) {
            var far = t && t.touched &&
                (t.x - 120) * (t.x - 120) + (t.y - MIC_CY) * (t.y - MIC_CY) >
                (MIC_R + 26) * (MIC_R + 26);
            if (far) cancelRec();
            else if (!t || !t.touched) finishRec();
            else redrawAll = true;  // anima tempo/nivel
        }
    }

    // arrasto no transcript: rever a conversa
    if (!rec && t && t.touched && t.y > BTN_Y + BTN_H + 4 && t.y < transcriptBottom() - 4) {
        if (dragY0 < 0) {
            dragY0 = t.y;
            dragScroll0 = scrollBack;
        }
        var visD = Math.max(1, Math.floor((transcriptBottom() - TOP - 8) / LINE_H));
        var maxBackD = Math.max(0, lines.length - visD);
        var want = dragScroll0 + Math.floor((dragY0 - t.y) / LINE_H);
        if (want < 0) want = 0;
        if (want > maxBackD) want = maxBackD;
        if (want !== scrollBack) {
            scrollBack = want;
            redrawAll = true;
        }
    } else if (!rec) {
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
        drawStatus();
        lastBlink = System.millis();
    } else if (kbOpen && !busy && System.millis() - lastBlink > 500) {
        cursorOn = !cursorOn;
        drawInput();
        lastBlink = System.millis();
    }

    System.delay(20);
}
}  // fim da interface touch

// =============================================================================
// INTERFACE DO CAO ROBOTICO (OLED 128x64, pad capacitivo, sem touch)
// =============================================================================
if (IS_DOG) {
var PW = 128, PH = 64;  // vidro fisico do OLED

function PX(x) { return Math.round(x * 240 / PW); }
function PY(y) { return Math.round(y * 320 / PH); }
function PW_(w) { return Math.round(w * 240 / PW); }
function PH_(h) { return Math.round(h * 320 / PH); }

var REC_MS_D = 6000;
var MIN_MS_D = 800;
var HOLD_MS = 400;      // segurar este tempo comeca a gravar
var TAP_EXIT = 3;       // toques rapidos para sair (sem touch/botao no cao)
var CHARS = 25;         // 25 colunas de 5 px (4 de glifo + 1 de folga)
var ROWS = 7;           // 7 linhas de 8 px no corpo

// Fonte 4x6 propria: no OLED o drawString do canvas virtual (escala y0.20)
// e ilegivel — cada glifo e desenhado em pixels fisicos do vidro. O painel
// e 1-bit: toda cor vira tinta, barras usam moldura+preenchimento. Cada
// glifo = 6 linhas de 4 bits (nibble, bit mais alto = coluna da esquerda).
var FONT = {
    " ":"000000",
    "A":"69F999","B":"E9E99E","C":"788887","D":"E9999E","E":"F8E88F","F":"F8E888",
    "G":"78B997","H":"99F999","I":"E4444E","J":"311196","K":"9ACA99","L":"88888F",
    "M":"9FF999","N":"9DFB99","O":"699996","P":"E9E888","Q":"6999A5","R":"E9EA99",
    "S":"78611E","T":"E44444","U":"999996","V":"999966","W":"999FF9","X":"996699",
    "Y":"AA4444","Z":"F1688F",
    "0":"69BD96","1":"4C444E","2":"E1688F","3":"E6111E","4":"99F111","5":"F8E11E",
    "6":"78E996","7":"F12444","8":"696996","9":"69971E",
    ".":"000006",",":"000064","?":"E16404","!":"444404",":":"0C0C00","-":"000F00",
    "+":"04E400","/":"124488","'":"440000","(":"248842",")":"421124","=":"00F0F0",
    "%":"924890"
};

// acentos viram a base (a fonte 4x6 nao tem glifo acentuado)
var FOLD = {
    "Á":"A","À":"A","Â":"A","Ã":"A","Ä":"A","É":"E","Ê":"E","Í":"I",
    "Ó":"O","Ô":"O","Õ":"O","Ö":"O","Ú":"U","Ü":"U","Ç":"C"
};

function dogFold(s) {
    var out = "";
    s = String(s).toUpperCase();
    for (var i = 0; i < s.length; i++) {
        var ch = s.charAt(i);
        if (FOLD[ch]) ch = FOLD[ch];
        if (FONT[ch] !== undefined) out += ch;
    }
    return out;
}

function dogChar(x, y, ch, color) {
    var g = FONT[ch];
    if (!g) return;
    for (var r = 0; r < 6; r++) {
        var bits = parseInt(g.charAt(r), 16);
        if (!bits) continue;
        for (var c = 0; c < 4; c++) {
            if (bits & (8 >> c)) System.fillRect(PX(x + c), PY(y + r), PW_(1), PH_(1), color);
        }
    }
}

function dogText(x, y, s, color) {
    s = dogFold(s);
    var cx = x;
    for (var i = 0; i < s.length; i++) {
        if (cx > PW - 4) return;
        dogChar(cx, y, s.charAt(i), color);
        cx += 5;
    }
}

function dogWrap(s) {
    var out = [];
    var words = dogFold(s).split(" ");
    var cur = "";
    for (var i = 0; i < words.length; i++) {
        var w = words[i];
        while (w.length > CHARS) {
            if (cur.length) { out.push(cur); cur = ""; }
            out.push(w.substring(0, CHARS));
            w = w.substring(CHARS);
        }
        var trial = cur.length ? cur + " " + w : w;
        if (trial.length > CHARS && cur.length) {
            out.push(cur);
            cur = w;
        } else {
            cur = trial;
        }
    }
    if (cur.length) out.push(cur);
    return out;
}

var stD = "idle";       // idle | rec | busy
var recStartD = 0;
var padDown = false;
var padDownAt = 0;
var holdFired = false;
var answerLines = [];   // resposta atual quebrada em linhas de ate 25 chars
var answerPage = 0;
var taps = [];
var hintIdx = 0;
var lastHint = 0;
var lastDraw = 0;
var dirtyD = true;
var HINTS = ["HOLD=FALA TOQUE=ROLA", "3 TOQUES SAEM", "SOLTE O PAD P/ ENVIAR"];

function pages() {
    return Math.max(1, Math.ceil(answerLines.length / ROWS));
}

function drawHeader() {
    var right;
    if (stD === "rec") {
        right = "REC " + ((System.millis() - recStartD) / 1000).toFixed(1) + "S";
    } else if (stD === "busy") {
        right = "ENVIANDO";
        var d = Math.floor(System.millis() / 400) % 4;
        for (var i = 0; i < d; i++) right += ".";
    } else {
        right = "PRONTO";
    }
    System.fillRect(0, 0, PW_(PW), PH_(8), T.text);
    dogText(1, 1, "QWEN", T.bg);
    dogText(PW - 2 - right.length * 5, 1, right, T.bg);
}

function drawBody() {
    System.fillRect(0, PY(9), PW_(PW), PH_(47), T.bg);
    if (stD === "rec") {
        dogText(2, 12, "OUVINDO VOCE", T.text);
        var lvl = Mic.level();
        if (lvl < 0) lvl = 0;
        // nivel: moldura + preenchimento (painel 1-bit nao tem cor de fundo)
        System.drawRect(PX(2), PY(21), PW_(124), PH_(8), T.text);
        System.fillRect(PX(4), PY(23), PW_(Math.round(120 * lvl / 100)), PH_(4), T.accent);
        // tempo restante
        var frac = (System.millis() - recStartD) / REC_MS_D;
        if (frac > 1) frac = 1;
        System.drawRect(PX(2), PY(33), PW_(124), PH_(6), T.text);
        System.fillRect(PX(4), PY(35), PW_(Math.round(120 * frac)), PH_(2), T.accent);
        dogText(2, 44, "SOLTE O PAD P/", T.textDim);
        dogText(2, 52, "ENVIAR", T.textDim);
    } else if (stD === "busy") {
        dogText(2, 20, "PENSANDO...", T.accent);
        dogText(2, 32, "A RESPOSTA VAI SAIR", T.textDim);
        dogText(2, 40, "AQUI EM TEXTO", T.textDim);
    } else {
        var p = Math.min(answerPage, pages() - 1);
        var y = 11;
        for (var i = p * ROWS; i < (p + 1) * ROWS && i < answerLines.length; i++) {
            dogText(2, y, answerLines[i], T.text);
            y += 8;
        }
        if (!answerLines.length) {
            dogText(2, 16, "QWEN PRONTO", T.accent);
            dogText(2, 30, "SEGURE O PAD NAS", T.textDim);
            dogText(2, 38, "COSTAS E FALE", T.textDim);
        }
    }
}

function drawFooter() {
    System.fillRect(0, PY(57), PW_(PW), PH_(7), T.bg);
    dogText(2, 58, HINTS[hintIdx % HINTS.length], T.textDim);
    if (stD === "idle" && answerLines.length && pages() > 1) {
        var pg = ((answerPage % pages()) + 1) + "/" + pages();
        dogText(PW - 2 - pg.length * 5, 58, pg, T.accent);
    }
}

function drawAllD() {
    drawHeader();
    drawBody();
    drawFooter();
    // o texto do cao e desenhado pixel a pixel (fillRect): o harness nao ve
    // nada no log — o estado vai por aqui para os testes
    if (typeof __harness !== "undefined") {
        __harness.qwenDog = { st: stD, lines: answerLines.length, page: answerPage };
    }
}

function registerTaps() {
    var now = System.millis();
    taps.push(now);
    if (taps.length > TAP_EXIT) taps.shift();
    if (taps.length === TAP_EXIT && now - taps[0] < 900) {
        cue(500, 40);
        System.exitApp();
    }
}

answerSink = function (r) {
    if (r && r.retrying) { stD = "busy"; dirtyD = true; return; }  // repete em silencio
    stD = "idle";
    if (r && r.ok && r.content) {
        answerLines = dogWrap(fit(r.content));
        answerPage = 0;
    } else {
        var msg = "ERRO: " + (r && r.error ? r.error : "HTTP " + (r ? r.status : 0));
        if (r && r.detail) msg += " " + fit(r.detail).substring(0, 40);
        answerLines = dogWrap(msg);
        answerPage = 0;
    }
    dirtyD = true;
};

function startRecD() {
    if (stD !== "idle" || busy) return;
    if (!Mic.start({ ms: REC_MS_D })) {
        answerLines = dogWrap("MIC OCUPADO OU SEM RAM");
        answerPage = 0;
        dirtyD = true;
        return;
    }
    stD = "rec";
    recStartD = System.millis();
    dirtyD = true;
}

function finishRecD() {
    var el = System.millis() - recStartD;
    stD = "idle";
    if (el < MIN_MS_D) {
        Mic.stop();  // curtinho: descarta
        dirtyD = true;
        return;
    }
    var b64 = Mic.stop();
    if (b64) {
        sendRecording(b64);   // empilha "(voz)" e pede
        stD = busy ? "busy" : "idle";
    }
    dirtyD = true;
}

// a ultima resposta sobrevive ao reboot (historico)
if (msgs.length) {
    for (var mi = msgs.length - 1; mi >= 0; mi--) {
        if (msgs[mi].r === "assistant" && msgs[mi].s) {
            answerLines = dogWrap(msgs[mi].s);
            break;
        }
    }
}

while (true) {
    var pad = (typeof System.touchPad === "function") ? System.touchPad() : 0;
    var now = System.millis();

    if (pad && !padDown) {         // borda de descida
        padDown = true;
        padDownAt = now;
        holdFired = false;
    } else if (!pad && padDown) {  // borda de subida
        padDown = false;
        if (stD === "rec") {
            finishRecD();
        } else if (!holdFired) {
            registerTaps();
            if (answerLines.length) {  // toque rola a resposta
                answerPage++;
                dirtyD = true;
            }
        }
    } else if (padDown && !holdFired && stD === "idle" && !busy &&
               now - padDownAt >= HOLD_MS) {
        holdFired = true;
        startRecD();
    }

    // teto de tempo alcancado: encerra e envia sozinho
    if (stD === "rec" && !Mic.recording()) finishRecD();

    // dicas do rodape giram a cada 3 s
    if (now - lastHint > 3000) {
        lastHint = now;
        hintIdx++;
        dirtyD = true;
    }
    // rec/busy animam (tempo, nivel, pontos)
    if (stD !== "idle" && now - lastDraw > 150) dirtyD = true;

    if (dirtyD) {
        drawAllD();
        dirtyD = false;
        lastDraw = now;
    }
    System.delay(20);
}
}  // fim da interface do cao
