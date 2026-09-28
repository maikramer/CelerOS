// CelerOS App Store — app de sistema. Le o catalogo do CelerOS Hub
// (os.celer.tec.br) e instala/atualiza apps em /local/apps ou /sd/apps.
//
// Catalogo: o hub ja manda nome/versao/autor/descricao/changelog/tamanho/md5
// no proprio indice (all.json) — 2 HTTPS no total (indice + "Todos"); se uma
// entrada vier sem nome (hub antigo), cai no caminho lento buscando o
// app.json de cada app. Cache em /local/appstore_cache.json abre a loja na
// hora (ate offline); "Atualizar" vai a rede.
//
// Update: estado "upd" = versao remota maior (semver). A lista ordena as
// atualizacoes primeiro, com banner fixo e contador no cabecalho. A
// instalacao grava main.js em <pkg>/main.js.new (streaming via Net.download,
// sem passar pela heap), confere o MD5 do catalogo e renomeia por cima do
// antigo (rename e atomico no mesmo FS); so entao grava app.json + icon.png.
// Staging de um arquivo dentro do proprio pacote: littlefs/CYD nao tem
// espaco para duas copias do app, e falha no meio nao quebra a versao
// instalada. X no canto sup. direito sai.

var INDEX_URL = "https://os.celer.tec.br/store/index.json";
var FLAG_SD = "/local/config_install_sd.txt";
// Catalogo da ultima carga bem-sucedida: a loja abre na hora (ate offline) e
// so vai a rede no "Atualizar".
var CACHE = "/local/appstore_cache.json";

var T = System.theme();
var API = System.getAPILevel();

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
function updLabel(n) {
    return n + (n === 1 ? " atualizacao" : " atualizacoes");
}
function header(title) {
    System.fillRoundRect(0, 0, 240, 40, 0, T.card);
    System.setTextColor(T.text, T.card);
    System.drawString(title, 12, 12, 2);
    var w = System.textWidth(title, 2);
    // contador de atualizacoes (so na tela raiz "App Store")
    if (updCount > 0 && title === "App Store") {
        var txt = updLabel(updCount);
        var pw = System.textWidth(txt, 1) + 14;
        if (12 + w + 8 + pw < 212) {
            System.fillRoundRect(212 - pw, 11, pw, 18, 9, T.warn);
            ctext(txt, 212 - pw / 2, 20, 1, T.bg, T.warn);
        }
    }
    System.fillRect(0, 40, 240, 3, T.accent);
}
function footerVoltar() {
    System.fillRoundRect(8, 282, 84, 30, 8, T.raised);
    System.drawRoundRect(8, 282, 84, 30, 8, T.stroke);
    ctext("< Voltar", 50, 297, 2, T.text, T.raised);
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
// corta a string em 1 linha ate caber em maxw (mede com a fonte real)
function truncLine(s, maxw, f) {
    if (System.textWidth(s, f) <= maxw) return s;
    var r = s;
    while (r.length > 1 && System.textWidth(r + "..", f) > maxw) {
        r = r.substring(0, r.length - 1);
    }
    return r + "..";
}
// quebra em linhas por largura; limita a maxLines (corta a ultima com "..")
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

// ---- rede (Net bloqueante; qualquer erro vira null/false) ------------------
function fetchJSON(url) {
    try { return Net.getJSON(url); } catch (e) { return null; }
}
function fetchText(url) {
    try { return Net.get(url); } catch (e) { return null; }
}

// compareVersions do AppStoreUI.cpp: semver numerico parte a parte
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
var apps = [];       // catalogo: [{id,pkg,metaUrl,appUrl,name,desc,author,
                     //            ver,api,icon,changelog,size,md5,published}]
var localMap = {};   // packageName -> {ver, root}
var scrollY = 0;
var sel = -1;        // indice do app aberto no detalhe
var errMsg = "";
var errHint = "";
var retryMode = "";  // "load" | "install"
var updCount = 0;    // apps com atualizacao (contador do cabecalho/banner)
var wasUpdate = false;

var LIST_Y = 50, LIST_END = 272, ROW_H = 44, PITCH = 50, VIS = 4;

// mapeia os apps instalados (/local/apps e /sd/apps) por packageName
function scanLocalApps() {
    localMap = {};
    var roots = ["/local/apps", "/sd/apps"];
    for (var r = 0; r < 2; r++) {
        var root = roots[r];
        if (!FS.isDirectory(root)) continue;
        var list = FS.listDir(root);
        for (var i = 0; i < list.length; i++) {
            var name = baseName(list[i]);
            var aj = root + "/" + name + "/app.json";
            if (!FS.exists(aj)) continue;
            var body = FS.readTextFile(aj);
            if (!body) continue;
            var doc = null;
            try { doc = JSON.parse(body); } catch (e) { doc = null; }
            if (!doc) continue;
            localMap[doc.packageName || name] = { ver: doc.version || "", root: root };
        }
    }
}

// estado do app vs instalado: "new" | "upd" | "inst" | "api"
function stateInfo(it) {
    var loc = localMap[it.pkg];
    if (it.api > API) return { code: "api", txt: "API " + it.api, col: T.warn };
    if (!loc) return { code: "new", txt: "novo", col: T.ok };
    if (cmpV(it.ver, loc.ver) === 0) {
        return { code: "inst", txt: "instalado v" + (loc.ver || "?"), col: T.textDim };
    }
    return { code: "upd", txt: "atualizacao", col: T.warn };
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

function drawLoading(msg, sub) {
    System.fillScreen(T.bg);
    header("App Store");
    ctext(truncLine(msg, 216, 2), 120, 130, 2, T.text, T.bg);
    if (sub) ctext(truncLine(sub, 216, 1), 120, 155, 1, T.textDim, T.bg);
}

// entrada do catalogo (all.json/categoria) -> item da loja. O entry novo ja
// traz tudo; sem "name" (hub antigo) busca o app.json de cada app.
function entryToItem(pkg, e) {
    return {
        id: pkg, pkg: pkg,
        metaUrl: e.meta || "",
        appUrl: e.app,
        name: e.name || "",
        desc: e.description || "",
        author: e.author || "Desconhecido",
        ver: e.version || "1.0.0",
        api: e.api || 1,
        icon: e.icon || "",
        changelog: e.changelog || "",
        size: e.size || 0,
        md5: e.md5 || "",
        published: e.published_at || ""
    };
}
function fillItemFromMeta(it) {
    var m = fetchJSON(it.metaUrl);
    if (!m) return null;  // meta fora do ar: nao lista
    it.pkg = m.packageName || it.pkg;
    it.name = m.name || it.id;
    it.desc = m.description || "";
    it.author = m.author || "Desconhecido";
    it.ver = m.version || "1.0.0";
    it.api = m.api || 1;
    return it;
}

// ---- carga do catalogo (indice -> "Todos" (ou categorias) -> entradas) -----
function loadCatalog() {
    scanLocalApps();
    apps = [];
    scrollY = 0;
    sel = -1;
    updCount = 0;
    retryMode = "load";

    drawLoading("Baixando catalogo...", "");
    System.delay(30);  // devolve o controle e da chance ao GC
    var idx = fetchJSON(INDEX_URL);
    if (!idx || !idx.categories) {
        errMsg = "Falha ao baixar o catalogo";
        errHint = "Verifique a internet.";
        return "err";
    }

    // coleta as entradas {pkg, e}. O atalho "Todos" (all.json) ja tem todos
    // os apps: evita baixar categoria a categoria (o indice do hub sempre o
    // tem); sem ele, cai no caminho das categorias (hubs antigos).
    var entries = [];
    var cats = idx.categories;
    if (cats["Todos"]) {
        drawLoading("Baixando catalogo...", "todos os apps");
        System.delay(30);
        var all = fetchJSON(cats["Todos"]);
        if (all && all.apps) {
            for (var pkg in all.apps) {
                if (!all.apps.hasOwnProperty(pkg)) continue;
                if (all.apps[pkg] && all.apps[pkg].app) {
                    entries.push({ pkg: pkg, e: all.apps[pkg] });
                }
            }
        }
    } else {
        var catTotal = 0, catOk = 0;
        for (var cname in cats) {
            if (!cats.hasOwnProperty(cname)) continue;
            catTotal++;
            drawLoading("Categoria: " + cname, "");
            System.delay(30);
            var cat = fetchJSON(cats[cname]);
            if (!cat || !cat.apps) continue;  // categoria fora do ar: pula
            catOk++;
            for (var id in cat.apps) {
                if (!cat.apps.hasOwnProperty(id)) continue;
                var e = cat.apps[id];
                if (!e || !e.app) continue;
                entries.push({ pkg: id, e: e });
            }
        }
        if (entries.length === 0 && catTotal > 0 && catOk === 0) {
            errMsg = "Falha ao baixar o catalogo";
            errHint = "Verifique a internet.";
            return "err";
        }
    }

    // entrada -> item; busca o app.json so quando o entry nao trouxer nome
    var n = entries.length;
    var seenPkg = {};
    for (var k = 0; k < n; k++) {
        var it = entryToItem(entries[k].pkg, entries[k].e);
        if ((it.api || 1) > API) continue;  // filtra como no original
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

    countUpdates();
    sortByUpdate();
    if (apps.length > 0) FS.writeTextFile(CACHE, JSON.stringify(apps));
    return "list";
}

// ---- tela: lista de apps ----------------------------------------------------
function maxScroll() {
    var m = (apps.length - VIS) * PITCH;
    return m > 0 ? m : 0;
}
function clampScroll() {
    var scrollMax = maxScroll();
    if (scrollY < 0) scrollY = 0;
    if (scrollY > scrollMax) scrollY = scrollMax;
}
function drawUpdBanner() {
    // fixo entre o cabecalho e a lista (so quando ha atualizacao)
    System.fillRoundRect(8, 47, 224, 18, 6, T.card);
    System.drawRoundRect(8, 47, 224, 18, 6, T.stroke);
    ctext(updLabel(updCount) + " disponiveis", 120, 56, 1, T.warn, T.card);
}
function drawRow(i, y) {
    var it = apps[i];
    System.fillRoundRect(8, y, 224, ROW_H, 8, T.card);
    System.drawRoundRect(8, y, 224, ROW_H, 8, T.stroke);
    System.setTextColor(T.text, T.card);
    System.drawString(truncLine(it.name, 190, 2), 20, y + 3, 2);
    var st = stateInfo(it);
    System.setTextColor(st.col, T.card);
    System.drawString(st.txt, 20, y + 22, 1);
    var vtxt = "v" + it.ver;
    System.setTextColor(T.textDim, T.card);
    System.drawString(vtxt, 222 - System.textWidth(vtxt, 1), y + 22, 1);
    System.drawString(truncLine(it.desc, 200, 1), 20, y + 33, 1);
}
function drawList() {
    System.fillScreen(T.bg);
    header("App Store");
    LIST_Y = updCount > 0 ? 72 : 50;  // banner come 22px da lista

    if (apps.length === 0) {
        ctext("Catalogo vazio", 120, 120, 2, T.text, T.bg);
        ctext("Nenhum app encontrado.", 120, 148, 1, T.textDim, T.bg);
        drawListFooter();
        return;
    }

    if (updCount > 0) drawUpdBanner();

    var scrollMax = maxScroll();
    var first = Math.floor(scrollY / PITCH);
    var y = LIST_Y - (scrollY - first * PITCH);
    // com recorte (API 3+), linhas parciais rolam suaves pelas bordas;
    // sem ele, so linhas inteiras (evita pintar sobre o cabecalho)
    var clip = typeof System.setClip === "function";
    if (clip) System.setClip(0, LIST_Y, 240, LIST_END - LIST_Y);
    for (var i = first; i < apps.length && y < LIST_END; i++, y += PITCH) {
        if (!clip && (y < LIST_Y || y + ROW_H > LIST_END)) continue;
        drawRow(i, y);
    }
    if (clip) System.clearClip();

    drawListFooter();
    if (scrollMax > 0) {
        System.drawFastVLine(235, LIST_Y, LIST_END - LIST_Y, T.stroke);
        var trackH = LIST_END - LIST_Y;
        var thH = Math.max(24, Math.floor(trackH * trackH / (apps.length * PITCH)));
        var ty = LIST_Y + Math.floor((trackH - thH) * scrollY / scrollMax);
        System.fillRect(234, ty, 3, thH, T.accent);
    }
}
// drag rola a lista; toque parado seleciona. devolve o indice ou -1
function listDrag(t0) {
    var startY = t0.y;
    var startScroll = scrollY;
    var moved = false;
    var t = t0;
    var guard = 0;
    while (t.touched && guard < 400) {
        t = System.getTouch();
        if (!t.touched) break;  // soltou: amostra sem toque vem com x/y=0
        if (Math.abs(t.y - startY) > 8) moved = true;
        if (moved) {
            scrollY = startScroll + (startY - t.y);
            clampScroll();
            drawList();
        }
        System.delay(20);
        guard++;
    }
    if (moved) {
        scrollY = Math.round(scrollY / PITCH) * PITCH;  // encaixa na linha
        clampScroll();
        return -1;
    }
    var idx = Math.floor((t0.y - LIST_Y + scrollY) / PITCH);
    if (idx >= 0 && idx < apps.length) {
        waitRelease();
        return idx;
    }
    return -1;
}
function drawListFooter() {
    footerVoltar();
    System.fillRoundRect(140, 282, 92, 30, 8, T.raised);
    System.drawRoundRect(140, 282, 92, 30, 8, T.stroke);
    ctext("Atualizar", 186, 297, 1, T.text, T.raised);
}
function loadCache() {
    var body = FS.readTextFile(CACHE);
    if (!body) return false;
    var arr = null;
    try { arr = JSON.parse(body); } catch (e) { arr = null; }
    if (!arr || !arr.length) return false;
    apps = arr;
    scanLocalApps();  // estado instalado/atualizacao vem sempre do disco
    countUpdates();
    sortByUpdate();
    scrollY = 0;
    sel = -1;
    return true;
}

function screenList() {
    drawList();
    while (true) {
        var t = System.getTouch();  // canto sup. direito dispara OS_EXIT
        if (t.touched) {
            if (hit(t, 8, 282, 84, 30)) {
                waitRelease();
                return "exit";
            } else if (hit(t, 140, 282, 92, 30)) {
                waitRelease();
                return Net.isConnected() ? "load" : "wifi";
            } else if (t.y >= LIST_Y && t.y < LIST_END) {
                var r = listDrag(t);
                if (r >= 0) {
                    sel = r;
                    return "detail";
                }
                drawList();
            } else {
                waitRelease();
            }
        }
        System.delay(20);
    }
}

// ---- tela: detalhe do app ---------------------------------------------------
function drawDetail() {
    var it = apps[sel];
    System.fillScreen(T.bg);
    header(truncLine(it.name, 200, 2));

    System.fillRoundRect(8, 50, 224, 92, 10, T.card);
    System.drawRoundRect(8, 50, 224, 92, 10, T.stroke);

    var loc = localMap[it.pkg];
    var remote = "v" + it.ver + (it.size ? " (" + fmtKB(it.size) + ")" : "");
    var rows = [
        ["Autor", truncLine(it.author, 140, 1)],
        ["Local", loc ? ("v" + (loc.ver || "?")) : "nao instalado"],
        ["Remota", remote]
    ];
    var yy = 60;
    for (var i = 0; i < rows.length; i++) {
        System.setTextColor(T.textDim, T.card);
        System.drawString(rows[i][0], 20, yy, 1);
        System.setTextColor(T.text, T.card);
        System.drawString(rows[i][1], 80, yy, 1);
        yy += 20;
    }
    var st = stateInfo(it);
    System.setTextColor(T.textDim, T.card);
    System.drawString("Estado", 20, yy, 1);
    System.setTextColor(st.col, T.card);
    System.drawString(st.txt, 80, yy, 1);

    // descricao: 4 linhas livres; com novidades, 2 da desc + 3 do changelog
    var news = it.changelog && (st.code === "upd" || st.code === "inst");
    System.setTextColor(T.textDim, T.bg);
    System.drawString("Descricao", 12, 154, 1);
    var lines = wrapLines(it.desc, 216, 1, news ? 2 : 4);
    var y2 = 168;
    for (var k = 0; k < lines.length; k++) {
        System.setTextColor(T.text, T.bg);
        System.drawString(lines[k], 12, y2, 1);
        y2 += 13;
    }

    if (news) {
        System.setTextColor(T.textDim, T.bg);
        System.drawString("Novidades", 12, 198, 1);
        var nlines = wrapLines(it.changelog, 216, 1, 3);
        var y3 = 210;
        for (var j = 0; j < nlines.length; j++) {
            System.setTextColor(T.text, T.bg);
            System.drawString(nlines[j], 12, y3, 1);
            y3 += 13;
        }
    }

    if (it.api > API) {
        ctext("Requer API " + it.api + " (sistema: " + API + ")", 120, 254, 1, T.warn, T.bg);
    } else {
        var lbl = "Instalar";
        if (st.code === "upd") lbl = "Atualizar";
        else if (st.code === "inst") lbl = "Reinstalar";
        System.fillRoundRect(56, 236, 128, 36, 8, T.accent);
        ctext(lbl, 120, 254, 2, T.onAccent, T.accent);
    }
    footerVoltar();
}
function screenDetail() {
    if (sel < 0 || sel >= apps.length) return "list";
    drawDetail();
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 8, 282, 84, 30)) {
                waitRelease();
                return "list";
            } else if (hit(t, 56, 236, 128, 36) && apps[sel].api <= API) {
                waitRelease();
                return "install";
            } else {
                waitRelease();
            }
        }
        System.delay(20);
    }
}

// ---- instalacao / atualizacao ----------------------------------------------
function drawDownload(name, sub) {
    System.fillScreen(T.bg);
    header("App Store");
    ctext(truncLine("Baixando " + name + "...", 216, 2), 120, 96, 2, T.text, T.bg);
    if (sub) ctext(sub, 120, 124, 1, T.textDim, T.bg);
}
function drawProgress(name, got, total) {
    System.fillScreen(T.bg);
    header("App Store");
    ctext(truncLine("Baixando " + name + "...", 216, 2), 120, 96, 2, T.text, T.bg);
    var gotKB = Math.floor(got / 1024);
    System.drawRoundRect(40, 126, 160, 16, 6, T.stroke);
    if (total > 0) {
        var pct = got / total;
        if (pct > 1) pct = 1;
        if (pct > 0.02) System.fillRect(43, 129, Math.floor(154 * pct), 10, T.accent);
        ctext(Math.floor(pct * 100) + "%", 120, 160, 1, T.textDim, T.bg);
    } else if (gotKB > 0) {
        // sem Content-Length: preenchimento indeterminado + KB baixados
        System.fillRect(43, 129, Math.min(154, 20 + (gotKB % 7) * 18), 10, T.accent);
        ctext(gotKB + " KB", 120, 160, 1, T.textDim, T.bg);
    }
}
// Update in-place: main.js novo entra como <pkg>/main.js.new (staging de um
// arquivo dentro do proprio pacote — littlefs/CYD nao tem espaco para duas
// copias do app), confere o MD5 do catalogo e so entao rename por cima do
// antigo; app.json e icon.png vem depois. Falha no meio deixa a versao
// instalada intacta (retry resolve).
function installApp() {
    var it = apps[sel];
    retryMode = "install";
    var fail = "";
    wasUpdate = stateInfo(it).code === "upd";

    var root = FS.exists(FLAG_SD) ? "/sd/apps" : "/local/apps";
    var dir = root + "/" + it.pkg;
    var tmp = dir + "/main.js.new";

    if (!Net.isConnected()) {
        fail = "Sem conexao WiFi";
    } else {
        // espaco: main.js novo + app.json + folga pro icon.png
        var need = (it.size || 0) + 16384;
        var free = 0;
        try { free = FS.getFreeSpace(root); } catch (e) { free = 0; }
        if (free > 0 && free < need) fail = "Sem espaco no disco";
    }

    // app.json do pacote (pequeno; preserva os campos do dev na instalacao)
    var json = "";
    if (!fail) {
        drawDownload(it.name, "app.json");
        System.delay(30);
        json = fetchText(it.metaUrl);
        if (!json) fail = "Erro ao baixar app.json";
    }

    // staging: garante a pasta antes do download do codigo
    if (!fail && !FS.isDirectory(root) && !FS.mkdir(root)) fail = "Erro no disco";
    if (!fail && !FS.isDirectory(dir) && !FS.mkdir(dir)) fail = "Erro no disco";

    // main.js direto para .new (streaming, barra de progresso por KB)
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

    // integridade: MD5 que o hub calculou no publish (quando presente)
    if (!fail && it.md5) {
        var md = "";
        try { md = FS.getFileMD5(tmp); } catch (e2) { md = ""; }
        if (md !== it.md5) {
            FS.deleteFile(tmp);
            fail = "Verificacao falhou (md5)";
        }
    }

    // troca atomica do codigo; metadados por cima; icone best-effort
    if (!fail && !FS.renameFile(tmp, dir + "/main.js")) {
        FS.deleteFile(tmp);
        fail = "Erro ao gravar main.js";
    }
    if (!fail && !FS.writeTextFile(dir + "/app.json", json)) {
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
            FS.deleteFile(dir + "/icon.png");  // versao nova nao tem icone
        }
    }

    if (fail) {
        errMsg = fail;
        errHint = it.name;
        return "err";
    }
    System.rescanApps();  // launcher re-le a lista de apps
    localMap[it.pkg] = { ver: it.ver, root: root };
    countUpdates();
    sortByUpdate();
    return "done";
}
function screenDone() {
    var it = apps[sel];
    System.fillScreen(T.bg);
    header("App Store");
    ctext(wasUpdate ? "Atualizado!" : "Instalado!", 120, 100, 2, T.ok, T.bg);
    ctext(truncLine(it.name, 216, 2), 120, 128, 2, T.text, T.bg);
    ctext(wasUpdate ? ("agora v" + it.ver) : ("v" + it.ver), 120, 150, 1, T.textDim, T.bg);
    var root = localMap[it.pkg] ? localMap[it.pkg].root : "";
    if (root) ctext("em " + root, 120, 166, 1, T.textDim, T.bg);
    ctext("App pronto no launcher.", 120, 184, 1, T.textDim, T.bg);
    footerVoltar();
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 8, 282, 84, 30)) {
                waitRelease();
                return "list";
            }
            waitRelease();
        }
        System.delay(20);
    }
}

// ---- tela: erro -------------------------------------------------------------
function screenErr() {
    System.fillScreen(T.bg);
    header("App Store");
    ctext("Erro", 120, 92, 2, T.err, T.bg);
    var lines = wrapLines(errMsg, 216, 2, 2);
    var y = 118;
    for (var i = 0; i < lines.length; i++) {
        ctext(lines[i], 120, y, 2, T.text, T.bg);
        y += 20;
    }
    if (errHint) ctext(truncLine(errHint, 216, 1), 120, y + 6, 1, T.textDim, T.bg);

    System.fillRoundRect(24, 200, 192, 36, 8, T.accent);
    ctext("Tentar de novo", 120, 218, 2, T.onAccent, T.accent);
    footerVoltar();
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 24, 200, 192, 36)) {
                waitRelease();
                if (retryMode === "install") return "install";
                return "load";
            } else if (hit(t, 8, 282, 84, 30)) {
                waitRelease();
                if (retryMode === "install") return "list";
                return "exit";
            } else {
                waitRelease();
            }
        }
        System.delay(20);
    }
}

// ---- tela: sem WiFi ---------------------------------------------------------
function screenWifi() {
    while (true) {
        System.fillScreen(T.bg);
        header("App Store");
        ctext("WiFi desconectado", 120, 108, 2, T.warn, T.bg);
        ctext("Conecte o WiFi para usar", 120, 138, 1, T.textDim, T.bg);
        ctext("a loja de apps.", 120, 154, 1, T.textDim, T.bg);
        footerVoltar();
        var last = System.millis();
        var go = false;
        while (System.millis() - last < 1500) {  // reconectou? segue
            var t = System.getTouch();
            if (t.touched) {
                if (hit(t, 8, 282, 84, 30)) {
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
// puras e de instalacao para os checks de update.
if (typeof __harness !== "undefined" && __harness.storeTest) {
    __harness.storeTest({
        cmpV: cmpV,
        stateInfo: stateInfo,
        scanLocalApps: scanLocalApps,
        updCount: function () { return updCount; },
        refresh: function () { countUpdates(); sortByUpdate(); },
        setCatalog: function (arr) {
            apps = arr;
            sel = 0;
            countUpdates();  // mesmo pipeline do loadCatalog/loadCache
            sortByUpdate();
        },
        catalog: function () { return apps; },
        installApp: installApp,
        fmtKB: fmtKB
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
        else System.exitApp();  // "exit"
    }
}
