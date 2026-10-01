// CelerOS App Store v3 — app de sistema com abas. Le o catalogo do CelerOS
// Hub (os.celer.tec.br; 2 HTTPS: indice + "Todos"), instala/atualiza apps em
// /local/apps ou /sd/apps, desinstala e ATUALIZA A SI MESMA (celeros.appstore
// esta no proprio catalogo: gravar os arquivos em disco basta — o main.js e
// relido do disco a cada abertura; ao se atualizar, a loja pede sair/reabrir).
//
// Estrutura (a faixa de topo e a topbar do sistema; o canvas comeca abaixo):
//   [Loja | Atualizacoes | Meus apps]  strip de abas (y 0..26)
//   Loja:        chips de categoria (drag horizontal) + lista com pill de
//                estado por linha (Novo / Instalado vX / Atualizar)
//   Atualizacoes: so apps com versao nova (vLocal -> vRemota + changelog),
//                rodape "Atualizar tudo" (a propria loja vai por ultimo)
//   Meus apps:   instalados, com Desinstalar no detalhe (dupla confirmacao
//                p/ app de sistema)
// Instalacao: main.js em <pkg>/main.js.new via Net.download (streaming, sem
// teto de 32KB), MD5 do catalogo conferido, rename atomico no mesmo FS; a
// pasta alvo e RESOLVIDA por packageName (primeira vista na ordem do
// listDir, mesma regra do launcher) — updates in-place nao criam copias
// sombreadas de apps preinstalados ("App Store" vs celeros.appstore) e as
// duplicatas antigas sao removidas apos o update. X no canto sup. sai.

var INDEX_URL = "https://os.celer.tec.br/store/index.json";
// Compatibilidade de hardware: sem PSRAM, o main.js (compilado + dados do
// app) tem que caber na RAM interna. Teto DINAMICO pela RAM que a placa da
// a um app (getInfo().appRAM: DRAM + IRAM livres no inicio do app — freeRAM
// agora mede a loja ja carregada). Calibrado na CYD (appRAM ~225KB): app de
// 61KB roda, de 82KB nao compila -> (appRAM - 60000) * 0,4 = ~66KB.
// Firmware antigo sem appRAM: formula antiga (heap base de ~52KB).
var HWINFO = System.getInfo ? System.getInfo() : null;
var NO_PSRAM = !!(HWINFO && HWINFO.totalPSRAM === 0);
var PSRAM_MAX_JS = !NO_PSRAM ? Infinity
    : HWINFO.appRAM ? Math.max(4096, Math.floor((HWINFO.appRAM - 60000) * 0.4))
    : Math.max(4096, Math.floor(((HWINFO.freeRAM || 0) - 52000) / 1.7));
function needsPsram(it) { return (it.size || 0) > PSRAM_MAX_JS; }

// Flag "instalar no SD" no NVS de settings (F3; System.setting). Arquivo
// legado continua valendo para firmware antigo.
var FLAG_SD = "/local/config_install_sd.txt";
function installOnSd() {
    if (System.setting) {
        var v = System.setting("install_sd");
        if (v !== null) return v === "1";
    }
    return FS.exists(FLAG_SD);
}
var CACHE = "/local/appstore_cache.json";
var STORE_PKG = "celeros.appstore";

var T = System.theme();
var API = System.getAPILevel();

// ---- helpers de UI ---------------------------------------------------------
function ctext(s, cx, cy, f, col, bg) {
    System.setTextColor(col, bg);
    var fh = System.fontHeight ? System.fontHeight(f) : (f >= 2 ? 16 : 10);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), cy - (fh >> 1), f);
}
function hit(t, x, y, w, h) {
    return t.x >= x && t.x <= x + w && t.y >= y && t.y <= y + h;
}
function waitRelease() {
    var guard = 0;
    while (guard < 200) {
        var t = System.getTouch();
        if (!t.touched) return;
        System.delay(10);
        guard++;
    }
}
function baseName(p) {
    var i = p.lastIndexOf("/");
    return i >= 0 ? p.substring(i + 1) : p;
}
function dirName(p) {
    var i = p.lastIndexOf("/");
    return i > 0 ? p.substring(0, i) : p;
}
function truncLine(s, maxw, f) {
    if (System.textWidth(s, f) <= maxw) return s;
    var r = s;
    while (r.length > 1 && System.textWidth(r + "..", f) > maxw) {
        r = r.substring(0, r.length - 1);
    }
    return r + "..";
}
function wrapLines(s, maxw, f, maxLines) {
    var out = [];
    var cur = "";
    var words = s.split(" ");
    for (var i = 0; i < words.length; i++) {
        var w = words[i];
        if (!w.length) continue;
        var trial = cur.length ? cur + " " + w : w;
        if (!cur.length || System.textWidth(trial, f) <= maxw) cur = trial;
        else { out.push(cur); cur = w; }
    }
    if (cur.length) out.push(cur);
    while (out.length > maxLines) out.pop();
    if (out.length > 0 && System.textWidth(out[out.length - 1], f) > maxw) {
        out[out.length - 1] = truncLine(out[out.length - 1], maxw, f);
    }
    return out;
}
function fmtKB(n) {
    if (!n || n <= 0) return "";
    if (n < 1024) return n + " B";
    var kb = Math.round(n / 102.4) / 10;
    return (kb % 1 ? kb : kb.toFixed(0)) + " KB";
}
function pill(label, rightX, y, bgc, fgc) {
    var w = System.textWidth(label, 1) + 12;
    System.fillRoundRect(rightX - w, y, w, 14, 7, bgc);
    ctext(label, rightX - w / 2, y + 7, 1, fgc, bgc);
    return w;
}

// ---- rede (Net bloqueante; qualquer erro vira null/false) ------------------
function fetchJSON(url) {
    try { return Net.getJSON(url); } catch (e) { return null; }
}
function fetchText(url) {
    try { return Net.get(url); } catch (e) { return null; }
}

// semver numerico parte a parte (herdado do AppStoreUI.cpp)
function cmpV(v1, v2) {
    v1 = v1 || "";
    v2 = v2 || "";
    var p1 = 0, p2 = 0;
    while (p1 < v1.length || p2 < v2.length) {
        var n1 = 0, n2 = 0;
        while (p1 < v1.length && v1.charAt(p1) != ".") {
            n1 = n1 * 10 + (v1.charCodeAt(p1) - 48);
            p1++;
        }
        while (p2 < v2.length && v2.charAt(p2) != ".") {
            n2 = n2 * 10 + (v2.charCodeAt(p2) - 48);
            p2++;
        }
        if (n1 > n2) return 1;
        if (n1 < n2) return -1;
        p1++;
        p2++;
    }
    return 0;
}

// ---- estado ----------------------------------------------------------------
var apps = [];        // catalogo
var cats = [];        // ["Todos", "Arcade", ...] (do catalogo)
var curCat = "Todos";
var catScroll = 0;
var curTab = 0;       // 0=Loja 1=Atualizacoes 2=Meus apps
var scrollYs = [0, 0, 0];
var selIt = null;     // item aberto no detalhe
var localMap = {};    // pkg -> {ver, root, dir, name, system, sd}
var updCount = 0;
var errMsg = "", errHint = "", retryMode = "";
var wasUpdate = false, selfUpdated = false;
var batchOk = 0, batchFails = [];

var TAB_H = 26, FOOT_Y = 282, ROW_H = 44, PITCH = 50, LIST_END = 272;
var LIST_Y = 54;      // 54 na Loja (chips acima), 32 nas outras abas
var query = "";       // filtro de busca da Loja ("" = desligado)

// ---- disco: instalados -----------------------------------------------------
// mesma regra do launcher (LauncherUI::scanLocalApps): PRIMEIRO visto ganha —
// a ordem do listDir e a mesma nos dois, entao o estado exibido e o que o
// launcher executa de verdade.
function scanLocalApps() {
    localMap = {};
    var roots = ["/local/apps", "/sd/apps"];
    for (var r = 0; r < 2; r++) {
        var root = roots[r];
        if (!FS.isDirectory(root)) continue;
        var list = FS.listDir(root);
        for (var i = 0; i < list.length; i++) {
            var dir = list[i];
            if (!FS.isDirectory(dir)) continue;
            var body = FS.readTextFile(dir + "/app.json");
            if (!body) continue;
            var doc = null;
            try { doc = JSON.parse(body); } catch (e) { doc = null; }
            if (!doc) continue;
            var pkg = doc.packageName || baseName(dir);
            if (localMap[pkg]) continue;  // duplicata: o launcher usa a 1a
            localMap[pkg] = {
                ver: doc.version || "", dir: dir, root: root,
                name: doc.name || pkg, system: doc.system === true,
                sd: root === "/sd/apps"
            };
        }
    }
}

// pasta onde o pkg vive hoje (na ordem do launcher) ou null.
function resolveInstalledDir(pkg) {
    var roots = ["/local/apps", "/sd/apps"];
    for (var r = 0; r < 2; r++) {
        var root = roots[r];
        if (!FS.isDirectory(root)) continue;
        var list = FS.listDir(root);
        for (var i = 0; i < list.length; i++) {
            var dir = list[i];
            if (!FS.isDirectory(dir)) continue;
            var body = FS.readTextFile(dir + "/app.json");
            if (!body) continue;
            var doc = null;
            try { doc = JSON.parse(body); } catch (e2) { doc = null; }
            if (doc && (doc.packageName || baseName(dir)) === pkg) return dir;
        }
    }
    return null;
}

// remove copias sombreadas do pkg (duplicatas de installs antigos);
// keepDir e a pasta que o launcher executa.
function removeShadowed(pkg, keepDir) {
    var roots = ["/local/apps", "/sd/apps"];
    for (var r = 0; r < 2; r++) {
        var root = roots[r];
        if (!FS.isDirectory(root)) continue;
        var list = FS.listDir(root);
        for (var i = 0; i < list.length; i++) {
            var dir = list[i];
            if (dir === keepDir || !FS.isDirectory(dir)) continue;
            var body = FS.readTextFile(dir + "/app.json");
            if (!body) continue;
            var doc = null;
            try { doc = JSON.parse(body); } catch (e2) { doc = null; }
            if (doc && doc.packageName === pkg) FS.removeDirectory(dir);
        }
    }
}

function catalogByPkg(pkg) {
    for (var i = 0; i < apps.length; i++) {
        if (apps[i].pkg === pkg) return apps[i];
    }
    return null;
}

// estado do app vs instalado: "new" | "upd" | "inst" | "api"
function stateInfo(it) {
    var loc = localMap[it.pkg];
    if ((it.api || 1) > API) return { code: "api", txt: "API " + it.api, col: T.warn };
    if (NO_PSRAM && needsPsram(it)) return { code: "hw", txt: "Requer PSRAM", col: T.warn };
    if (!loc) return { code: "new", txt: "Novo", col: T.ok };
    // so oferece update se o hub for MAIS NOVO: versao local mais nova (dev,
    // imagem da flash a frente do hub) nao pode virar "Atualizar" (downgrade)
    if (cmpV(it.ver, loc.ver) <= 0) {
        return { code: "inst", txt: "Instalado", col: T.textDim };
    }
    return { code: "upd", txt: "Atualizar", col: T.warn };
}

function countUpdates() {
    updCount = 0;
    for (var i = 0; i < apps.length; i++) {
        if (stateInfo(apps[i]).code === "upd") updCount++;
    }
}

// atualizacoes primeiro; empate = ordem alfabetica
function sortByUpdate() {
    apps.sort(function (a, b) {
        var ua = stateInfo(a).code === "upd" ? 0 : 1;
        var ub = stateInfo(b).code === "upd" ? 0 : 1;
        if (ua !== ub) return ua - ub;
        var x = a.name.toLowerCase(), y = b.name.toLowerCase();
        return x < y ? -1 : (x > y ? 1 : 0);
    });
}

function refresh() {
    scanLocalApps();
    countUpdates();
    sortByUpdate();
    buildCats();
}

// itens da aba Meus apps: todo pkg instalado (do catalogo quando existir,
// sintetico quando nao)
function installedItems() {
    var out = [];
    for (var pkg in localMap) {
        if (!localMap.hasOwnProperty(pkg)) continue;
        var it = catalogByPkg(pkg);
        if (!it) {
            it = {
                pkg: pkg, name: localMap[pkg].name, ver: localMap[pkg].ver,
                desc: "", author: "", api: 1, appUrl: "", metaUrl: "",
                icon: "", changelog: "", size: 0, md5: "", cat: "Apps"
            };
        }
        out.push(it);
    }
    out.sort(function (a, b) {
        var x = a.name.toLowerCase(), y = b.name.toLowerCase();
        return x < y ? -1 : (x > y ? 1 : 0);
    });
    return out;
}

// ---- catalogo ---------------------------------------------------------------
function entryToItem(pkg, e) {
    return {
        id: pkg, pkg: pkg,
        metaUrl: e.meta || "",
        appUrl: e.app || "",
        name: e.name || "",
        desc: e.description || "",
        author: e.author || "Desconhecido",
        ver: e.version || "1.0.0",
        api: e.api || 1,
        cat: e.category || "Apps",
        icon: e.icon || "",
        changelog: e.changelog || "",
        size: e.size || 0,
        md5: e.md5 || "",
        published: e.published_at || ""
    };
}
function fillItemFromMeta(it) {
    var m = fetchJSON(it.metaUrl);
    if (!m) return null;
    it.pkg = m.packageName || it.pkg;
    it.name = m.name || it.id;
    it.desc = m.description || "";
    it.author = m.author || "Desconhecido";
    it.ver = m.version || "1.0.0";
    it.api = m.api || 1;
    it.cat = m.category || "Apps";
    return it;
}
function buildCats() {
    var seen = {};
    cats = ["Todos"];
    for (var i = 0; i < apps.length; i++) {
        var c = apps[i].cat || "Apps";
        if (!seen[c]) { seen[c] = 1; cats.push(c); }
    }
}
function filteredApps() {
    var out = [];
    var q = query.toLowerCase();
    for (var i = 0; i < apps.length; i++) {
        if (curCat !== "Todos" && (apps[i].cat || "Apps") !== curCat) continue;
        if (q && ((apps[i].name || "").toLowerCase().indexOf(q) < 0 &&
                  (apps[i].desc || "").toLowerCase().indexOf(q) < 0)) continue;
        out.push(apps[i]);
    }
    return out;
}
function updApps() {
    var out = [];
    for (var i = 0; i < apps.length; i++) {
        if (stateInfo(apps[i]).code === "upd") out.push(apps[i]);
    }
    return out;
}

function drawLoading(msg, sub) {
    System.fillScreen(T.bg);
    drawTabs();
    ctext(truncLine(msg, 216, 2), 120, 130, 2, T.text, T.bg);
    if (sub) ctext(truncLine(sub, 216, 1), 120, 155, 1, T.textDim, T.bg);
}

function loadCatalog() {
    apps = [];
    curCat = "Todos";
    catScroll = 0;
    selIt = null;
    retryMode = "load";

    drawLoading("Baixando catálogo...", "");
    System.delay(30);
    var idx = fetchJSON(INDEX_URL);
    if (!idx || !idx.categories) {
        errMsg = "Falha ao baixar o catálogo";
        errHint = "Verifique a internet.";
        return "err";
    }

    // atalho "Todos" (all.json): todos os apps em 1 request; sem ele (hubs
    // antigos) ou se ele nao couber na RAM (placa sem PSRAM: o JSON inteiro
    // + o parse de uma vez), categoria a categoria — pedacos menores
    var entries = [];
    var catsIdx = idx.categories;
    var gotAll = false;
    if (catsIdx["Todos"]) {
        drawLoading("Baixando catálogo...", "todos os apps");
        System.delay(30);
        var all = fetchJSON(catsIdx["Todos"]);
        if (all && all.apps) {
            gotAll = true;
            for (var pkg in all.apps) {
                if (!all.apps.hasOwnProperty(pkg)) continue;
                if (all.apps[pkg] && all.apps[pkg].app) {
                    entries.push({ pkg: pkg, e: all.apps[pkg] });
                }
            }
        }
        all = null;
    }
    if (!gotAll) {
        var catTotal = 0, catOk = 0;
        for (var cname in catsIdx) {
            if (!catsIdx.hasOwnProperty(cname) || cname === "Todos") continue;
            catTotal++;
            drawLoading("Categoria: " + cname, "");
            System.delay(30);
            var cat = fetchJSON(catsIdx[cname]);
            if (!cat || !cat.apps) continue;
            catOk++;
            for (var id in cat.apps) {
                if (!cat.apps.hasOwnProperty(id)) continue;
                var e = cat.apps[id];
                if (!e || !e.app) continue;
                entries.push({ pkg: id, e: e });
            }
        }
        if (entries.length === 0 && catTotal > 0 && catOk === 0) {
            errMsg = "Falha ao baixar o catálogo";
            errHint = "Verifique a internet.";
            return "err";
        }
    }

    var n = entries.length;
    var seenPkg = {};
    for (var k = 0; k < n; k++) {
        var it = entryToItem(entries[k].pkg, entries[k].e);
        if ((it.api || 1) > API) continue;
        if (!it.name && it.metaUrl) {
            drawLoading("Carregando apps...", (k + 1) + "/" + n);
            System.delay(30);
            it = fillItemFromMeta(it);
            if (!it) continue;
        }
        if (seenPkg[it.pkg]) continue;
        seenPkg[it.pkg] = 1;
        apps.push(it);
    }

    refresh();
    if (apps.length > 0) FS.writeTextFile(CACHE, JSON.stringify(apps));
    return "list";
}

function loadCache() {
    var body = FS.readTextFile(CACHE);
    if (!body) return false;
    var arr = null;
    try { arr = JSON.parse(body); } catch (e) { arr = null; }
    if (!arr || !arr.length) return false;
    apps = arr;
    refresh();
    selIt = null;
    return true;
}

// ---- abas -------------------------------------------------------------------
function drawTabs() {
    System.fillRect(0, 0, 240, TAB_H, T.card);
    System.drawFastHLine(0, TAB_H, 240, T.stroke);
    var names = ["Loja", "Atualizações", "Meus apps"];
    for (var i = 0; i < 3; i++) {
        var col;
        if (i === curTab) col = T.text;
        else if (i === 1 && updCount > 0) col = T.warn;
        else col = T.textDim;
        ctext(names[i], i * 80 + 40, TAB_H / 2, 1, col, T.card);
        if (i === curTab) System.fillRect(i * 80 + 10, TAB_H - 3, 60, 3, T.accent);
    }
}

// chips de categoria da Loja (drag horizontal quando nao cabem) — o primeiro
// chip e fixo: abre a busca (ou limpa o filtro ativo)
function searchChipLabel() {
    return query ? query + " x" : "Buscar";
}
function chipsGeom() {
    var xs = [], x = 8;
    var qw = System.textWidth(searchChipLabel(), 1) + 16;
    xs.push({ x: x, w: qw, search: true });
    x += qw + 6;
    for (var i = 0; i < cats.length; i++) {
        var w = System.textWidth(cats[i], 1) + 16;
        xs.push({ x: x, w: w });
        x += w + 6;
    }
    return { xs: xs, total: x };
}
function drawChips() {
    var g = chipsGeom();
    var maxScroll = g.total > 232 ? g.total - 232 : 0;
    if (catScroll > maxScroll) catScroll = maxScroll;
    if (catScroll < 0) catScroll = 0;
    if (typeof System.setClip === "function") {
        System.setClip(0, 30, 240, 20);
    }
    for (var i = 0; i < g.xs.length; i++) {
        var cx = g.xs[i].x - catScroll;
        if (cx + g.xs[i].w < 0 || cx > 240) continue;
        if (g.xs[i].search) {
            var on = query !== "";
            System.fillRoundRect(cx, 30, g.xs[i].w, 18, 9, on ? T.warn : T.card);
            System.drawRoundRect(cx, 30, g.xs[i].w, 18, 9, on ? T.warn : T.stroke);
            ctext(searchChipLabel(), cx + g.xs[i].w / 2, 39, 1,
                  on ? T.bg : T.textDim, on ? T.warn : T.card);
            continue;
        }
        var ci = i - 1;
        var active = cats[ci] === curCat;
        System.fillRoundRect(cx, 30, g.xs[i].w, 18, 9,
                             active ? T.accent : T.card);
        System.drawRoundRect(cx, 30, g.xs[i].w, 18, 9,
                             active ? T.accent : T.stroke);
        ctext(cats[ci], cx + g.xs[i].w / 2, 39, 1,
              active ? T.onAccent : T.textDim,
              active ? T.accent : T.card);
    }
    if (typeof System.clearClip === "function") System.clearClip();
}
function chipAt(x) {
    var g = chipsGeom();
    for (var i = 0; i < g.xs.length; i++) {
        var cx = g.xs[i].x - catScroll;
        if (x >= cx && x <= cx + g.xs[i].w) return i;   // 0 = chip de busca
    }
    return -1;
}

// ---- listas -----------------------------------------------------------------
function curList() {
    if (curTab === 0) return filteredApps();
    if (curTab === 1) return updApps();
    return installedItems();
}
function maxScroll(len) {
    var m = (len - 4) * PITCH;
    return m > 0 ? m : 0;
}
function clampScroll(len) {
    var m = maxScroll(len);
    if (scrollYs[curTab] < 0) scrollYs[curTab] = 0;
    if (scrollYs[curTab] > m) scrollYs[curTab] = m;
}
function listY() {
    if (curTab === 0) return 54;              // chips em 30..48
    return 32 + 18;                            // linha de contagem em 32..50
}
function drawCountLine() {
    if (curTab === 1) {
        ctext(updCount === 0 ? "Nenhuma atualização" :
              (updCount + (updCount === 1 ? " atualização disponível"
                                          : " atualizações disponíveis")),
              120, 41, 1, updCount ? T.warn : T.textDim, T.bg);
    } else if (curTab === 2) {
        var n = 0;
        for (var k in localMap) if (localMap.hasOwnProperty(k)) n++;
        ctext(n + (n === 1 ? " app instalado" : " apps instalados"),
              120, 41, 1, T.textDim, T.bg);
    }
}
function drawRow(it, y, subLeft, subRight) {
    System.fillRoundRect(8, y, 224, ROW_H, 8, T.card);
    System.drawRoundRect(8, y, 224, ROW_H, 8, T.stroke);
    var st = stateInfo(it);
    var lbl = st.code === "new" ? "Novo" :
              st.code === "upd" ? "Atualizar" :
              st.code === "api" ? ("API " + it.api) : "Instalado";
    var pw = System.textWidth(lbl, 1) + 12;
    System.setTextColor(T.text, T.card);
    System.drawString(truncLine(it.name, 224 - pw - 26, 2), 18, y + 3, 2);
    statePillLine(it, y);
    System.setTextColor(T.textDim, T.card);
    System.drawString(truncLine(subLeft, 150, 1), 18, y + 31, 1);
    if (subRight) {
        System.drawString(subRight, 224 - System.textWidth(subRight, 1), y + 31, 1);
    }
}
function statePillLine(it, y) {
    var st = stateInfo(it);
    if (st.code === "new") pill("Novo", 224, y + 4, T.ok, T.bg);
    else if (st.code === "upd") pill("Atualizar", 224, y + 4, T.warn, T.bg);
    else if (st.code === "api") pill("API " + it.api, 224, y + 4, T.raised, T.warn);
    else pill("Instalado", 224, y + 4, T.raised, T.textDim);
}
function drawList() {
    System.fillScreen(T.bg);
    drawTabs();
    var items = curList();
    LIST_Y = listY();
    if (curTab === 0) drawChips();
    else drawCountLine();

    var scrollY = scrollYs[curTab];
    clampScroll(items.length);
    scrollY = scrollYs[curTab];

    if (items.length === 0) {
        var msg = curTab === 1 ? "Tudo em dia!" :
                  curTab === 2 ? "Nenhum app instalado" : "Nenhum app";
        ctext(msg, 120, 140, 2, T.text, T.bg);
        if (curTab === 1) ctext("Volte depois para conferir novidades.", 120, 164, 1, T.textDim, T.bg);
        drawFooter();
        return;
    }

    var first = Math.floor(scrollY / PITCH);
    var y = LIST_Y - (scrollY - first * PITCH);
    var clip = typeof System.setClip === "function";
    if (clip) System.setClip(0, LIST_Y, 240, LIST_END - LIST_Y);
    for (var i = first; i < items.length && y < LIST_END; i++, y += PITCH) {
        if (!clip && (y < LIST_Y || y + ROW_H > LIST_END)) continue;
        var it = items[i];
        if (curTab === 1) {
            var lm = localMap[it.pkg];
            drawRow(it, y,
                    (lm ? "v" + lm.ver : "v?") + " -> v" + it.ver +
                    (it.changelog ? "  " + it.changelog.split("\n")[0] : ""),
                    "");
        } else if (curTab === 2) {
            var l2 = localMap[it.pkg];
            drawRow(it, y,
                    "v" + (l2 ? l2.ver : it.ver) +
                    (l2 && l2.sd ? "  no SD" : "") +
                    (l2 && l2.system ? "  sistema" : ""), "");
        } else {
            drawRow(it, y, it.desc, "v" + it.ver);
        }
    }
    if (clip) System.clearClip();

    drawFooter();
    if (maxScroll(items.length) > 0) {
        System.drawFastVLine(235, LIST_Y, LIST_END - LIST_Y, T.stroke);
        var trackH = LIST_END - LIST_Y;
        var thH = Math.max(24, Math.floor(trackH * trackH / (items.length * PITCH)));
        var ty = LIST_Y + Math.floor((trackH - thH) * scrollY / maxScroll(items.length));
        System.fillRect(234, ty, 3, thH, T.accent);
    }
}
function footerButton(x, w, label, enabled) {
    System.fillRoundRect(x, FOOT_Y, w, 30, 8, enabled ? T.raised : T.card);
    System.drawRoundRect(x, FOOT_Y, w, 30, 8, T.stroke);
    ctext(label, x + w / 2, FOOT_Y + 15, 1, enabled ? T.text : T.textDim,
          enabled ? T.raised : T.card);
}
function drawFooter() {
    if (curTab === 0) footerButton(140, 92, "Atualizar", true);
    else if (curTab === 1) footerButton(140, 92, "Atualizar tudo", updCount > 0);
    else footerButton(140, 92, "< Sair", true);
}
// drag rola; toque parado devolve o item ou null
function listDrag(t0, items) {
    var startY = t0.y;
    var startScroll = scrollYs[curTab];
    var moved = false;
    var t = t0;
    var guard = 0;
    while (t.touched && guard < 400) {
        t = System.getTouch();
        if (!t.touched) break;
        if (Math.abs(t.y - startY) > 8) moved = true;
        if (moved) {
            scrollYs[curTab] = startScroll + (startY - t.y);
            clampScroll(items.length);
            drawList();
        }
        System.delay(20);
        guard++;
    }
    if (moved) {
        scrollYs[curTab] = Math.round(scrollYs[curTab] / PITCH) * PITCH;
        clampScroll(items.length);
        return null;
    }
    LIST_Y = listY();
    var idx = Math.floor((t0.y - LIST_Y + scrollYs[curTab]) / PITCH);
    if (idx >= 0 && idx < items.length) {
        waitRelease();
        return items[idx];
    }
    return null;
}
function screenList() {
    drawList();
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (t.y < TAB_H) {  // troca de aba
                var nt = Math.floor(t.x / 80);
                if (nt >= 0 && nt < 3 && nt !== curTab) {
                    waitRelease();
                    curTab = nt;
                    selIt = null;
                } else {
                    waitRelease();
                }
                drawList();
            } else if (curTab === 0 && t.y >= 30 && t.y < 50) {
                // chips: tap seleciona; drag horizontal rola. 0 = busca
                var r = chipsDrag(t);
                if (r === 0) {
                    if (query) {
                        query = "";                 // x limpa o filtro
                    } else if (typeof System.prompt === "function") {
                        var q = System.prompt("Buscar app", "", "");
                        query = (q && String(q).length) ? String(q) : "";
                    }
                    scrollYs[0] = 0;
                } else if (r > 0) {
                    curCat = cats[r - 1];
                    scrollYs[0] = 0;
                }
                drawList();
            } else if (t.y >= listY() && t.y < LIST_END) {
                var items = curList();
                var it = listDrag(t, items);
                if (it) {
                    selIt = it;
                    return "detail";
                }
                drawList();
            } else if (t.y >= FOOT_Y) {
                if (hit(t, 140, FOOT_Y, 92, 30)) {
                    waitRelease();
                    if (curTab === 0) {
                        return Net.isConnected() ? "load" : "wifi";
                    }
                    if (curTab === 1) {
                        if (updCount > 0) return "batch";
                    } else {
                        return "exit";
                    }
                } else {
                    waitRelease();
                }
            } else {
                waitRelease();
            }
        }
        System.delay(20);
    }
}
// devolve o indice do chip tocado ou -1; drag horizontal rola os chips
function chipsDrag(t0) {
    var startScroll = catScroll;
    var moved = false;
    var t = t0;
    var guard = 0;
    var g = chipsGeom();
    var maxS = g.total > 232 ? g.total - 232 : 0;
    while (t.touched && guard < 400) {
        t = System.getTouch();
        if (!t.touched) break;
        if (Math.abs(t.x - t0.x) > 6) moved = true;
        if (moved) {
            catScroll = startScroll - (t.x - t0.x);
            if (catScroll < 0) catScroll = 0;
            if (catScroll > maxS) catScroll = maxS;
            drawList();
        }
        System.delay(20);
        guard++;
    }
    if (moved) return -1;
    waitRelease();
    return chipAt(t0.x);
}

// ---- detalhe ----------------------------------------------------------------
function drawDetail() {
    var it = selIt;
    var lm = localMap[it.pkg];
    System.fillScreen(T.bg);
    System.setTextColor(T.text, T.bg);
    System.drawString(truncLine(it.name, 216, 2), 10, 6, 2);

    System.fillRoundRect(8, 30, 224, 92, 10, T.card);
    System.drawRoundRect(8, 30, 224, 92, 10, T.stroke);

    // icone do pacote instalado (cache do sistema) ou inicial
    var ip = lm ? (lm.dir + "/icon.png") : "";
    var drew = false;
    if (ip && FS.exists(ip) && typeof System.drawIcon === "function") {
        System.drawIcon(ip, 14, 44);
        drew = true;
    }
    if (!drew) {
        System.fillRoundRect(14, 44, 56, 56, 10, T.raised);
        ctext((it.name || "?").substring(0, 1).toUpperCase(), 42, 72, 2,
              T.textDim, T.raised);
    }

    var remote = it.appUrl ? ("v" + it.ver + (it.size ? " (" + fmtKB(it.size) + ")" : ""))
                           : "-";
    var rows = [
        ["Autor", truncLine(it.author || "-", 118, 1)],
        ["Local", lm ? ("v" + (lm.ver || "?")) : "não instalado"],
        ["Remota", remote]
    ];
    var yy = 42;
    for (var i = 0; i < rows.length; i++) {
        System.setTextColor(T.textDim, T.card);
        System.drawString(rows[i][0], 80, yy, 1);
        System.setTextColor(T.text, T.card);
        System.drawString(rows[i][1], 128, yy, 1);
        yy += 18;
    }
    var st = stateInfo(it);
    System.setTextColor(T.textDim, T.card);
    System.drawString("Estado", 80, yy, 1);
    System.setTextColor(st.col, T.card);
    System.drawString(st.txt + (lm ? " v" + lm.ver : ""), 128, yy, 1);

    // descricao + novidades
    var news = it.changelog && st.code !== "new" && st.code !== "api";
    System.setTextColor(T.textDim, T.bg);
    System.drawString("Descrição", 10, 130, 1);
    var lines = wrapLines(it.desc || "Sem descrição.", 220, 1, news ? 2 : 4);
    var y2 = 142;
    for (var k = 0; k < lines.length; k++) {
        System.setTextColor(T.text, T.bg);
        System.drawString(lines[k], 10, y2, 1);
        y2 += 13;
    }
    if (news) {
        System.setTextColor(T.textDim, T.bg);
        System.drawString("Novidades", 10, 186, 1);
        var nlines = wrapLines(it.changelog, 220, 1, 3);
        var y3 = 198;
        for (var j = 0; j < nlines.length; j++) {
            System.setTextColor(T.text, T.bg);
            System.drawString(nlines[j], 10, y3, 1);
            y3 += 13;
        }
    }

    // botoes: primario + desinstalar (se instalado)
    if (it.appUrl && (it.api || 1) > API) {
        ctext("Requer API " + it.api + " (sistema: " + API + ")", 120, 254, 1, T.warn, T.bg);
    } else if (it.appUrl && st.code === "hw") {
        ctext("Incompatível: requer hardware com PSRAM", 120, 250, 1, T.warn, T.bg);
        ctext("(a RAM interna não basta p/ este app)", 120, 264, 1, T.textDim, T.bg);
    } else if (it.appUrl) {
        var lbl = st.code === "upd" ? "Atualizar" :
                  st.code === "inst" ? "Reinstalar" : "Instalar";
        System.fillRoundRect(8, 236, 110, 36, 8, T.accent);
        ctext(lbl, 63, 254, 2, T.onAccent, T.accent);
        if (lm) {
            System.fillRoundRect(126, 236, 106, 36, 8, T.bg);
            System.drawRoundRect(126, 236, 106, 36, 8, T.err);
            ctext("Desinstalar", 179, 254, 2, T.err, T.bg);
        }
    } else if (lm) {
        System.fillRoundRect(65, 236, 110, 36, 8, T.bg);
        System.drawRoundRect(65, 236, 110, 36, 8, T.err);
        ctext("Desinstalar", 120, 254, 2, T.err, T.bg);
    }

    footerButton(8, 84, "< Voltar", true);
}
function askConfirm(title, body, yesLabel, danger) {
    System.fillRoundRect(20, 88, 200, 140, 10, T.card);
    System.drawRoundRect(20, 88, 200, 140, 10, danger ? T.err : T.stroke);
    ctext(truncLine(title, 180, 2), 120, 106, 2, T.text, T.card);
    var lines = wrapLines(body, 180, 1, 3);
    var y = 126;
    for (var i = 0; i < lines.length; i++) {
        ctext(lines[i], 120, y, 1, T.textDim, T.card);
        y += 13;
    }
    System.fillRoundRect(28, 186, 84, 30, 8, T.raised);
    ctext("Cancelar", 70, 201, 1, T.text, T.raised);
    System.fillRoundRect(128, 186, 84, 30, 8, danger ? T.err : T.accent);
    ctext(yesLabel, 170, 201, 1, danger ? T.text : T.onAccent,
          danger ? T.err : T.accent);
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 28, 186, 84, 30)) { waitRelease(); return false; }
            if (hit(t, 128, 186, 84, 30)) { waitRelease(); return true; }
            waitRelease();
        }
        System.delay(20);
    }
}
// remove o pacote instalado (sem UI de confirmacao — a tela pergunta antes)
function uninstallApp(pkg) {
    var dir = resolveInstalledDir(pkg);
    if (!dir) return false;
    // Storage privado do app (API 12): apaga junto — em firmware antigo o
    // typeof segue undefined e so os arquivos sao removidos
    if (typeof Storage !== "undefined" && Storage.clearFor) { try { Storage.clearFor(pkg); } catch (e) {} }
    if (!FS.removeDirectory(dir)) return false;
    System.rescanApps();
    refresh();
    return true;
}
function screenDetail() {
    if (!selIt) return "list";
    drawDetail();
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 8, FOOT_Y, 84, 30)) {
                waitRelease();
                return "list";
            }
            var it = selIt;
            var lm = localMap[it.pkg];
            if (hit(t, 8, 236, 110, 36) && it.appUrl && (it.api || 1) <= API) {
                waitRelease();
                return "install";
            }
            if (lm && it.appUrl && hit(t, 126, 236, 106, 36)) {
                waitRelease();
                doUninstallFlow(it);
                drawDetail();
            } else if (lm && !it.appUrl && hit(t, 65, 236, 110, 36)) {
                waitRelease();
                doUninstallFlow(it);
                if (!localMap[it.pkg]) return "list";  // item sintetico sumiu
                drawDetail();
            } else {
                waitRelease();
            }
        }
        System.delay(20);
    }
}
function doUninstallFlow(it) {
    var lm = localMap[it.pkg];
    if (!lm) return;
    var ok = askConfirm("Desinstalar",
                        "Remover " + it.name + " do aparelho?", "Remover", true);
    if (ok && lm.system) {
        ok = askConfirm("App do sistema",
                        "Confirma remover " + it.name +
                        " definitivamente?", "Remover", true);
    }
    if (ok && uninstallApp(it.pkg)) selIt = null;
}

// ---- instalacao / atualizacao ----------------------------------------------
function drawDownload(name, sub) {
    System.fillScreen(T.bg);
    drawTabs();
    ctext(truncLine("Baixando " + name + "...", 216, 2), 120, 96, 2, T.text, T.bg);
    if (sub) ctext(sub, 120, 124, 1, T.textDim, T.bg);
}
function drawProgress(name, got, total) {
    System.fillScreen(T.bg);
    drawTabs();
    ctext(truncLine("Baixando " + name + "...", 216, 2), 120, 96, 2, T.text, T.bg);
    var gotKB = Math.floor(got / 1024);
    System.drawRoundRect(40, 126, 160, 16, 6, T.stroke);
    if (total > 0) {
        var pct = got / total;
        if (pct > 1) pct = 1;
        if (pct > 0.02) System.fillRect(43, 129, Math.floor(154 * pct), 10, T.accent);
        ctext(Math.floor(pct * 100) + "%", 120, 160, 1, T.textDim, T.bg);
    } else if (gotKB > 0) {
        System.fillRect(43, 129, Math.min(154, 20 + (gotKB % 7) * 18), 10, T.accent);
        ctext(gotKB + " KB", 120, 160, 1, T.textDim, T.bg);
    }
}
// Update in-place: main.js novo entra como <pkg>/main.js.new (staging de um
// arquivo dentro do proprio pacote), MD5 do catalogo conferido e rename
// atomico por cima do antigo; app.json e icon.png vem depois. A pasta alvo e
// a que o launcher executa (resolveInstalledDir) e duplicatas sombreadas do
// mesmo pkg sao removidas no fim. Falha no meio nao quebra a versao ativa.
function installApp() {
    var it = selIt;
    if (it && stateInfo(it).code === "hw") {
        // incompativel com esta placa: mesmo caminho das falhas de install
        errMsg = "Incompatível com esta placa";
        errHint = "Este app exige hardware com PSRAM (a RAM interna não basta para o runtime dele).";
        return "err";
    }
    retryMode = "install";
    var fail = "";
    wasUpdate = stateInfo(it).code === "upd";
    selfUpdated = false;

    var dir = resolveInstalledDir(it.pkg);
    if (!dir) dir = (installOnSd() ? "/sd/apps" : "/local/apps") + "/" + it.pkg;
    var root = dirName(dir);
    var tmp = dir + "/main.js.new";

    if (!Net.isConnected()) {
        fail = "Sem conexão WiFi";
    } else {
        var need = (it.size || 0) + 16384;
        var free = 0;
        try { free = FS.getFreeSpace(root); } catch (e) { free = 0; }
        if (free > 0 && free < need) fail = "Sem espaço no disco";
    }

    var json = "";
    if (!fail && it.metaUrl) {
        drawDownload(it.name, "app.json");
        System.delay(30);
        json = fetchText(it.metaUrl);
        if (!json) fail = "Erro ao baixar app.json";
    }

    if (!fail && !FS.isDirectory(root) && !FS.mkdir(root)) fail = "Erro no disco";
    if (!fail && !FS.isDirectory(dir) && !FS.mkdir(dir)) fail = "Erro no disco";

    if (!fail) {
        var lastKB = -1;
        var okDL = false;
        try {
            okDL = Net.download(it.appUrl, tmp, function (got, total) {
                var kb = Math.floor(got / 1024);
                if (kb !== lastKB) { lastKB = kb; drawProgress(it.name, got, total); }
            });
        } catch (e) { okDL = false; }
        if (!okDL) fail = "Erro ao baixar main.js";
    }

    if (!fail && it.md5) {
        var md = "";
        try { md = FS.getFileMD5(tmp); } catch (e2) { md = ""; }
        if (md !== it.md5) {
            FS.deleteFile(tmp);
            fail = "Verificação falhou (md5)";
        }
    }

    if (!fail && !FS.renameFile(tmp, dir + "/main.js")) {
        FS.deleteFile(tmp);
        fail = "Erro ao gravar main.js";
    }
    if (!fail && json && !FS.writeTextFile(dir + "/app.json", json)) {
        fail = "Erro ao gravar app.json";
    }
    if (!fail) {
        if (it.icon) {
            var iconTmp = dir + "/icon.png.new";
            drawDownload(it.name, "icon.png");
            System.delay(30);
            var iconOk = false;
            try { iconOk = Net.download(it.icon, iconTmp); } catch (e3) { iconOk = false; }
            if (iconOk && FS.renameFile(iconTmp, dir + "/icon.png")) {
                // icone novo no lugar; o rescan revalida o cache
            } else {
                try { FS.deleteFile(iconTmp); } catch (e4) {}
            }
        } else if (FS.exists(dir + "/icon.png")) {
            FS.deleteFile(dir + "/icon.png");  // versao nova sem icone
        }
    }

    if (fail) {
        errMsg = fail;
        errHint = it.name;
        return "err";
    }
    try { removeShadowed(it.pkg, dir); } catch (e5) {}
    System.rescanApps();
    refresh();
    if (it.pkg === STORE_PKG) selfUpdated = true;
    return "done";
}

// "Atualizar tudo": sequencial; a propria loja (self-update) vai por ultimo.
// Guarda ITENS (nao indices): installApp -> refresh() reordena apps[] a cada
// update, e indice guardado viraria ponteiro pro app errado.
function updateAll() {
    var order = [];
    for (var i = 0; i < apps.length; i++) {
        if (stateInfo(apps[i]).code === "upd") order.push(apps[i]);
    }
    order.sort(function (a, b) {
        return (a.pkg === STORE_PKG ? 1 : 0) - (b.pkg === STORE_PKG ? 1 : 0);
    });
    batchOk = 0;
    batchFails = [];
    selfUpdated = false;
    for (var k = 0; k < order.length; k++) {
        selIt = order[k];
        drawBatch(k + 1, order.length, selIt.name);
        System.delay(30);
        if (installApp() === "done") batchOk++;
        else batchFails.push(selIt.name);
    }
    return "batchDone";
}
function drawBatch(k, n, name) {
    System.fillScreen(T.bg);
    drawTabs();
    ctext("Atualizando " + k + " de " + n, 120, 96, 2, T.text, T.bg);
    ctext(truncLine(name, 216, 1), 120, 124, 1, T.textDim, T.bg);
    ctext("não feche a loja", 120, 150, 1, T.textDim, T.bg);
}
function screenBatch() {
    updateAll();
    return "batchDone";
}
function screenBatchDone() {
    var self = selfUpdated;
    System.fillScreen(T.bg);
    drawTabs();
    if (batchFails.length === 0) {
        ctext("Tudo atualizado!", 120, 84, 2, T.ok, T.bg);
    } else {
        ctext("Terminado com falhas", 120, 84, 2, T.warn, T.bg);
    }
    ctext(batchOk + (batchOk === 1 ? " app atualizado" : " apps atualizados"),
          120, 110, 1, T.textDim, T.bg);
    var y = 132;
    for (var i = 0; i < batchFails.length && i < 4; i++) {
        ctext(truncLine("falhou: " + batchFails[i], 216, 1), 120, y, 1, T.err, T.bg);
        y += 14;
    }
    if (self) {
        ctext("A App Store se atualizou:", 120, y + 8, 1, T.text, T.bg);
        ctext("saia e abra de novo", 120, y + 22, 2, T.text, T.bg);
    }
    footerButton(8, 84, "< Voltar", true);
    if (self) footerButton(148, 84, "Sair agora", true);
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 8, FOOT_Y, 84, 30)) {
                waitRelease();
                curTab = 0;
                return "list";
            }
            if (self && hit(t, 148, FOOT_Y, 84, 30)) {
                waitRelease();
                System.exitApp();
            }
            waitRelease();
        }
        System.delay(20);
    }
}
function screenDone() {
    var it = selIt;
    System.fillScreen(T.bg);
    drawTabs();
    if (selfUpdated) {
        ctext("App Store atualizada!", 120, 84, 2, T.ok, T.bg);
        ctext(truncLine(it.name, 216, 2), 120, 112, 2, T.text, T.bg);
        ctext("saia e abra de novo para rodar v" + it.ver, 120, 140, 1, T.textDim, T.bg);
    } else {
        ctext(wasUpdate ? "Atualizado!" : "Instalado!", 120, 84, 2, T.ok, T.bg);
        ctext(truncLine(it.name, 216, 2), 120, 112, 2, T.text, T.bg);
        var lm = localMap[it.pkg];
        ctext("v" + it.ver + (lm && lm.sd ? "  no SD" : ""), 120, 138, 1, T.textDim, T.bg);
        ctext("App pronto no launcher.", 120, 156, 1, T.textDim, T.bg);
    }
    footerButton(8, 84, "< Voltar", true);
    if (selfUpdated) footerButton(148, 84, "Sair agora", true);
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 8, FOOT_Y, 84, 30)) {
                waitRelease();
                curTab = 0;
                scrollYs[0] = 0;
                return "list";
            }
            if (selfUpdated && hit(t, 148, FOOT_Y, 84, 30)) {
                waitRelease();
                System.exitApp();
            }
            waitRelease();
        }
        System.delay(20);
    }
}

// ---- erro / sem WiFi --------------------------------------------------------
function screenErr() {
    System.fillScreen(T.bg);
    drawTabs();
    ctext("Erro", 120, 84, 2, T.err, T.bg);
    var lines = wrapLines(errMsg, 216, 2, 2);
    var y = 110;
    for (var i = 0; i < lines.length; i++) {
        ctext(lines[i], 120, y, 2, T.text, T.bg);
        y += 20;
    }
    if (errHint) ctext(truncLine(errHint, 216, 1), 120, y + 6, 1, T.textDim, T.bg);

    System.fillRoundRect(24, 190, 192, 36, 8, T.accent);
    ctext("Tentar de novo", 120, 208, 2, T.onAccent, T.accent);
    footerButton(8, 84, "< Voltar", true);
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 24, 190, 192, 36)) {
                waitRelease();
                if (retryMode === "install") return "install";
                return "load";
            } else if (hit(t, 8, FOOT_Y, 84, 30)) {
                waitRelease();
                return "list";
            } else {
                waitRelease();
            }
        }
        System.delay(20);
    }
}
function screenWifi() {
    while (true) {
        System.fillScreen(T.bg);
        drawTabs();
        ctext("WiFi desconectado", 120, 104, 2, T.warn, T.bg);
        ctext("Conecte o WiFi para usar", 120, 134, 1, T.textDim, T.bg);
        ctext("a loja de apps.", 120, 150, 1, T.textDim, T.bg);
        footerButton(8, 84, "< Sair", true);
        var last = System.millis();
        var go = false;
        while (System.millis() - last < 1500) {
            var t = System.getTouch();
            if (t.touched) {
                if (hit(t, 8, FOOT_Y, 84, 30)) {
                    waitRelease();
                    System.exitApp();
                }
                waitRelease();
            }
            if (Net.isConnected()) { go = true; break; }
            System.delay(20);
        }
        if (go) return "load";
    }
}

// ---- fluxo principal --------------------------------------------------------
// No harness (test/js_harness) nao entra no loop de telas: expoe as funcoes
// puras e de instalacao para os checks de update/self-update.
if (typeof __harness !== "undefined" && __harness.storeTest) {
    __harness.storeTest({
        cmpV: cmpV,
        stateInfo: stateInfo,
        scanLocalApps: scanLocalApps,
        resolveInstalledDir: resolveInstalledDir,
        uninstallApp: uninstallApp,
        updateAll: updateAll,
        updCount: function () { return updCount; },
        refresh: refresh,
        setCatalog: function (arr) {
            apps = arr;
            selIt = arr.length ? arr[0] : null;
            refresh();
        },
        catalog: function () { return apps; },
        installApp: installApp,
        fmtKB: fmtKB,
        cats: function () { return cats; },
        setCat: function (c) { curCat = c; },
        filtered: filteredApps,
        installed: installedItems,
        selfUpdatedFlag: function () { return selfUpdated; },
        batchStats: function () { return { ok: batchOk, fails: batchFails }; },
        select: function (pkg) {
            var it = catalogByPkg(pkg);
            if (it) selIt = it;
            return it;
        }
    });
} else {
    var mode = loadCache() ? "list" : (Net.isConnected() ? "load" : "wifi");
    while (true) {
        if (mode === "wifi") mode = screenWifi();
        else if (mode === "load") mode = loadCatalog();
        else if (mode === "err") mode = screenErr();
        else if (mode === "list") mode = screenList();
        else if (mode === "detail") mode = screenDetail();
        else if (mode === "install") mode = installApp();
        else if (mode === "done") mode = screenDone();
        else if (mode === "batch") mode = screenBatch();
        else if (mode === "batchDone") mode = screenBatchDone();
        else System.exitApp();  // "exit"
    }
}
