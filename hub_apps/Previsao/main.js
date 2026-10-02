// Previsao — previsão do tempo via Open-Meteo (API 16)
// ES5 puro (Duktape). Busca o tempo atual + 4 dias, grava o cache no appData
// e instala o plugin do watchface (appData/watchface.js) na primeira
// execução — o relógio passa a mostrar a linha do tempo e o toque nela abre
// este app (System.launchApp, API 16). Sem WiFi, cai para o clima do
// celular (Gadgetbridge), como o app Clima do sistema.

var T = System.theme();
var INFO = {};
try { INFO = System.getInfo() || {}; } catch (e) { INFO = {}; }
// vidro grande (watch/4"): digito grande na fonte 8; senao a 24 px (4)
var BIGF = (INFO.screenW || 240) >= 400 ? 8 : 4;
var HAS_PHONE = typeof Phone !== "undefined" && Phone && typeof Phone.weather === "function";

var DATA = FS.appData();              // "/local/data/celeros.previsao/"
var CACHE = DATA + "forecast.json";
var PLUGIN = DATA + "watchface.js";

var CIDADES = [
    { n: "Sao Paulo", la: -23.55, lo: -46.64 },
    { n: "Rio de Janeiro", la: -22.91, lo: -43.17 },
    { n: "Belo Horizonte", la: -19.92, lo: -43.94 },
    { n: "Curitiba", la: -25.43, lo: -49.27 },
    { n: "Porto Alegre", la: -30.03, lo: -51.23 },
    { n: "Brasilia", la: -15.79, lo: -47.88 },
    { n: "Salvador", la: -12.97, lo: -38.5 },
    { n: "Manaus", la: -3.12, lo: -60.02 }
];
var DIA = ["DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"];

// ---- cidade escolhida (Storage: indice, ou "@lat,lon|Nome" p/ custom) ------
var sel = 0;
var custom = null;
try {
    var sv = Storage.get("city", "0");
    if (sv && sv.charAt(0) === "@") {
        var pp = sv.substring(1).split("|");
        var ll = pp[0].split(",");
        custom = { n: pp[1] || "Custom", la: parseFloat(ll[0]), lo: parseFloat(ll[1]) };
        if (!(isFinite(custom.la) && isFinite(custom.lo))) { custom = null; sel = 0; }
        else sel = -1;
    } else {
        sel = parseInt(sv, 10) || 0;
        if (!(sel >= 0 && sel < CIDADES.length)) sel = 0;
    }
} catch (e) { sel = 0; }

function cidade() { return sel >= 0 ? CIDADES[sel] : custom; }
function salvaCidade() {
    try {
        if (sel >= 0) Storage.set("city", String(sel));
        else Storage.set("city", "@" + custom.la + "," + custom.lo + "|" + custom.n);
    } catch (e) {}
}

// ---- codigo WMO -> texto pt -------------------------------------------------
function wmo(c) {
    if (c === 0 || c === 1) return "Sol";
    if (c === 2) return "Parcial";
    if (c === 3) return "Nublado";
    if (c === 45 || c === 48) return "Nevoeiro";
    if (c >= 51 && c <= 57) return "Garoa";
    if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return "Chuva";
    if ((c >= 71 && c <= 77) || c === 85 || c === 86) return "Neve";
    if (c >= 95) return "Tempestade";
    return "-";
}

function ctext(s, y, f, col) {
    System.setTextColor(col);
    System.drawString(s, Math.round((240 - System.textWidth(s, f)) / 2), y, f);
}

// ---- cache -------------------------------------------------------------------
var d = null;
var msg = "";
function leCache() {
    try {
        var s = FS.readTextFile(CACHE);
        d = s ? JSON.parse(s) : null;
    } catch (e) { d = null; }
}
function gravaCache() {
    try { FS.writeTextFile(CACHE, JSON.stringify(d)); } catch (e) {}
}
function horaAgora() {
    try { return parseInt(System.getTime().split(":")[0], 10) || 0; } catch (e) { return 0; }
}
// o firmware nao expoe epoch aos apps: staleness = data/hora do cache
function velho() {
    if (!d || !d.date) return true;
    try {
        if (System.getDate() !== d.date) return true;
        var hh = (horaAgora() - d.hour + 24) % 24;
        return hh >= 3;
    } catch (e) { return true; }
}

// ---- busca (Open-Meteo; fallback Phone.weather) -------------------------------
function busca() {
    var c = cidade();
    msg = "Atualizando...";
    draw();
    var url = "https://api.open-meteo.com/v1/forecast?latitude=" + c.la + "&longitude=" + c.lo +
        "&current=temperature_2m,relative_humidity_2m,weather_code" +
        "&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=4&timezone=auto";
    var r = null;
    try { r = Net.getJSON(url); } catch (e) { r = null; }
    if (r && r.current && r.daily) {
        d = {
            v: 1, city: c.n, t: Math.round(r.current.temperature_2m),
            hum: typeof r.current.relative_humidity_2m === "number" ? r.current.relative_humidity_2m : -1,
            txt: wmo(r.current.weather_code),
            date: System.getDate(), hour: horaAgora(), days: []
        };
        var dl = r.daily;
        for (var i = 0; i < dl.time.length && i < 4; i++) {
            d.days.push({
                dt: dl.time[i], tx: Math.round(dl.temperature_2m_max[i]),
                tn: Math.round(dl.temperature_2m_min[i]), txt: wmo(dl.weather_code[i])
            });
        }
        gravaCache();
        msg = "";
        return true;
    }
    if (HAS_PHONE) {
        try {
            var w = Phone.weather();
            if (w && typeof w.temp === "number") {
                d = {
                    v: 1, city: w.loc || c.n, t: Math.round(w.temp),
                    hum: typeof w.hum === "number" ? w.hum : -1, txt: w.txt || "",
                    date: System.getDate(), hour: horaAgora(), days: []
                };
                gravaCache();
                msg = "";
                return true;
            }
        } catch (e2) { }
    }
    msg = "Sem conexao";
    return false;
}

function diaLabel(iso, i) {
    if (i === 0) return "Hoje";
    if (i === 1) return "Amanha";
    try {
        var y = parseInt(iso.substring(0, 4), 10), m = parseInt(iso.substring(5, 7), 10),
            dd = parseInt(iso.substring(8, 10), 10);
        return DIA[new Date(y, m - 1, dd).getDay()];
    } catch (e) { return "DIA"; }
}

// ---- plugin do watchface (fonte unica; escrita no appData na 1a execucao) -----
var PLUGIN_SRC = [
    "// Plugin Previsao do watchface - instalado pelo app celeros.previsao.",
    "// Le o cache do app e ocupa uma linha na banda de widgets do relogio.",
    "var T = System.theme();",
    "var CACHE = \"/local/data/celeros.previsao/forecast.json\";",
    "var d = null, lido = -60000;",
    "function wmo(c) {",
    "    if (c === 0 || c === 1) return \"Sol\";",
    "    if (c === 2) return \"Parcial\";",
    "    if (c === 3) return \"Nublado\";",
    "    if (c === 45 || c === 48) return \"Nevoeiro\";",
    "    if (c >= 51 && c <= 57) return \"Garoa\";",
    "    if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return \"Chuva\";",
    "    if ((c >= 71 && c <= 77) || c === 85 || c === 86) return \"Neve\";",
    "    if (c >= 95) return \"Tempestade\";",
    "    return \"-\";",
    "}",
    "function le() {",
    "    if (d && System.millis() - lido < 60000) return;",
    "    lido = System.millis();",
    "    try {",
    "        var s = FS.readTextFile(CACHE);",
    "        d = s ? JSON.parse(s) : null;",
    "    } catch (e) { d = null; }",
    "}",
    "function velho() {",
    "    if (!d || !d.date) return true;",
    "    try {",
    "        if (System.getDate() !== d.date) return true;",
    "        var h = parseInt(System.getTime().split(\":\")[0], 10) || 0;",
    "        return ((h - d.hour + 24) % 24) >= 3;",
    "    } catch (e) { return true; }",
    "}",
    "return {",
    "    id: \"celeros.previsao\",",
    "    sig: function () {",
    "        le();",
    "        if (!d) return \"no\";",
    "        return [d.t, d.txt, d.tn, d.tx, velho() ? 1 : 0].join(\"|\");",
    "    },",
    "    line: function () {",
    "        le();",
    "        if (!d) return \"Previsao: abra o app\";",
    "        return Math.round(d.t) + \"C \" + d.txt + \" \" + d.tn + \"/\" + d.tx;",
    "    },",
    "    draw: function (x, y, w, h, bg) {",
    "        le();",
    "        if (!d) {",
    "            System.setTextColor(T.textDim);",
    "            System.drawString(\"Previsao: abra o app\", x + 14, y + 3, 2);",
    "            return;",
    "        }",
    "        var col = velho() ? T.textDim : T.text;",
    "        var s1 = Math.round(d.t) + \"C \" + d.txt;",
    "        var s2 = d.tn + \"/\" + d.tx;",
    "        System.setTextColor(col);",
    "        System.drawString(s1, x + 14, y + 3, 2);",
    "        System.setTextColor(T.textDim);",
    "        System.drawString(s2, x + 14 + System.textWidth(s1, 2) + 10, y + 3, 2);",
    "    },",
    "    open: \"celeros.previsao\"",
    "};"
].join("\n");

function instalaPlugin() {
    try {
        if (FS.readTextFile(PLUGIN) === PLUGIN_SRC) return;
        FS.writeTextFile(PLUGIN, PLUGIN_SRC);
    } catch (e) { }
}

// ---- UI -----------------------------------------------------------------------
var BTN = { upd: { x: 20, y: 276, w: 112, h: 32 }, city: { x: 144, y: 276, w: 76, h: 32 } };
var view = "main";  // main | cidades

function button(c, label) {
    var pr = c.pressed;
    System.fillRoundRect(c.x, c.y, c.w, c.h, 10, pr ? T.raised : T.card);
    System.setTextColor(pr ? T.accent : T.text);
    System.drawString(label, c.x + Math.round((c.w - System.textWidth(label, 2)) / 2), c.y + 8, 2);
}

function drawMain() {
    System.fillRect(0, 0, 240, 320, T.bg);
    var c = cidade();
    ctext(c ? c.n : "?", 40, 2, T.textDim);
    if (!d) {
        ctext("Sem dados ainda", 120, 2, T.textDim);
        ctext(msg || "Toque em Atualizar", 152, 1, T.stroke);
    } else {
        ctext(d.t + " C", 76, BIGF, d.t >= 30 ? T.warn : T.text);
        ctext(d.txt, BIGF === 8 ? 168 : 140, 2, T.accent);
        if (d.hum >= 0) ctext("umidade " + d.hum + "%", BIGF === 8 ? 204 : 176, 1, T.textDim);
        // proximos dias
        var rowsTop = BIGF === 8 ? 216 : 206;
        var rowStep = BIGF === 8 ? 16 : 20;
        for (var i = 1; i < d.days.length && i < 4; i++) {
            var f = d.days[i];
            var y = rowsTop + (i - 1) * rowStep;
            System.setTextColor(T.textDim);
            System.drawString(diaLabel(f.dt, i), 28, y, 1);
            var mm = f.tn + "/" + f.tx;
            System.setTextColor(T.text);
            System.drawString(mm, 120 - Math.round(System.textWidth(mm, 1) / 2), y, 1);
            System.setTextColor(T.textDim);
            System.drawString(f.txt, 212 - System.textWidth(f.txt, 1), y, 1);
        }
    }
    if (msg) ctext(msg, 258, 1, msg === "Sem conexao" ? T.err : T.stroke);
    else if (d && BIGF !== 8) ctext(velho() ? "dados de " + d.date : "de hoje, " + d.hour + "h", 258, 1, T.stroke);
    BTN.upd.pressed = false;
    BTN.city.pressed = false;
    button(BTN.upd, "Atualizar");
    button(BTN.city, "Cidade");
}

function rowsCidades() {
    var rows = [];
    for (var i = 0; i < CIDADES.length; i++) rows.push(CIDADES[i].n);
    rows.push("Outra (lat,lon)");
    return rows;
}

function drawCidades() {
    System.fillRect(0, 0, 240, 320, T.bg);
    ctext("Cidade", 30, 2, T.textDim);
    var rows = rowsCidades();
    for (var i = 0; i < rows.length; i++) {
        var y = 64 + i * 24;
        var on = (sel === i) || (sel === -1 && i === rows.length - 1);
        System.setTextColor(on ? T.accent : T.text);
        System.drawString((on ? "> " : "") + rows[i], 40, y, 2);
    }
    ctext("toque numa cidade para escolher", 296, 1, T.stroke);
}

function draw() {
    if (view === "cidades") drawCidades();
    else drawMain();
}

function tapMain(x, y) {
    function in_(r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
    if (in_(BTN.upd)) { busca(); return; }
    if (in_(BTN.city)) { view = "cidades"; draw(); return; }
}

function tapCidades(y) {
    var rows = rowsCidades();
    var i = Math.floor((y - 64) / 24);
    if (!(i >= 0 && i < rows.length)) return;
    if (i === rows.length - 1) {
        var s = "";
        try { s = System.prompt("Latitude,longitude:", "-23.55,-46.64") || ""; } catch (e) { s = ""; }
        if (!s) { draw(); return; }
        var ll = s.split(",");
        var la = parseFloat(ll[0]), lo = parseFloat(ll[1] || "0");
        if (!(isFinite(la) && isFinite(lo))) { msg = "Coordenada invalida"; draw(); return; }
        custom = { n: "Custom", la: la, lo: lo };
        sel = -1;
    } else {
        sel = i;
    }
    salvaCidade();
    view = "main";
    d = null;
    busca();
}

// ---- laco -----------------------------------------------------------------------
instalaPlugin();
leCache();
if (!d || velho()) busca();  // cache frio: ja atualiza ao abrir
draw();

var ty0 = -1, tlX = -1, tlY = -1, moved = false;
while (true) {
    var tp = System.getTouch();
    if (tp.touched) {
        if (ty0 < 0) { ty0 = tp.y; tlX = tp.x; tlY = tp.y; moved = false; }
        if (Math.abs(tp.y - ty0) > 12) moved = true;
        tlX = tp.x; tlY = tp.y;
    } else if (ty0 >= 0) {
        if (!moved) {
            if (view === "cidades") tapCidades(tlY);
            else tapMain(tlX, tlY);
        }
        ty0 = -1;
    }
    System.delay(250);
}
