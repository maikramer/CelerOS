// CelerOS Settings — app de sistema (W8). Porte JS do SettingsScreens.cpp:
// PIN de entrada, menu, Wi-Fi, Aplicativos, Hora/Fuso, Seguranca (PIN),
// Tela (brilho), Atualizacao OTA, Sobre e Reset. Canvas virtual 240x320,
// tema do OS (System.theme). X no canto sup. direito sai (exit nativo).

var T = System.theme();
var PIN_FILE = "/local/settings_pin.txt";
var INSTALL_SD = "/local/config_install_sd.txt";

// ---- helpers de UI (padrao dos apps de sistema) ---------------------------

function ctext(s, cx, cy, f, col, bg) {
    System.setTextColor(col, bg);
    // centro vertical pela altura real da fonte (API 3+: System.fontHeight)
    var fh = System.fontHeight ? System.fontHeight(f) : (f >= 2 ? 16 : 10);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), cy - (fh >> 1), f);
}
function hit(t, x, y, w, h) {
    return t.x >= x && t.x <= x + w && t.y >= y && t.y <= y + h;
}
function header(title) {
    System.fillRoundRect(0, 0, 240, 40, 0, T.card);
    System.setTextColor(T.text, T.card);
    System.drawString(title, 12, 12, 2);
    System.fillRect(0, 40, 240, 3, T.accent);
}
function trimStr(s) {
    return String(s).replace(/^\s+|\s+$/g, "");
}
function basename(p) {
    var i = p.lastIndexOf("/");
    return i >= 0 ? p.substring(i + 1) : p;
}
function fmtBytes(b) {
    if (!b || b < 0) b = 0;
    if (b < 1024) return b + " B";
    if (b < 1048576) return Math.round(b / 1024) + " KB";
    return Math.round((b / 1048576) * 10) / 10 + " MB";
}
function fmtUptime(ms) {
    if (!ms || ms < 0) ms = 0;
    var s = Math.floor(ms / 1000);
    var d = Math.floor(s / 86400); s -= d * 86400;
    var h = Math.floor(s / 3600); s -= h * 3600;
    var m = Math.floor(s / 60);
    if (d > 0) return d + "d " + h + "h";
    if (h > 0) return h + "h " + m + "m";
    return m + "m";
}
function trunc(s, maxW, f) {
    if (System.textWidth(s, f) <= maxW) return s;
    while (s.length > 1 && System.textWidth(s + "...", f) > maxW) {
        s = s.substring(0, s.length - 1);
    }
    return s + "...";
}
function wrapLines(s, maxW, f) {
    var out = [];
    if (!s) return out;
    var words = String(s).split(" ");
    var line = "";
    for (var i = 0; i < words.length; i++) {
        var w = words[i];
        var cand = line === "" ? w : line + " " + w;
        if (line !== "" && System.textWidth(cand, f) > maxW) {
            out.push(line);
            line = w;
        } else {
            line = cand;
        }
    }
    if (line !== "") out.push(line);
    return out;
}
// Tela-cheia de aviso rapido (substitui o toast do Kui).
function note(title, msg, ms) {
    System.fillScreen(T.bg);
    header(title);
    var ls = wrapLines(msg, 208, 1);
    var y = 150 - (ls.length * 8);
    for (var i = 0; i < ls.length; i++, y += 16) {
        ctext(ls[i], 120, y, 1, T.text, T.bg);
    }
    System.delay(ms || 900);
}

// ---- maquina de estados ----------------------------------------------------

var tela = "menu";
var items = [];        // linhas da tela atual: {l, v, a, i, cur, d}
var scroll = 0;
var listBottom = 276;  // limite inferior da lista (apps reserva espaco p/ info)
var ROW_H = 38, PITCH = 44, TOP = 52;

var appsArr = [];      // apps encontrados em /local/apps e /sd/apps
var selApp = 0;
var otaState = "idle"; // idle|checking|nowifi|failed|uptodate|avail|installing|done
var otaInfo = null;
var otaErrTitle = "";
var otaErr = "";
var otaLines = [];     // bloco rolavel (changelog + guia): {t, h, y}
var otaLinesH = 0;
var blLvl = 60;
var modal = null;      // {t, b, y, f} — confirmacao em 2 botoes
var pressIdx = -1;     // linha sob o dedo (feedback visual antes do tap)
var itemsSig = "";     // assinatura das linhas desenhadas (evita redesenho inutil)

var TZS = [
    ["UTC-12 Baker Is", "UTC12"], ["UTC-11 Midway", "UTC11"],
    ["UTC-10 Hawaii", "UTC10"], ["UTC-9 Alaska", "UTC9"],
    ["UTC-8 PST", "UTC8"], ["UTC-7 MST", "UTC7"],
    ["UTC-6 CST", "UTC6"], ["UTC-5 EST", "UTC5"],
    ["UTC-4 AST", "UTC4"], ["UTC-3 BRT", "UTC3"],
    ["UTC-2", "UTC2"], ["UTC-1 AZOT", "UTC1"],
    ["UTC+0 GMT", "UTC0"], ["UTC+1 CET", "UTC-1"],
    ["UTC+2 EET", "UTC-2"], ["UTC+3 MSK", "UTC-3"],
    ["UTC+4 GST", "UTC-4"], ["UTC+5 PKT", "UTC-5"],
    ["UTC+5:30 IST", "UTC-5:30"], ["UTC+6 BST", "UTC-6"],
    ["UTC+7 ICT", "UTC-7"], ["UTC+8 CST/AWST", "UTC-8"],
    ["UTC+9 JST", "UTC-9"], ["UTC+10 AEST", "UTC-10"],
    ["UTC+11 AEDT", "UTC-11"], ["UTC+12 NZST", "UTC-12"]
];

// ---- lista generica (linhas h=38, pitch 44, rolagem por arraste) -----------

function listMax() {
    var n = items.length * PITCH - (listBottom - TOP);
    return n > 0 ? n : 0;
}
function otaRegionH() {
    // janela 98..202: abaixo ficam o aviso/botao (ou o hint sem firmware)
    return 104;
}
function scrollMax() {
    if (tela === "update" && otaState === "avail") {
        var n = otaLinesH - otaRegionH();
        return n > 0 ? n : 0;
    }
    return listMax();
}
function clampScroll() {
    if (scroll < 0) scroll = 0;
    var m = scrollMax();
    if (scroll > m) scroll = m;
}
function hasScroll() {
    return scrollMax() > 0;
}
function setItems(a) {
    items = a;
    scroll = 0;
}
function drawRows() {
    var first = Math.floor(scroll / PITCH) - 1;
    if (first < 0) first = 0;
    var y = TOP + first * PITCH - scroll;
    // linhas parciais nas bordas: recortadas na janela da lista (sem o
    // recorte elas invadiam o cabecalho ao rolar)
    var clip = typeof System.setClip === "function";
    if (clip) System.setClip(0, TOP - 4, 240, listBottom - TOP + 4);
    for (var i = first; i < items.length; i++, y += PITCH) {
        if (y >= listBottom) break;
        if (!clip && y + ROW_H > listBottom) break;
        if (y < TOP - ROW_H) continue;
        if (!clip && y < TOP) continue;
        var it = items[i];
        var bgc = (i === pressIdx && !it.i && !it.d) ? T.raised : T.card;
        System.fillRoundRect(8, y, 224, ROW_H, 8, bgc);
        System.drawRoundRect(8, y, 224, ROW_H, 8, i === pressIdx ? T.accent : T.stroke);
        if (it.cur) System.fillRect(8, y + 6, 4, ROW_H - 12, T.accent);
        var labCol = (it.i || it.d) ? T.textDim : (it.cur ? T.accent : T.text);
        System.setTextColor(labCol, bgc);
        System.drawString(trunc(it.l, it.v ? 128 : 200, 2), 20, y + 11, 2);
        if (it.v) {
            System.setTextColor(it.cur ? T.accent : T.textDim, bgc);
            System.drawString(it.v, 220 - System.textWidth(it.v, 1), y + 14, 1);
        }
    }
    if (clip) System.clearClip();
}

function rowsVisible() {
    return tela === "menu" || tela === "tz" || tela === "security" || tela === "about" ||
        tela === "apps" || tela === "time";
}
function rowAt(t) {
    if (t.y < TOP || t.y >= listBottom) return -1;
    var idx = Math.floor((t.y - TOP + scroll) / PITCH);
    if (idx < 0 || idx >= items.length) return -1;
    var ry = TOP + idx * PITCH - scroll;
    if (t.y < ry || t.y > ry + ROW_H) return -1;  // no vao entre linhas
    return idx;
}

// ---- PIN (md5 compativel com settings_pin.txt legado) ----------------------

function readPin() {
    var s = FS.readTextFile(PIN_FILE);
    return s ? trimStr(s) : "";
}
function pinOk(p) {
    var st = readPin();
    return st.length === 32 && System.md5(p) === st;
}
function validPin(p) {
    return /^[0-9]{4,6}$/.test(p);
}
function flowNewPin() {
    var p1 = System.prompt("Novo PIN (4-6 digitos)", "");
    if (!validPin(p1)) {
        note("Seguranca", "PIN invalido: use 4 a 6 digitos", 1200);
        return false;
    }
    var p2 = System.prompt("Confirmar PIN", "");
    if (p1 !== p2) {
        note("Seguranca", "PINs nao conferem", 1200);
        return false;
    }
    FS.writeTextFile(PIN_FILE, System.md5(p1));
    note("Seguranca", "PIN salvo", 1000);
    return true;
}

// ---- navegacao -------------------------------------------------------------

function titleNow() {
    if (tela === "appdetail" && appsArr[selApp]) return trunc(appsArr[selApp].name, 216, 2);
    if (tela === "manual") return "Ajuste manual";
    if (tela === "time") return "Hora e fuso";
    if (tela === "tz") return "Fuso horario";
    if (tela === "security") return "Seguranca";
    if (tela === "display") return "Tela";
    if (tela === "update") return "Atualizacao";
    if (tela === "about") return "Sobre";
    if (tela === "wifi") return "Wi-Fi";
    if (tela === "apps") return "Aplicativos";
    if (tela === "reset") return "Reset";
    return "Settings";
}
function rebuildItems() {
    listBottom = 276;
    if (tela === "menu") setItems(buildMenu());
    else if (tela === "apps") { listBottom = 248; setItems(buildApps()); }
    else if (tela === "time") setItems(buildTime());
    else if (tela === "tz") setItems(buildTz());
    else if (tela === "security") setItems(buildSec());
    else if (tela === "about") setItems(buildAbout());
    else setItems([]);
}
function go(s) {
    tela = s;
    scroll = 0;
    if (s === "display") {
        blLvl = System.getBrightness();
        if (blLvl < 5) blLvl = 5;
    } else if (s === "update") {
        otaState = "idle";
        otaInfo = null;
        otaErr = "";
        otaErrTitle = "";
        otaLines = [];
        otaLinesH = 0;
    }
    rebuildItems();
    drawAll();
}
function footerBack() {
    if (tela === "appdetail") go("apps");
    else if (tela === "tz") go("time");
    else if (tela === "menu") System.exitApp();
    else go("menu");
}

// ---- confirmacao modal (2 botoes) ------------------------------------------

function askConfirm(title, body, yesLabel, fn) {
    modal = { t: title, b: body, y: yesLabel, f: fn };
    drawAll();
}
function drawModal() {
    System.fillRoundRect(16, 92, 208, 156, 10, T.raised);
    System.drawRoundRect(16, 92, 208, 156, 10, T.stroke);
    System.setTextColor(T.text, T.raised);
    System.drawString(trunc(modal.t, 188, 2), 26, 104, 2);
    var ls = wrapLines(modal.b, 184, 1);
    var y = 130;
    for (var i = 0; i < ls.length && i < 5; i++, y += 14) {
        System.setTextColor(T.textDim, T.raised);
        System.drawString(ls[i], 26, y, 1);
    }
    System.fillRoundRect(28, 208, 84, 28, 8, T.card);
    System.drawRoundRect(28, 208, 84, 28, 8, T.stroke);
    ctext("Cancelar", 70, 222, 1, T.text, T.card);
    System.fillRoundRect(128, 208, 84, 28, 8, T.err);
    ctext(modal.y, 170, 222, 1, T.text, T.err);
}

// ---- desenho das telas -----------------------------------------------------

function drawAll() {
    System.fillScreen(T.bg);
    header(titleNow());
    drawContent();
    var doneOta = (tela === "update" && otaState === "done");
    if (!doneOta) {
        System.fillRoundRect(8, 282, 84, 30, 8, T.raised);
        System.drawRoundRect(8, 282, 84, 30, 8, T.stroke);
        ctext(tela === "menu" ? "< Sair" : "< Voltar", 50, 297, 2, T.text, T.raised);
        if (tela === "update" && (otaState === "nowifi" || otaState === "failed" ||
            otaState === "uptodate" || otaState === "avail")) {
            System.fillRoundRect(140, 282, 92, 30, 8, T.raised);
            System.drawRoundRect(140, 282, 92, 30, 8, T.stroke);
            ctext("Verificar", 186, 297, 1, T.text, T.raised);
        }
    }
    if (modal) drawModal();
}

function drawContent() {
    if (tela === "menu" || tela === "tz" || tela === "security" || tela === "about") {
        drawRows();
    } else if (tela === "wifi") {
        drawWifi();
    } else if (tela === "apps") {
        drawRows();
        System.setTextColor(T.textDim, T.bg);
        System.drawString("Interno: " + spaceStr("/local"), 12, 252, 1);
        System.drawString("SD: " + spaceStr("/sd"), 12, 266, 1);
    } else if (tela === "appdetail") {
        drawAppDetail();
    } else if (tela === "time") {
        drawRows();
    } else if (tela === "display") {
        drawDisplay();
    } else if (tela === "update") {
        drawUpdate();
    } else if (tela === "reset") {
        drawReset();
    }
}

// ---- menu principal --------------------------------------------------------

function buildMenu() {
    var w = System.wifiStatus();
    var a = [];
    a.push({ l: "Wi-Fi", v: w.connected ? "ON" : "OFF", a: "wifi" });
    a.push({ l: "Aplicativos", a: "apps" });
    a.push({ l: "Hora e fuso", a: "time" });
    a.push({ l: "Seguranca", v: FS.exists(PIN_FILE) ? "PIN" : "--", a: "sec" });
    a.push({
        l: "Tela",
        v: System.backlightSupported() ? System.getBrightness() + "%" : "--",
        a: "display"
    });
    a.push({ l: "Atualizacao", a: "ota" });
    a.push({ l: "Sobre", v: "v" + System.getOSVersion(), a: "about" });
    a.push({ l: "Reset", a: "reset" });
    return a;
}

// ---- Wi-Fi -----------------------------------------------------------------

function drawWifi() {
    var w = System.wifiStatus();
    System.fillRoundRect(8, 52, 224, 76, 10, T.card);
    System.drawRoundRect(8, 52, 224, 76, 10, T.stroke);
    System.setTextColor(T.textDim, T.card);
    System.drawString("Estado", 20, 60, 1);
    System.setTextColor(w.connected ? T.ok : T.warn, T.card);
    System.drawString(w.connected ? "Conectado" : "Desconectado",
        220 - System.textWidth(w.connected ? "Conectado" : "Desconectado", 1), 60, 1);
    System.setTextColor(T.textDim, T.card);
    System.drawString("IP", 20, 80, 2);
    System.setTextColor(T.accent, T.card);
    System.drawString(w.connected && w.ip ? w.ip : "--", 52, 80, 2);
    System.setTextColor(T.textDim, T.card);
    System.drawString("Redes salvas", 20, 104, 1);
    System.setTextColor(T.text, T.card);
    System.drawString(w.savedNetworks ? "sim" : "nenhuma",
        220 - System.textWidth(w.savedNetworks ? "sim" : "nenhuma", 1), 104, 1);

    System.fillRoundRect(48, 150, 144, 36, 8, T.accent);
    ctext("Configurar", 120, 168, 2, T.onAccent, T.accent);

    System.setTextColor(T.textDim, T.bg);
    System.drawString("Abre o assistente nativo de conexao", 12, 208, 1);
    System.drawString("(scan + senha + portal). Este app sai", 12, 222, 1);
    System.drawString("para dar lugar ao assistente.", 12, 236, 1);
}

// ---- Aplicativos -----------------------------------------------------------

function installSd() {
    return FS.exists(INSTALL_SD);
}
function spaceStr(drive) {
    var tot = FS.getTotalSpace(drive);
    if (!tot || tot <= 0) return "ausente";
    return fmtBytes(FS.getFreeSpace(drive)) + " livre / " + fmtBytes(tot);
}
function collectApps(dir, out, sd) {
    var list = FS.listDir(dir);
    if (!list) return;
    for (var i = 0; i < list.length; i++) {
        var p = list[i];
        if (!FS.isDirectory(p)) continue;
        var jp = p + "/app.json";
        if (!FS.exists(jp)) continue;
        var meta = {};
        try {
            meta = JSON.parse(FS.readTextFile(jp)) || {};
        } catch (e) {
            meta = {};
        }
        out.push({
            dir: p,
            sd: sd,
            name: meta.name || basename(p),
            ver: meta.version || "",
            sys: meta.system === true,
            api: meta.api || 0,
            desc: meta.description || ""
        });
    }
}
function scanApps() {
    var out = [];
    collectApps("/local/apps", out, false);
    collectApps("/sd/apps", out, true);
    out.sort(function (a, b) {
        var x = a.name.toLowerCase(), y = b.name.toLowerCase();
        return x < y ? -1 : (x > y ? 1 : 0);
    });
    return out;
}
function buildApps() {
    appsArr = scanApps();
    var a = [];
    a.push({ l: "Instalar em", v: installSd() ? "SD" : "Interno", a: "dest" });
    for (var i = 0; i < appsArr.length; i++) {
        var ap = appsArr[i];
        a.push({ l: ap.name, v: (ap.sd ? "[SD] " : "") + (ap.ver ? "v" + ap.ver : ""), a: "app:" + i });
    }
    if (appsArr.length === 0) a.push({ l: "Nenhum app instalado", i: 1 });
    return a;
}
function infoLine(l, v, y) {
    System.setTextColor(T.textDim, T.card);
    System.drawString(l, 20, y, 1);
    System.setTextColor(T.text, T.card);
    System.drawString(String(v), 220 - System.textWidth(String(v), 1), y, 1);
}
function drawAppDetail() {
    var ap = appsArr[selApp];
    if (!ap) return;
    System.fillRoundRect(8, 52, 224, 76, 10, T.card);
    System.drawRoundRect(8, 52, 224, 76, 10, T.stroke);
    infoLine("Versao", ap.ver ? "v" + ap.ver : "--", 60);
    infoLine("API", ap.api ? ap.api : "--", 76);
    infoLine("Tipo", ap.sys ? "app do sistema" : "comum", 92);
    infoLine("Origem", ap.sd ? "cartao SD" : "interno", 108);

    System.setTextColor(T.textDim, T.bg);
    System.drawString("Descricao", 12, 140, 1);
    var ls = wrapLines(ap.desc, 216, 1);
    var y = 156;
    for (var i = 0; i < ls.length && y < 244; i++, y += 14) {
        System.setTextColor(T.textDim, T.bg);
        System.drawString(ls[i], 12, y, 1);
    }

    System.fillRoundRect(48, 246, 144, 32, 8, T.err);
    ctext("Desinstalar", 120, 262, 2, T.text, T.err);
}
function askUninstall(i) {
    selApp = i;
    var ap = appsArr[i];
    var body = "Remover " + ap.name + "?";
    if (ap.sys) {
        askConfirm("App do sistema", "Este app faz parte do sistema. Remover app do sistema?",
            "Continuar", function () {
                askConfirm("Desinstalar?", body, "Remover", function () { doUninstall(); });
            });
    } else {
        askConfirm("Desinstalar?", body, "Remover", function () { doUninstall(); });
    }
}
function doUninstall() {
    var ap = appsArr[selApp];
    note("Aplicativos", "Removendo " + ap.name + "...", 500);
    if (FS.isDirectory(ap.dir)) FS.removeDirectory(ap.dir);
    else FS.deleteFile(ap.dir);
    System.rescanApps();
    note("Aplicativos", "App removido", 900);
    go("apps");
}

// ---- Hora e fuso -----------------------------------------------------------

function buildTime() {
    var a = [];
    a.push({ l: "Agora", v: System.getTime(), i: 1 });
    a.push({ l: "Data", v: System.getDate(), i: 1 });
    a.push({ l: "NTP (internet)", v: System.getNtpEnabled() ? "ON" : "OFF", a: "ntp" });
    a.push({ l: "Fuso", v: System.getTimezone(), a: "tz" });
    a.push({ l: "Formato", v: System.get24hFormat() ? "24h" : "12h", a: "fmt" });
    a.push({ l: "Ajuste manual", a: "manual", d: System.getNtpEnabled() ? 1 : 0 });
    return a;
}
function buildTz() {
    var cur = System.getTimezone();
    var a = [];
    for (var i = 0; i < TZS.length; i++) {
        a.push({ l: TZS[i][0], a: "tz:" + i, cur: TZS[i][1] === cur });
    }
    return a;
}
function nowHM() {
    var s = System.getTime();
    var m = s.match(/(\d{1,2}):(\d{2})/);
    var h = m ? parseInt(m[1], 10) : 12;
    var mi = m ? parseInt(m[2], 10) : 0;
    if (s.indexOf("PM") >= 0 && h < 12) h += 12;
    if (s.indexOf("AM") >= 0 && h === 12) h = 0;
    return [h, mi];
}
function drawManual(step, vals) {
    System.fillScreen(T.bg);
    header("Ajuste manual");
    var names = ["Ano", "Mes", "Dia", "Hora", "Minuto"];
    var y = 64;
    for (var i = 0; i < 5; i++, y += 28) {
        var col = i < step ? T.text : (i === step ? T.accent : T.textDim);
        System.setTextColor(col, T.bg);
        System.drawString(names[i] + ": " + vals[i], 40, y, 2);
    }
    ctext("campo " + (step + 1) + " de 5", 120, 226, 1, T.textDim, T.bg);
}
function askNum(label, cur, lo, hi) {
    while (true) {
        var init = "" + cur;
        if (cur < 10) init = "0" + cur;
        var s = System.prompt(label + " (" + lo + " a " + hi + ")", init);
        if (s === null || s === "") return null;
        s = trimStr(s);
        if (/^[0-9]+$/.test(s)) {
            var n = parseInt(s, 10);
            if (n >= lo && n <= hi) return n;
        }
        note(label, "Valor invalido", 700);
    }
}
function runManual() {
    var hm = nowHM();
    var vals = [System.getYear(), System.getMonth(), System.getDay(), hm[0], hm[1]];
    var labels = ["Ano", "Mes", "Dia", "Hora", "Minuto"];
    var lo = [2000, 1, 1, 0, 0];
    var hi = [2100, 12, 31, 23, 59];
    tela = "manual";
    for (var i = 0; i < 5; i++) {
        drawManual(i, vals);
        var n = askNum(labels[i], vals[i], lo[i], hi[i]);
        if (n === null) { go("time"); return; }
        vals[i] = n;
    }
    drawManual(5, vals);
    System.setManualTime(vals[0], vals[1], vals[2], vals[3], vals[4]);
    note("Ajuste manual", "Hora ajustada", 900);
    go("time");
}

// ---- Seguranca -------------------------------------------------------------

function buildSec() {
    var has = FS.exists(PIN_FILE);
    var a = [];
    a.push({ l: "PIN do Settings", v: has ? "ativo" : "desativado", i: 1 });
    if (has) {
        a.push({ l: "Trocar PIN", a: "chg" });
        a.push({ l: "Remover PIN", a: "rm" });
    } else {
        a.push({ l: "Definir PIN", a: "set" });
    }
    return a;
}

// ---- Tela (brilho) ---------------------------------------------------------

function drawDisplay() {
    if (!System.backlightSupported()) {
        ctext("Backlight fixo", 120, 120, 2, T.warn, T.bg);
        ctext("nesta placa.", 120, 146, 2, T.textDim, T.bg);
        ctext("Controle de brilho indisponivel", 120, 190, 1, T.textDim, T.bg);
        ctext("neste hardware.", 120, 204, 1, T.textDim, T.bg);
        return;
    }
    ctext("Brilho", 120, 84, 2, T.text, T.bg);
    ctext("arraste o trilho", 120, 104, 1, T.textDim, T.bg);
    ctext(blLvl + "%", 120, 134, 2, T.accent, T.bg);

    System.fillRoundRect(24, 160, 192, 12, 6, T.card);
    System.drawRoundRect(24, 160, 192, 12, 6, T.stroke);
    var fw = Math.round(192 * blLvl / 100);
    if (fw > 4) System.fillRoundRect(24, 160, fw, 12, 6, T.accent);
    System.fillCircle(24 + fw, 166, 7, T.text);
}

// ---- Atualizacao (OTA) -----------------------------------------------------

function buildOtaLines() {
    otaLines = [];
    var y = 0;
    function addHdr(s) {
        otaLines.push({ t: s, h: 1, y: y });
        y += 18;
    }
    function addBody(s) {
        var ls = wrapLines(s, 216, 1);
        for (var i = 0; i < ls.length; i++) {
            otaLines.push({ t: ls[i], h: 0, y: y });
            y += 14;
        }
        y += 4;
    }
    if (otaInfo.changelog) { addHdr("Novidades:"); addBody(otaInfo.changelog); }
    if (otaInfo.guide) { addHdr("Como instalar:"); addBody(otaInfo.guide); }
    otaLinesH = y;
}
function drawUpdate() {
    if (otaState === "idle") {
        ctext("Versao atual", 120, 92, 1, T.textDim, T.bg);
        ctext("CelerOS v" + System.getOSVersion(), 120, 116, 2, T.text, T.bg);
        System.fillRoundRect(48, 150, 144, 36, 8, T.accent);
        ctext("Verificar", 120, 168, 2, T.onAccent, T.accent);
        ctext("requer Wi-Fi conectado", 120, 205, 1, T.textDim, T.bg);
    } else if (otaState === "checking") {
        ctext("Verificando atualizacoes...", 120, 150, 1, T.textDim, T.bg);
    } else if (otaState === "nowifi") {
        ctext("Sem conexao Wi-Fi.", 120, 116, 2, T.err, T.bg);
        ctext("Conecte em Wi-Fi primeiro", 120, 146, 1, T.textDim, T.bg);
        ctext("e toque em Verificar.", 120, 160, 1, T.textDim, T.bg);
    } else if (otaState === "failed") {
        ctext(trunc(otaErrTitle, 216, 2), 120, 92, 2, T.err, T.bg);
        var ls = wrapLines(otaErr, 216, 1);
        var y = 122;
        for (var i = 0; i < ls.length && i < 7; i++, y += 14) {
            ctext(ls[i], 120, y, 1, T.textDim, T.bg);
        }
    } else if (otaState === "uptodate") {
        ctext("Sistema atualizado!", 120, 110, 2, T.ok, T.bg);
        ctext("CelerOS v" + System.getOSVersion(), 120, 140, 1, T.textDim, T.bg);
    } else if (otaState === "avail") {
        ctext(trunc(otaInfo.type || "Atualizacao disponivel", 224, 1), 120, 60, 1, T.accent, T.bg);
        ctext(trunc("v" + System.getOSVersion() + " -> v" + otaInfo.version, 224, 2), 120, 78, 2, T.text, T.bg);
        // bloco rolavel (changelog + guia), janela 98..(98+otaRegionH)
        var yEnd = 98 + otaRegionH();
        for (var j = 0; j < otaLines.length; j++) {
            var ln = otaLines[j];
            var y2 = 98 + ln.y - scroll;
            if (y2 < 92 || y2 + 12 > yEnd) continue;
            if (ln.h) {
                System.setTextColor(T.text, T.bg);
                System.drawString(ln.t, 12, y2, 1);
            } else {
                System.setTextColor(T.textDim, T.bg);
                System.drawString(ln.t, 12, y2, 1);
            }
        }
        if (otaInfo.hasFirmware) {
            ctext("nao desligue na atualizacao", 120, 208, 1, T.err, T.bg);
            System.fillRoundRect(60, 222, 120, 32, 8, T.ok);
            ctext("Instalar", 120, 238, 2, T.text, T.ok);
        } else {
            ctext("Canal sem firmware:", 120, 210, 1, T.textDim, T.bg);
            ctext("siga o guia acima.", 120, 224, 1, T.textDim, T.bg);
        }
    } else if (otaState === "installing") {
        ctext("Atualizando sistema", 120, 96, 2, T.text, T.bg);
        ctext("Nao desligue a alimentacao!", 120, 118, 1, T.err, T.bg);
        System.drawRoundRect(30, 158, 180, 18, 4, T.stroke);
    } else if (otaState === "done") {
        ctext("Atualizacao concluida!", 120, 104, 2, T.ok, T.bg);
        ctext("Reinicie para concluir.", 120, 132, 1, T.textDim, T.bg);
        System.fillRoundRect(60, 170, 120, 36, 8, T.accent);
        ctext("Reiniciar", 120, 188, 2, T.onAccent, T.accent);
    }
}
function doCheck() {
    otaState = "checking";
    drawAll();
    if (!Net.isConnected()) {
        otaState = "nowifi";
        drawAll();
        return;
    }
    var info = System.otaCheck();
    otaInfo = info;
    if (info.fetchFailed) {
        otaState = "failed";
        otaErrTitle = "Falha na verificacao";
        otaErr = "Confira a conexao e tente de novo.";
    } else if (!info.available) {
        otaState = "uptodate";
    } else {
        otaState = "avail";
        scroll = 0;
        buildOtaLines();
    }
    drawAll();
}
function startInstall() {
    otaState = "installing";
    drawAll();
    // cb desenha o progresso direto no display (mesma geometria do draw)
    var res = System.otaStart(otaInfo.url, function (p) {
        System.fillRect(50, 126, 140, 22, T.bg);  // limpa o % anterior
        ctext(p + "%", 120, 140, 2, T.text, T.bg);
        var fw = Math.round(176 * p / 100) - 4;
        if (fw > 0) System.fillRect(32, 160, fw, 14, T.accent);
    });
    if (res && res.ok) {
        otaState = "done";
    } else {
        otaState = "failed";
        otaErrTitle = "Falha na instalacao";
        otaErr = (res && res.error) ? res.error : "erro desconhecido";
    }
    drawAll();
}

// ---- Sobre -----------------------------------------------------------------

function buildAbout() {
    var inf = System.getInfo();
    var sdTot = FS.getTotalSpace("/sd");
    var a = [];
    a.push({ l: "Versao", v: "v" + System.getOSVersion(), i: 1 });
    a.push({ l: "API", v: "" + System.getAPILevel(), i: 1 });
    a.push({ l: "Chip", v: inf.chipModel || "--", i: 1 });
    a.push({ l: "CPU", v: inf.cpuFreqMHz + " MHz x" + inf.chipCores, i: 1 });
    a.push({ l: "RAM livre", v: fmtBytes(inf.freeRAM), i: 1 });
    a.push({ l: "Flash", v: fmtBytes(inf.flashSize), i: 1 });
    a.push({ l: "IP", v: System.getIPAddress() || "--", i: 1 });
    a.push({ l: "Interno", v: fmtBytes(FS.getFreeSpace("/local")) + " / " + fmtBytes(FS.getTotalSpace("/local")), i: 1 });
    a.push({ l: "SD", v: (sdTot > 0) ? (fmtBytes(FS.getFreeSpace("/sd")) + " / " + fmtBytes(sdTot)) : "ausente", i: 1 });
    a.push({ l: "Uptime", v: fmtUptime(inf.uptimeMs), i: 1 });
    return a;
}

// ---- Reset -----------------------------------------------------------------

function drawReset() {
    System.fillRoundRect(8, 52, 224, 76, 10, T.card);
    System.drawRoundRect(8, 52, 224, 76, 10, T.stroke);
    System.setTextColor(T.text, T.card);
    System.drawString("Limpar configuracoes", 20, 62, 2);
    System.setTextColor(T.textDim, T.card);
    System.drawString("Apaga Wi-Fi, PIN, brilho e outras", 20, 88, 1);
    System.drawString("configuracoes. Apps sao mantidos.", 20, 102, 1);
    System.fillRoundRect(48, 136, 144, 32, 8, T.err);
    ctext("Limpar", 120, 152, 2, T.text, T.err);

    System.fillRoundRect(8, 180, 224, 76, 10, T.card);
    System.drawRoundRect(8, 180, 224, 76, 10, T.stroke);
    System.setTextColor(T.text, T.card);
    System.drawString("Reset total", 20, 190, 2);
    System.setTextColor(T.warn, T.card);
    System.drawString("APAGA TUDO: apps, icones e configs.", 20, 216, 1);
    System.setTextColor(T.textDim, T.card);
    System.drawString("Recuperacao exige cabo USB.", 20, 230, 1);
    System.fillRoundRect(48, 258, 144, 32, 8, T.err);
    ctext("Reset total", 120, 274, 2, T.text, T.err);
}
function doReset(mode) {
    System.fillScreen(T.bg);
    header("Reset");
    ctext(mode === "total" ? "Formatando..." : "Limpando...", 120, 150, 2, T.text, T.bg);
    ctext("o aparelho vai reiniciar", 120, 176, 1, T.textDim, T.bg);
    System.delay(400);
    System.factoryReset(mode);
    System.delay(600);
    System.restart();
}

// ---- toques ----------------------------------------------------------------

function onTap() {
    var t = { x: lastX, y: lastY };
    if (modal) {
        if (hit(t, 28, 208, 84, 28)) {
            modal = null;
            drawAll();
        } else if (hit(t, 128, 208, 84, 28)) {
            var f = modal.f;
            modal = null;
            f();
        }
        return;
    }
    if (tela !== "update" || otaState !== "done") {
        if (hit(t, 8, 282, 84, 30)) { footerBack(); return; }
    }
    if (tela === "update" && (otaState === "nowifi" || otaState === "failed" ||
        otaState === "uptodate" || otaState === "avail")) {
        if (hit(t, 140, 282, 92, 30)) { doCheck(); return; }
    }

    if (tela === "menu") {
        var idx = rowAt(t);
        if (idx < 0) return;
        var a = items[idx].a;
        if (a === "wifi") go("wifi");
        else if (a === "apps") go("apps");
        else if (a === "time") go("time");
        else if (a === "sec") go("security");
        else if (a === "display") go("display");
        else if (a === "ota") go("update");
        else if (a === "about") go("about");
        else if (a === "reset") go("reset");
    } else if (tela === "wifi") {
        if (hit(t, 48, 150, 144, 36)) {
            // abre a tela NATIVA de Wi-Fi; este app precisa sair na sequencia
            System.openWifiSetup();
            System.exitApp();
        }
    } else if (tela === "apps") {
        var idx2 = rowAt(t);
        if (idx2 < 0) return;
        var a2 = items[idx2].a;
        if (a2 === "dest") {
            if (installSd()) FS.deleteFile(INSTALL_SD);
            else FS.writeTextFile(INSTALL_SD, "1");
            rebuildItems();
            drawAll();
        } else if (a2 && a2.indexOf("app:") === 0) {
            selApp = parseInt(a2.substring(4), 10);
            go("appdetail");
        }
    } else if (tela === "appdetail") {
        if (hit(t, 48, 246, 144, 32)) askUninstall(selApp);
    } else if (tela === "time") {
        var idx3 = rowAt(t);
        if (idx3 < 0) return;
        var a3 = items[idx3].a;
        if (a3 === "ntp") {
            System.setNtpEnabled(!System.getNtpEnabled());
            rebuildItems();
            drawAll();
        } else if (a3 === "fmt") {
            System.set24hFormat(!System.get24hFormat());
            rebuildItems();
            drawAll();
        } else if (a3 === "tz") {
            go("tz");
        } else if (a3 === "manual") {
            if (System.getNtpEnabled()) note("Hora e fuso", "Desligue o NTP antes de ajustar", 1100);
            else runManual();
        }
    } else if (tela === "tz") {
        var idx4 = rowAt(t);
        if (idx4 < 0) return;
        System.setTimezone(TZS[idx4][1]);
        note("Fuso horario", "Fuso aplicado", 800);
        go("time");
    } else if (tela === "security") {
        var idx5 = rowAt(t);
        if (idx5 < 0) return;
        var a5 = items[idx5].a;
        if (a5 === "set") {
            if (flowNewPin()) { rebuildItems(); drawAll(); }
        } else if (a5 === "chg") {
            var cur = System.prompt("PIN atual", "");
            if (cur === null || cur === "") return;
            if (!pinOk(cur)) { note("Seguranca", "PIN incorreto", 1100); return; }
            if (flowNewPin()) { rebuildItems(); drawAll(); }
        } else if (a5 === "rm") {
            var cur2 = System.prompt("PIN atual", "");
            if (cur2 === null || cur2 === "") return;
            if (!pinOk(cur2)) { note("Seguranca", "PIN incorreto", 1100); return; }
            FS.deleteFile(PIN_FILE);
            note("Seguranca", "PIN removido", 1000);
            rebuildItems();
            drawAll();
        }
    } else if (tela === "update") {
        if (otaState === "idle" && hit(t, 48, 150, 144, 36)) doCheck();
        else if (otaState === "avail" && otaInfo.hasFirmware && hit(t, 60, 222, 120, 32)) startInstall();
        else if (otaState === "done" && hit(t, 60, 170, 120, 36)) System.restart();
    } else if (tela === "reset") {
        if (hit(t, 48, 136, 144, 32)) {
            askConfirm("Limpar configuracoes?",
                "Apaga redes Wi-Fi, PIN, brilho e outras configuracoes. Apps e icones sao mantidos.",
                "Continuar", function () {
                    askConfirm("Confirmar limpeza", "Limpar todas as configuracoes agora?", "Apagar",
                        function () { doReset("configs"); });
                });
        } else if (hit(t, 48, 258, 144, 32)) {
            askConfirm("Reset total?",
                "APAGA TUDO: apps, icones e configuracoes. A recuperacao exige cabo USB.",
                "Continuar", function () {
                    askConfirm("Apagar tudo?",
                        "O armazenamento interno sera formatado. Os apps somem!",
                        "Formatar", function () { doReset("total"); });
                });
        }
    }
}

// ---- gate de PIN na entrada ------------------------------------------------

function drawPinGate(triesLeft) {
    System.fillScreen(T.bg);
    header("Settings");
    ctext("App protegido por PIN", 120, 108, 2, T.text, T.bg);
    ctext("Digite o PIN para continuar", 120, 138, 1, T.textDim, T.bg);
    ctext("Tentativas: " + triesLeft, 120, 168, 1, T.accent, T.bg);
}
function pinGate() {
    var ok = false;
    for (var attempt = 0; attempt < 3 && !ok; attempt++) {
        drawPinGate(3 - attempt);
        var pin = System.prompt("PIN do Settings", "");
        if (pin === null || pin === "") System.exitApp();  // cancelou: volta
        if (pinOk(pin)) ok = true;
        else note("PIN", "PIN incorreto", 900);
    }
    if (!ok) {
        note("PIN", "Acesso negado", 1200);
        System.exitApp();
    }
}

// ---- loop principal --------------------------------------------------------

if (FS.exists(PIN_FILE)) pinGate();

var down = false;
var lastX = 0, lastY = 0, movedPx = 0;
var dragBri = false, scrolled = false;
var lastScrollDraw = 0;
var lastAuto = 0;
var lastClock = "";

go("menu");
while (true) {
    var t = System.getTouch();  // canto sup. direito sai (exit nativo)
    if (t.touched) {
        if (!down) {
            down = true;
            lastX = t.x; lastY = t.y;
            movedPx = 0;
            if (!modal && rowsVisible()) {
                pressIdx = rowAt(t);
                if (pressIdx >= 0 && (items[pressIdx].i || items[pressIdx].d)) pressIdx = -1;
                if (pressIdx >= 0) drawAll();
            }
        } else {
            var dx = t.x - lastX;
            var dy = t.y - lastY;
            movedPx += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            lastX = t.x; lastY = t.y;
            if (pressIdx >= 0 && movedPx >= 12) {
                pressIdx = -1;
                drawAll();
            }
            // rolagem por arraste (listas e changelog da OTA)
            if (!dragBri && !modal && hasScroll() && (dy > 2 || dy < -2)) {
                scroll -= dy;
                clampScroll();
                scrolled = true;
                if (System.millis() - lastScrollDraw > 40) {
                    lastScrollDraw = System.millis();
                    drawAll();
                }
            }
        }
        // slider de brilho: arrastar ajusta, soltar persiste
        if (tela === "display" && System.backlightSupported()) {
            if (!dragBri && t.x >= 10 && t.x <= 230 && t.y >= 138 && t.y <= 196) {
                dragBri = true;
                movedPx = 99;
            }
            if (dragBri) {
                var lv = Math.round((t.x - 24) * 100 / 192);
                if (lv < 5) lv = 5;
                if (lv > 100) lv = 100;
                if (lv !== blLvl) {
                    blLvl = lv;
                    drawAll();
                }
            }
        }
    } else if (down) {
        down = false;
        var wasPressed = pressIdx >= 0;
        pressIdx = -1;
        if (dragBri) {
            dragBri = false;
            System.setBrightness(blLvl);  // persiste (grava sozinho)
        } else if (scrolled) {
            scrolled = false;
            drawAll();  // posicao final da rolagem
        } else if (movedPx < 12) {
            onTap();  // tap: usa a ultima posicao tocada
        }
        // tira o destaque da linha (so vai ao vidro no proximo yield: um
        // redesenho extra depois do onTap nao pisca)
        if (wasPressed) drawAll();
    }

    // atualizacao periodica (Wi-Fi do menu/estado, relogio da tela Hora)
    if (System.millis() - lastAuto > 1000) {
        lastAuto = System.millis();
        if (!modal && !down) {
            if (tela === "menu" || tela === "wifi") {
                var s = scroll;
                rebuildItems();
                scroll = s;
                clampScroll();
                var sig = JSON.stringify(items) + (tela === "wifi" ? JSON.stringify(System.wifiStatus()) : "");
                if (sig !== itemsSig) {
                    itemsSig = sig;
                    drawAll();
                }
            } else if (tela === "time") {
                var tm = System.getTime();
                if (tm !== lastClock) {
                    lastClock = tm;
                    var s2 = scroll;
                    rebuildItems();
                    scroll = s2;
                    clampScroll();
                    drawAll();
                }
            }
        }
    }
    System.delay(20);
}
