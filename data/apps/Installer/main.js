// CelerOS Installer — app de sistema (W8). Porte do InstallerUI.cpp:
// escaneia /sd/apps/, mostra detalhes dos pacotes e instala na flash
// (/local/apps) ou no proprio SD (flag /local/config_install_sd.txt).
// X no canto sup. direito sai.

var T = System.theme();

var SD_FLAG = "/local/config_install_sd.txt";
var SD_APPS = "/sd/apps";
var LCL_APPS = "/local/apps";
var ROW_H = 44;   // altura das linhas da lista
var PITCH = 50;   // linha + gap
var VIS = 4;      // linhas visiveis
var TOP = 80;     // topo da lista

// ---- helpers de UI (padrao dos apps de sistema) ---------------------------
function ctext(s, cx, cy, f, col, bg) {
    System.setTextColor(col, bg);
    // centro vertical pela altura real da fonte (API 3+: System.fontHeight)
    var fh = System.fontHeight ? System.fontHeight(f) : (f >= 2 ? 16 : 10);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), cy - (fh >> 1), f);
}
function lcenter(s, cx, y, f, col, bg) {
    System.setTextColor(col, bg);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), y, f);
}
function hit(t, x, y, w, h) {
    return t.x >= x && t.x <= x + w && t.y >= y && t.y <= y + h;
}
function header(title) {
    // titulo na faixa do sistema (API 6) — sem cabecalho desenhado
    if (System.topbarText) System.topbarText(title);
}
// voltar so existe onde ha navegacao real (detail -> list); nas listas o X
// da faixa sai do app
function footer() {
    if (state !== "detail") return;
    System.fillRoundRect(8, 282, 84, 30, 8, T.raised);
    System.drawRoundRect(8, 282, 84, 30, 8, T.stroke);
    ctext("< Voltar", 50, 297, 2, T.text, T.raised);
}
function trunc(s, maxw, f) {
    if (System.textWidth(s, f) <= maxw) return s;
    while (s.length > 1 && System.textWidth(s + "...", f) > maxw) {
        s = s.substring(0, s.length - 1);
    }
    return s + "...";
}
function wrap(s, maxChars, maxLines) {
    var out = [];
    if (!s || s.length === 0) return out;
    var words = s.split(" ");
    var cur = "";
    for (var i = 0; i < words.length; i++) {
        var w = words[i];
        if (w.length > maxChars) w = w.substring(0, maxChars);
        if (cur.length === 0) cur = w;
        else if (cur.length + 1 + w.length <= maxChars) cur = cur + " " + w;
        else {
            out[out.length] = cur;
            if (out.length >= maxLines) return out;
            cur = w;
        }
    }
    if (cur.length > 0 && out.length < maxLines) out[out.length] = cur;
    return out;
}

// ---- estado ----------------------------------------------------------------
var state = "boot"; // list | empty | detail | alert
var apps = [];
var sdOk = false;
var scroll = 0;
var sel = 0;
var detail = null;
var alertTitle = "";
var alertLines = [];
var alertErr = false;

// ---- logica de instalacao ---------------------------------------------------
function baseName(p) {
    var i = p.lastIndexOf("/");
    return i >= 0 ? p.substring(i + 1) : p;
}
function flagSD() { return FS.exists(SD_FLAG); }
function destBase() {
    if (flagSD() && FS.isDirectory("/sd")) return SD_APPS;
    return LCL_APPS;
}
function destLabel() {
    return destBase() === SD_APPS ? "SD (/sd/apps)" : "Flash (/local/apps)";
}
function toggleDest() {
    if (flagSD()) FS.deleteFile(SD_FLAG);
    else FS.writeTextFile(SD_FLAG, "sd\n");
    refreshInstalled();
    if (state === "list") drawList();
    else if (state === "detail") drawDetail();
}
function verParts(v) {
    var p = [0, 0, 0];
    var s = String(v).split(".");
    for (var i = 0; i < 3 && i < s.length; i++) {
        var n = parseInt(s[i], 10);
        if (!isNaN(n)) p[i] = n;
    }
    return p;
}
function verGreater(a, b) {
    var x = verParts(a);
    var y = verParts(b);
    for (var i = 0; i < 3; i++) {
        if (x[i] > y[i]) return true;
        if (x[i] < y[i]) return false;
    }
    return false;
}
function validPkg(pkg) {
    if (pkg.length === 0) return false;
    if (pkg.indexOf(" ") !== -1) return false;
    if (pkg.indexOf(".") === -1) return false;
    return pkg === pkg.toLowerCase();
}
function installedMeta(app) {
    var p = destBase() + "/" + app.pkg + "/app.json";
    if (!FS.exists(p)) return null;
    var txt = FS.readTextFile(p);
    if (txt === null || txt.length === 0) return null;
    try { return JSON.parse(txt); } catch (e) { return null; }
}
function refreshInstalled() {
    var base = destBase();
    for (var i = 0; i < apps.length; i++) {
        var app = apps[i];
        app.inst = false;
        app.instVer = "";
        if (app.pkg.length === 0) continue;
        var jp = base + "/" + app.pkg + "/app.json";
        if (!FS.exists(jp)) continue;
        app.inst = true;
        var m = installedMeta(app);
        if (m !== null && m.version) app.instVer = String(m.version);
    }
}
function scanApps() {
    apps = [];
    sdOk = FS.mountSD();
    if (!sdOk) return;
    var entries = FS.listDir(SD_APPS); // retorna caminhos completos
    for (var i = 0; i < entries.length; i++) {
        var base = baseName(entries[i]);
        if (base.length === 0 || base.charAt(0) === ".") continue;
        var dir = SD_APPS + "/" + base;
        if (!FS.isDirectory(dir)) continue;
        var jp = dir + "/app.json";
        if (!FS.exists(jp)) continue;
        var txt = FS.readTextFile(jp);
        if (txt === null || txt.length === 0) continue;
        var m = null;
        try { m = JSON.parse(txt); } catch (e) { m = null; }
        if (m === null || typeof m.name !== "string" || m.name.length === 0) continue;
        apps[apps.length] = {
            folder: dir,
            name: m.name,
            pkg: (typeof m.packageName === "string") ? m.packageName : "",
            version: (typeof m.version === "string" && m.version.length > 0) ? m.version : "1.0.0",
            api: (typeof m.api === "number") ? m.api : (parseInt(m.api, 10) || 0),
            desc: (typeof m.description === "string") ? m.description : "",
            author: (typeof m.author === "string") ? m.author : "",
            inst: false,
            instVer: ""
        };
    }
    apps.sort(function (a, b) {
        var x = a.name.toLowerCase();
        var y = b.name.toLowerCase();
        return x < y ? -1 : (x > y ? 1 : 0);
    });
    System.print("Installer: " + apps.length + " app(s) em " + SD_APPS);
}

// ---- telas ------------------------------------------------------------------
function drawState() {
    if (state === "list") drawList();
    else if (state === "empty") drawEmpty();
    else if (state === "detail") drawDetail();
    else if (state === "alert") drawAlert();
}
function drawBusy() {
    System.fillScreen(T.bg);
    header("Installer");
    ctext("Buscando apps...", 120, 158, 2, T.textDim, T.bg);
}
function drawEmpty() {
    System.fillScreen(T.bg);
    header("Installer");
    System.fillRoundRect(8, 56, 224, 150, 10, T.card);
    System.drawRoundRect(8, 56, 224, 150, 10, T.stroke);
    ctext("SD sem apps", 120, 92, 2, T.warn, T.card);
    var y = 122;
    if (sdOk) lcenter("Nenhum app em /sd/apps", 120, y, 1, T.text, T.card);
    else lcenter("Cartao SD nao encontrado", 120, y, 1, T.text, T.card);
    y += 20;
    lcenter("Copie apps para o cartao ou", 120, y, 1, T.textDim, T.card);
    y += 14;
    lcenter("use o celerctl / App Store.", 120, y, 1, T.textDim, T.card);
    System.fillRoundRect(8, 230, 224, 32, 8, T.accent);
    ctext("Reescanear", 120, 246, 2, T.onAccent, T.accent);
    footer();
}
function drawList() {
    System.fillScreen(T.bg);
    header("Installer");

    // faixa de destino (toque para alternar flash/SD)
    System.fillRoundRect(8, 48, 224, 26, 8, T.card);
    System.drawRoundRect(8, 48, 224, 26, 8, T.stroke);
    System.setTextColor(T.textDim, T.card);
    System.drawString("Destino:", 16, 57, 1);
    System.setTextColor(T.accent, T.card);
    System.drawString(destLabel(), 16 + System.textWidth("Destino: ", 1), 57, 1);
    System.setTextColor(T.textDim, T.card);
    System.drawString(">", 216, 57, 1);

    for (var v = 0; v < VIS; v++) {
        var i = scroll + v;
        if (i >= apps.length) break;
        var app = apps[i];
        var y = TOP + v * PITCH;
        var isSel = (i === sel);
        var bg = isSel ? T.raised : T.card;
        System.fillRoundRect(8, y, 224, ROW_H, 8, bg);
        System.drawRoundRect(8, y, 224, ROW_H, 8, T.stroke);
        if (isSel) System.fillRect(8, y + 6, 4, ROW_H - 12, T.accent);
        System.setTextColor(T.text, bg);
        System.drawString(trunc(app.name, 192, 2), 18, y + 5, 2);
        System.setTextColor(T.textDim, bg);
        System.drawString("v" + app.version + "  -  API " + app.api, 18, y + 28, 1);
        if (app.inst) {
            var s = "instalado";
            System.setTextColor(T.ok, bg);
            System.drawString(s, 220 - System.textWidth(s, 1), y + 28, 1);
        }
    }
    if (apps.length > VIS) {
        lcenter((scroll + 1) + "-" + Math.min(scroll + VIS, apps.length) + " de " + apps.length,
                120, 278, 1, T.textDim, T.bg);
    }
    footer();
}
function drawDetail() {
    var app = detail;
    var inst = installedMeta(app);
    var isUpd = (inst !== null && inst.version && verGreater(app.version, inst.version));

    System.fillScreen(T.bg);
    header(trunc(app.name, 200, 2));

    System.fillRoundRect(8, 48, 224, 150, 10, T.card);
    System.drawRoundRect(8, 48, 224, 150, 10, T.stroke);
    System.setTextColor(T.text, T.card);
    System.drawString(trunc(app.name, 208, 2), 16, 56, 2);
    System.setTextColor(T.textDim, T.card);
    System.drawString("v" + app.version + "  -  API " + app.api, 16, 76, 1);
    if (app.author.length > 0) {
        System.drawString("Autor: " + trunc(app.author, 28, 1), 16, 90, 1);
    }
    var lines = wrap(app.desc, 34, 4);
    var ly = app.author.length > 0 ? 108 : 96;
    for (var i = 0; i < lines.length; i++) {
        System.drawString(lines[i], 16, ly, 1);
        ly += 12;
    }

    // faixa de status
    System.fillRoundRect(8, 204, 224, 22, 8, T.card);
    System.drawRoundRect(8, 204, 224, 22, 8, T.stroke);
    var stTxt;
    var stCol;
    if (inst === null) { stTxt = "Nao instalado"; stCol = T.textDim; }
    else if (isUpd) { stTxt = "Nova versao: v" + app.version; stCol = T.warn; }
    else { stTxt = "Instalado v" + (inst.version ? String(inst.version) : "?"); stCol = T.ok; }
    System.setTextColor(stCol, T.card);
    System.drawString(stTxt, 16, 210, 1);

    System.setTextColor(T.textDim, T.bg);
    System.drawString(trunc("Instalar em: " + destBase() + "/" + app.pkg, 216, 1), 12, 234, 1);

    var lbl = "Instalar";
    if (inst !== null) lbl = isUpd ? "Atualizar" : "Reinstalar";
    System.fillRoundRect(8, 248, 224, 32, 8, T.accent);
    ctext(lbl, 120, 264, 2, T.onAccent, T.accent);

    footer();
}
function drawAlert() {
    System.fillScreen(T.bg);
    header("Installer");
    System.fillRoundRect(8, 64, 224, 146, 10, T.card);
    System.drawRoundRect(8, 64, 224, 146, 10, T.stroke);
    ctext(trunc(alertTitle, 208, 4), 120, 100, 4, alertErr ? T.err : T.ok, T.card);
    var y = 132;
    for (var i = 0; i < alertLines.length; i++) {
        lcenter(alertLines[i], 120, y, 1, T.text, T.card);
        y += 15;
    }
    System.fillRoundRect(73, 222, 94, 32, 8, T.accent);
    ctext("OK", 120, 238, 2, T.onAccent, T.accent);
    footer();
}
function drawInstalling(app) {
    System.fillScreen(T.bg);
    header("Installer");
    System.fillRoundRect(20, 90, 200, 130, 10, T.card);
    System.drawRoundRect(20, 90, 200, 130, 10, T.stroke);
    ctext("Instalando...", 120, 118, 2, T.text, T.card);
    lcenter(trunc(app.name, 30, 1), 120, 136, 1, T.textDim, T.card);
    System.drawRoundRect(40, 160, 160, 14, 7, T.stroke);
    System.fillRoundRect(41, 161, 94, 12, 6, T.accent);
    lcenter("Nao desligue o aparelho", 120, 186, 1, T.textDim, T.card);
    System.delay(250); // garante o render antes da copia bloqueante
}

// ---- fluxo -------------------------------------------------------------------
function showAlert(title, lines, isErr) {
    state = "alert";
    alertTitle = title;
    alertLines = lines;
    alertErr = isErr;
    drawAlert();
}
function openApp(i) {
    var app = apps[i];
    sel = i;
    var lvl = System.getAPILevel();
    if (app.api > lvl) {
        showAlert("Incompativel",
                  ["O app requer API " + app.api + ".",
                   "Este OS tem API " + lvl + ".",
                   "Atualize o CelerOS."], true);
        return;
    }
    if (!validPkg(app.pkg)) {
        showAlert("Pacote invalido",
                  ["packageName incorreto.",
                   "Use minusculas, sem espacos",
                   "e com ponto (a.b.c)."], true);
        return;
    }
    var inst = installedMeta(app);
    if (inst !== null && inst.author && app.author && inst.author !== app.author) {
        showAlert("Conflito",
                  ["Autor diferente do instalado:",
                   "Instalado: " + trunc(String(inst.author), 24, 1),
                   "Novo: " + trunc(app.author, 24, 1)], true);
        return;
    }
    detail = app;
    state = "detail";
    drawDetail();
}
function tryInstall(app) {
    if (!validPkg(app.pkg)) {
        showAlert("Pacote invalido",
                  ["packageName incorreto.",
                   "Use minusculas, sem espacos",
                   "e com ponto (a.b.c)."], true);
        return;
    }
    var base = destBase();
    var dest = base + "/" + app.pkg;
    drawInstalling(app); // copia e bloqueante, sem callback de progresso
    FS.mkdir(base);
    // Transacional: copia para <pkg>.inst e so entao troca pela versao antiga.
    // Antes a antiga era apagada ANTES da copia — disco cheio no meio deixava
    // o usuario sem nenhuma das duas (e com uma copia pela metade no disco).
    var tmp = dest + ".inst";
    if (FS.isDirectory(tmp)) FS.removeDirectory(tmp);  // sobra de falha anterior
    var ok = FS.copyDirectory(app.folder, tmp);
    if (ok) {
        if (FS.isDirectory(dest)) ok = FS.removeDirectory(dest);
        if (ok) ok = FS.renameFile(tmp, dest);
    } else if (FS.isDirectory(dest)) {
        // sem espaco para duas copias: cai no modo antigo (remove e copia)
        if (FS.isDirectory(tmp)) FS.removeDirectory(tmp);
        ok = FS.removeDirectory(dest) && FS.copyDirectory(app.folder, dest);
        if (!ok && FS.isDirectory(dest)) FS.removeDirectory(dest);
    }
    if (FS.isDirectory(tmp)) FS.removeDirectory(tmp);
    if (ok) {
        System.rescanApps();
        showAlert("Instalado!",
                  [trunc(app.name, 34, 1),
                   "v" + app.version,
                   "Em " + trunc(dest, 26, 1)], false);
    } else {
        var livre = Math.round(FS.getFreeSpace(base) / 1024);
        showAlert("Falhou",
                  ["Nao foi possivel copiar o app.",
                   "Espaco livre: " + livre + " KB"], true);
    }
}
function maxScroll() {
    var m = apps.length - VIS;
    return m > 0 ? m : 0;
}
function onTap(x, y) {
    var pt = { x: x, y: y };
    if (state === "list") {
        if (hit(pt, 8, 48, 224, 26)) { toggleDest(); return; }
        if (y >= TOP && y < 278) {
            var i = scroll + Math.floor((y - TOP) / PITCH);
            if (i >= 0 && i < apps.length) openApp(i);
        }
    } else if (state === "empty") {
        if (hit(pt, 8, 230, 224, 32)) rescan();
    } else if (state === "detail") {
        if (hit(pt, 8, 282, 84, 30)) { state = "list"; drawList(); return; }  // navegacao real
        if (hit(pt, 8, 248, 224, 32)) tryInstall(detail);
    } else if (state === "alert") {
        if (hit(pt, 8, 282, 84, 30) || hit(pt, 73, 222, 94, 32)) {
            state = "list";
            refreshInstalled();
            drawList();
        }
    }
}
function rescan() {
    drawBusy();
    scanApps();
    refreshInstalled();
    scroll = 0;
    sel = 0;
    detail = null;
    state = apps.length > 0 ? "list" : "empty";
    drawState();
}

// ---- inicializacao + loop -----------------------------------------------------
drawBusy();
scanApps();
refreshInstalled();
state = apps.length > 0 ? "list" : "empty";
drawState();

var down = false;
var downX = 0;
var downY = 0;
var moved = false;
while (true) {
    var t = System.getTouch(); // canto sup. direito sai automaticamente
    if (t.touched) {
        if (!down) {
            down = true;
            moved = false;
            downX = t.x;
            downY = t.y;
        } else if (state === "list") {
            // rolagem por drag vertical (1 linha a cada 30 px)
            var dy = t.y - downY;
            if (dy >= 30) {
                moved = true;
                downY = t.y;
                if (scroll > 0) { scroll--; drawList(); }
            } else if (dy <= -30) {
                moved = true;
                downY = t.y;
                if (scroll < maxScroll()) { scroll++; drawList(); }
            }
        }
    } else {
        if (down && !moved) onTap(downX, downY);
        down = false;
    }
    System.delay(20);
}
