// CelerOS App Store — app de sistema (W8). Porte do AppStoreUI.cpp: le o
// catalogo do CelerOS Hub (os.celer.tec.br; indice + categorias), lista os
// apps com estado vs instalado (novo / atualizacao / instalado), abre o
// detalhe (descricao, versao local x remota, API exigida) e instala direto
// baixando app.json + main.js para /local/apps ou /sd/apps (flag
// config_install_sd.txt), com rescan do launcher no fim. Net.get e bloqueante
// e sem progresso: tela "Baixando..." antes de cada chamada. X no canto sup.
// direito sai.

var INDEX_URL = "https://os.celer.tec.br/store/index.json";
var FLAG_SD = "/local/config_install_sd.txt";
// Catalogo da ultima carga bem-sucedida: a loja abre na hora (ate offline) e
// so vai a rede no "Atualizar" — a carga completa faz 1 HTTPS por app (~40 s).
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
function header(title) {
    System.fillRoundRect(0, 0, 240, 40, 0, T.card);
    System.setTextColor(T.text, T.card);
    System.drawString(title, 12, 12, 2);
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
    if (out.length > 0) out[out.length - 1] = truncLine(out[out.length - 1], maxw, f);
    return out;
}

// ---- rede (Net bloqueante; qualquer erro vira null) -----------------------
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
var apps = [];       // catalogo: [{id,pkg,metaUrl,appUrl,name,desc,author,ver,api}]
var localMap = {};   // packageName -> {ver, root}
var scrollY = 0;
var sel = -1;        // indice do app aberto no detalhe
var errMsg = "";
var errHint = "";
var retryMode = "";  // "load" | "install"

var LIST_Y = 50, LIST_END = 272, ROW_H = 44, PITCH = 50, VIS = 4;

// mapia os apps instalados (/local/apps e /sd/apps) por packageName
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

function drawLoading(msg, sub) {
    System.fillScreen(T.bg);
    header("App Store");
    ctext(truncLine(msg, 216, 2), 120, 130, 2, T.text, T.bg);
    if (sub) ctext(truncLine(sub, 216, 1), 120, 155, 1, T.textDim, T.bg);
}

// ---- carga do catalogo (indice -> categorias -> meta de cada app) ----------
function loadCatalog() {
    scanLocalApps();
    apps = [];
    scrollY = 0;
    sel = -1;
    retryMode = "load";

    drawLoading("Baixando catalogo...", "");
    System.delay(30);  // devolve o controle e da chance ao GC
    var idx = fetchJSON(INDEX_URL);
    if (!idx || !idx.categories) {
        errMsg = "Falha ao baixar o catalogo";
        errHint = "Verifique a internet.";
        return "err";
    }

    // entradas de todas as categorias do indice (mesmas URLs do original)
    var entries = [];
    var seenMeta = {};
    var seenPkg = {};
    var catTotal = 0, catOk = 0;
    var cats = idx.categories;
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
            if (!e || !e.meta || !e.app) continue;
            if (seenMeta[e.meta]) continue;  // duplicado no catalogo
            seenMeta[e.meta] = 1;
            if ((e.api || 1) > API) continue;  // filtra como no original
            entries.push({ id: id, meta: e.meta, app: e.app });
        }
    }
    if (entries.length === 0 && catTotal > 0 && catOk === 0) {
        errMsg = "Falha ao baixar o catalogo";
        errHint = "Verifique a internet.";
        return "err";
    }

    // meta de cada app (nome/versao/descricao p/ listar e comparar)
    var n = entries.length;
    for (var k = 0; k < n; k++) {
        var en = entries[k];
        drawLoading("Carregando apps...", (k + 1) + "/" + n + "  " + en.id);
        System.delay(30);
        var m = fetchJSON(en.meta);
        if (!m) continue;  // meta fora do ar: nao lista
        var pkg = m.packageName || en.id;
        if (seenPkg[pkg]) continue;
        seenPkg[pkg] = 1;
        apps.push({
            id: en.id,
            pkg: pkg,
            metaUrl: en.meta,
            appUrl: en.app,
            name: m.name || en.id,
            desc: m.description || "",
            author: m.author || "Desconhecido",
            ver: m.version || "1.0.0",
            api: m.api || 1
        });
    }

    // ordena por nome (igual ao original)
    apps.sort(function (a, b) {
        var x = a.name.toLowerCase(), y = b.name.toLowerCase();
        return x < y ? -1 : (x > y ? 1 : 0);
    });
    if (apps.length > 0) FS.writeTextFile(CACHE, JSON.stringify(apps));
    return "list";
}

// ---- tela: lista de apps ----------------------------------------------------
function maxScroll() {
    var m = (apps.length - VIS) * PITCH;
    return m > 0 ? m : 0;
}
function clampScroll() {
    var m = maxScroll();
    if (scrollY < 0) scrollY = 0;
    if (scrollY > m) scrollY = m;
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

    if (apps.length === 0) {
        ctext("Catalogo vazio", 120, 120, 2, T.text, T.bg);
        ctext("Nenhum app encontrado.", 120, 148, 1, T.textDim, T.bg);
        drawListFooter();
        return;
    }

    var maxS = maxScroll();
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
    if (maxS > 0) {
        System.drawFastVLine(235, LIST_Y, LIST_END - LIST_Y, T.stroke);
        var trackH = LIST_END - LIST_Y;
        var thH = Math.max(24, Math.floor(trackH * trackH / (apps.length * PITCH)));
        var ty = LIST_Y + Math.floor((trackH - thH) * scrollY / maxS);
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
    var rows = [
        ["Autor", truncLine(it.author, 140, 1)],
        ["Local", loc ? ("v" + (loc.ver || "?")) : "nao instalado"],
        ["Remota", "v" + it.ver]
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

    System.setTextColor(T.textDim, T.bg);
    System.drawString("Descricao", 12, 154, 1);
    var lines = wrapLines(it.desc, 216, 1, 4);
    var y2 = 170;
    for (var k = 0; k < lines.length; k++) {
        System.setTextColor(T.text, T.bg);
        System.drawString(lines[k], 12, y2, 1);
        y2 += 13;
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

// ---- instalacao -------------------------------------------------------------
function drawDownload(name, sub) {
    System.fillScreen(T.bg);
    header("App Store");
    ctext(truncLine("Baixando " + name + "...", 216, 2), 120, 126, 2, T.text, T.bg);
    if (sub) ctext(sub, 120, 152, 1, T.textDim, T.bg);
    ctext("aguarde...", 120, 168, 1, T.textDim, T.bg);
}
// baixa tudo antes de apagar a instalacao anterior (falha nao deixa sujeira)
function installApp() {
    var it = apps[sel];
    retryMode = "install";
    var fail = "";

    if (!Net.isConnected()) {
        fail = "Sem conexao WiFi";
    } else {
        drawDownload(it.name, "app.json");
        System.delay(30);
        var json = fetchText(it.metaUrl);
        if (!json) fail = "Erro ao baixar app.json";

        var js = "";
        if (!fail) {
            drawDownload(it.name, "main.js");
            System.delay(30);
            js = fetchText(it.appUrl);
            if (!js) fail = "Erro ao baixar main.js";
        }

        if (!fail) {
            var root = FS.exists(FLAG_SD) ? "/sd/apps" : "/local/apps";
            var dir = root + "/" + it.pkg;
            if (FS.isDirectory(dir)) FS.removeDirectory(dir);  // reinstala limpo
            if (!FS.isDirectory(root) && !FS.mkdir(root)) fail = "Erro no disco";
            if (!fail && !FS.mkdir(dir)) fail = "Erro no disco";
            if (!fail && !FS.writeTextFile(dir + "/app.json", json)) fail = "Erro ao gravar app.json";
            if (!fail && !FS.writeTextFile(dir + "/main.js", js)) fail = "Erro ao gravar main.js";
            if (!fail) {
                System.rescanApps();  // launcher rele a lista de apps
                localMap[it.pkg] = { ver: it.ver, root: root };
                return "done";
            }
        }
    }

    errMsg = fail;
    errHint = it.name;
    return "err";
}
function screenDone() {
    var it = apps[sel];
    System.fillScreen(T.bg);
    header("App Store");
    ctext("Instalado!", 120, 108, 2, T.ok, T.bg);
    ctext(truncLine(it.name, 216, 2), 120, 136, 2, T.text, T.bg);
    var root = localMap[it.pkg] ? localMap[it.pkg].root : "";
    if (root) ctext("em " + root, 120, 162, 1, T.textDim, T.bg);
    ctext("App pronto no launcher.", 120, 180, 1, T.textDim, T.bg);
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
