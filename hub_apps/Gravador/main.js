// Gravador de Voz — grava pelo microfone do aparelho (Mic.*, API 19),
// salva WAVs no appData e reproduz com System.playWav (API 13). Medidor
// de nivel em arco (fillArc, API 22) e chrome do toolkit UI. Requer
// permissao de microfone e uma placa com mic (relogio/cao): sem ele o app
// explica e vira um player das gravacoes existentes.

var T = System.theme();
var W = 240, H = 320;

var DATA = (FS.appData ? FS.appData() : "/local/");
var hasMic = (typeof Mic !== "undefined") && (typeof Mic.start === "function");
var hasWav = (typeof System.playWav === "function");

var hasTone = (typeof System.playTone === "function");
function tone(hz, ms) {
    if (hasTone) { try { System.playTone([[hz, ms]]); } catch (e) {} }
}

// ---------------------------------------------------------------- estado ---
var recMaxS = 6;             // duracao alvo (slider)
var recording = false;
var recStart = 0;
var recCount = 0;            // proximo indice de nome
var level = 0;               // 0..100 para o gauge
var note = "";
var noteUntil = 0;

function fmtDur(ms) {
    var s = Math.floor(ms / 1000);
    var d = Math.floor((ms % 1000) / 100);
    return s + "." + d + "s";
}

// lista de gravacoes: [{file, label, ms}]
var recs = [];

function baseName(p) {
    var i = String(p).lastIndexOf("/");
    return i < 0 ? String(p) : String(p).substring(i + 1);
}

// listDir devolve caminhos absolutos; aqui viram itens {label, right, file}
function scanRecs() {
    recs = [];
    var names = FS.listDir ? FS.listDir(DATA) : [];
    for (var i = 0; i < names.length; i++) {
        var n = baseName(names[i]);
        if (n.indexOf("voz-") !== 0 || n.indexOf(".wav") < 0) continue;
        var num = parseInt(n.substring(4), 10);
        if (isNaN(num)) continue;
        if (num >= recCount) recCount = num + 1;
        recs.push({ file: n, num: num });
    }
    recs.sort(function (a, b) { return a.num - b.num; });
    var out = [];
    for (var k = 0; k < recs.length; k++) {
        out.push({ label: "voz " + recs[k].num, right: "wav", file: recs[k].file });
    }
    return out;
}

function listItems() {
    var items = scanRecs();
    for (var i = 0; i < items.length; i++) {
        var sz = FS.getFileSize ? FS.getFileSize(DATA + items[i].file) : 0;
        if (sz > 0) items[i].right = Math.round(sz / 1024) + " KB";
    }
    return items;
}

function say(msg) {
    note = msg;
    noteUntil = System.millis() + 2600;
    UI.invalidate();
}

function startRec() {
    var ok = false;
    try { ok = Mic.start({ ms: recMaxS * 1000 }); } catch (e) { ok = false; }
    if (!ok) { say("não deu para gravar agora"); return; }
    recording = true;
    recStart = System.millis();
    tone(880, 40);
    UI.invalidate();
}

// encerra e salva; devolve true se gravou algo
function stopRec() {
    recording = false;
    var wav = null;
    try { wav = Mic.stop({ raw: true }); } catch (e) { wav = null; }
    var ms = System.millis() - recStart;
    UI.invalidate();
    if (!wav) { say("gravação vazia (muito curta?)"); return false; }
    var name = "voz-" + recCount + ".wav";
    var wrote = false;
    try { wrote = FS.writeFile(DATA + name, wav); } catch (e) { wrote = false; }
    if (!wrote) { say("sem espaço para salvar"); tone(220, 200); return false; }
    recCount++;
    say("salvo: " + name + " (" + fmtDur(ms) + ")");
    tone(660, 60);
    UI.invalidate();
    return true;
}

// ------------------------------------------------------------------ main ---
var items = listItems();
while (true) {
    UI.begin(T.bg);

    if (recording) {
        // nivel ao vivo (0..100) alimentando o arco
        var lv = -1;
        try { lv = Mic.level(); } catch (e) { lv = -1; }
        level = lv >= 0 ? Math.max(0, Math.min(100, lv)) : 0;
        var el = System.millis() - recStart;
        var pct = Math.min(100, Math.round(el / (recMaxS * 1000) * 100));

        UI.header("Gravando", { sub: fmtDur(el) });
        // gauge: arco do nivel + arco fina do tempo
        System.fillArc(W / 2, 130, 44, 54, -120, -120 + 240 * level / 100, T.accent);
        System.fillArc(W / 2, 130, 34, 40, -120, -120 + 240 * pct / 100, T.accentD);
        System.setTextDatum(5);           // MC (LovyanGFX: 4 e middle-left)
        System.setTextColor(T.text, T.bg);
        System.drawString(level + "%", W / 2, 126, 2);
        System.setTextColor(T.textDim, T.bg);
        System.drawString("nível do microfone", W / 2, 148, 1);
        System.setTextDatum(0);
        // expirou sozinho (fim do buffer)? encerra e salva
        var alive = false;
        try { alive = Mic.recording(); } catch (e) { alive = false; }
        if (!alive) { stopRec(); items = listItems(); }
        else if (UI.button("Parar e salvar", 40, 210, 160, 48, { style: "danger" })) {
            if (stopRec()) items = listItems();
        }
        UI.end();
        System.delay(30);
        continue;
    }

    UI.header("Gravador de voz", { sub: recs.length + " gravações" });

    if (!hasMic) {
        UI.badge("sem microfone", 12, 54, { color: T.err, textColor: T.onAccent });
        UI.text("esta placa não tem microfone (ou a permissão não foi dada).",
                W / 2, 92, { role: "caption", align: "center", color: T.textDim, w: W - 32, lines: 2 });
    } else {
        UI.text("duração máxima", 16, 58, { role: "caption", color: T.textDim });
        recMaxS = UI.slider(16, 70, 150, recMaxS, { min: 2, max: 10, step: 1 });
        UI.text(recMaxS + "s", 178, 74, { role: "title" });
        if (UI.button("Gravar", 16, 108, 208, 52)) startRec();
        UI.text("32 KB por segundo de áudio", W / 2, 170,
                { role: "caption", align: "center", color: T.textDim });
    }

    // nota (resultado da ultima acao)
    if (note && System.millis() < noteUntil) {
        UI.text(note, W / 2, 186, { role: "caption", align: "center", color: T.accent, id: 7, w: W - 24 });
    }

    var sel = UI.list("recs", 16, 196, W - 32, 96, items, { rowH: 30 });
    if (sel >= 0 && hasWav) {
        var it = items[sel];
        say("tocando " + it.label);
        var played = false;
        try { played = System.playWav(DATA + it.file); } catch (e) { played = false; }
        if (!played) say("falha ao tocar " + it.file);
    }

    if (items.length && UI.button("Apagar ultima", 16, 296, 208, 20, { style: "ghost" })) {
        var last = items[items.length - 1];
        if (UI.confirm("Apagar", "Remover " + last.label + " (" + last.right + ")?", { danger: true })) {
            try { FS.deleteFile(DATA + last.file); } catch (e) {}
            items = listItems();
            say("apagado");
        }
    }

    UI.end();
    System.delay(20);
}
