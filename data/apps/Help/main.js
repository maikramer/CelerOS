// CelerOS Help (Central de Ajuda) — app de sistema. Porte do HelpCenterUI.cpp
// (commit 64ccfc8): menu Offline/Online, categorias e topicos offline (textos
// originais resumidos em PT-BR), ajuda online buscada no GitHub via Net.getJSON
// (indice -> categoria -> artigo) e leitor com rolagem por arraste.
// X no canto sup. direito sai (exit automatico do OS).

var T = System.theme();

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
    // titulo na faixa do sistema (API 6) — sem cabecalho desenhado
    if (System.topbarText) System.topbarText(title);
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
function drawBack() {
    System.fillRoundRect(8, 282, 84, 30, 8, T.raised);
    System.drawRoundRect(8, 282, 84, 30, 8, T.stroke);
    ctext("< Voltar", 50, 297, 2, T.text, T.raised);
}
// corta com "..." se nao couber em maxW
function fitText(s, font, maxW) {
    if (System.textWidth(s, font) <= maxW) return s;
    var r = s;
    while (r.length > 1 && System.textWidth(r + "...", font) > maxW) {
        r = r.substring(0, r.length - 1);
    }
    return r + "...";
}
// quebra texto em linhas por largura medida (respeita \n e palavras longas)
function wrapText(s, font, maxW) {
    var out = [];
    var paras = String(s).replace(/\r/g, "").split("\n");
    for (var p = 0; p < paras.length; p++) {
        var words = paras[p].split(" ");
        var line = "";
        for (var w = 0; w < words.length; w++) {
            var word = words[w];
            while (System.textWidth(word, font) > maxW) {
                var cut = word.length - 1;
                while (cut > 1 && System.textWidth(word.substring(0, cut), font) > maxW) cut--;
                if (line !== "") { out.push(line); line = ""; }
                out.push(word.substring(0, cut));
                word = word.substring(cut);
            }
            var test = line === "" ? word : line + " " + word;
            if (System.textWidth(test, font) <= maxW) {
                line = test;
            } else {
                out.push(line);
                line = word;
            }
        }
        out.push(line);
    }
    return out;
}

// ---- conteudo offline (porte do HelpCenterUI.cpp, PT-BR resumido) ----------
var CATS = [
    { name: "Primeiros Passos", topics: [
        { t: "O que é o CelerOS", d: "Visão do sistema",
          c: "O CelerOS é um sistema operacional rápido e leve, feito para o ESP32. Traz loja de apps integrada, execução de apps JavaScript pelo cartão SD e uma interface fluida no toque." },
        { t: "Como começar", d: "WiFi e cartão SD",
          c: "Vá em Configurações > WiFi para conectar o aparelho. Se for instalar novos apps de usuário, insira um cartão SD formatado em FAT32." },
        { t: "Tipos de app", d: "Sistema vs usuário",
          c: "Apps de sistema (Configurações, Launcher) rodam em C++ integrado, com máxima velocidade. Apps de usuário rodam no runtime JavaScript, do cartão SD ou da memória local." }
    ]},
    { name: "Navegação Básica", topics: [
        { t: "Tela inicial", d: "Visão da home",
          c: "A tela inicial mostra os apps de sistema no topo e os seus apps de usuário instalados abaixo. Toque ou arraste a lista para navegar." },
        { t: "Abrir e fechar", d: "Toque e botão X",
          c: "Toque no nome de um app para abri-lo. Para fechar um app em execução, toque no X do canto superior direito e volte ao launcher." }
    ]},
    { name: "Conectividade", topics: [
        { t: "Conectar WiFi", d: "Config > WiFi",
          c: "Vá em Configurações > WiFi. O aparelho escaneia as redes. Toque numa rede, digite a senha no teclado na tela e toque em Conectar. O aparelho reinicia para aplicar." }
    ]},
    { name: "Loja e Apps", topics: [
        { t: "App Store", d: "Instalar e atualizar",
          c: "A App Store lista os apps do CelerOS Hub. Toque num app para ver a descrição, o changelog e o tamanho antes de instalar. Apps instalados mostram a versão local e ganham o botão Atualizar quando o hub tem versão nova." },
        { t: "Criar seus apps", d: "SDK celer.js",
          c: "O SDK do CelerOS (npm celer) cria o projeto, roda o lint, testa no emulador e publica na loja: celer.js new meuapp, depois dev, test e publish. Apps são JavaScript ES5 no espaço virtual 240x320." }
    ]},
    { name: "APIs Recentes", topics: [
        { t: "Timers e notificações", d: "API 12",
          c: "Apps podem usar setTimeout e setInterval, gravar dados no Storage (NVS privado do app), desenhar em sprites fora da tela e avisar o usuário com System.notify (aparece no centro de notificações e na faixa do sistema) e System.playTone (melodia [[freq,ms],...])." },
        { t: "Sons e áudio", d: "API 12 e 13",
          c: "System.playTone toca melodias curtas no alto-falante. System.playWav toca arquivos WAV PCM 16-bit (8 a 48 kHz) de /local ou /sd. System.setVolume e getVolume ajustam o volume geral (API 13)." },
        { t: "Sensores e relógio", d: "API 13",
          c: "No watch, Sensors.accel le o acelerômetro, Sensors.steps conta passos e Sensors.temp a temperatura. System.getWeekday devolve o dia da semana, System.keepAwake(segundos) segura a tela acordada e o RTC mantém a hora entre reboots." }
    ]},
    { name: "Problemas Comuns", topics: [
        { t: "Tela preta", d: "Travamento em apps",
          c: "Se um app travar ou ficar sem memória, o sistema mostra uma tela de erro: toque para voltar ao launcher. Em placas sem PSRAM (como a CYD), apps muito grandes podem não caber; a loja marca esses apps como Requer PSRAM." }
    ]}
];

// ---- estado de lista --------------------------------------------------------
var L_title = "", L_items = [], L_sub = null, L_sel = 0, L_top = 0;
var LIST_X = 8, LIST_Y = 50, LIST_W = 224, ROW_H = 38, ROW_GAP = 6;
var ROW_PITCH = ROW_H + ROW_GAP;  // 44
var VIS_ROWS = 5;                 // linhas visiveis acima do rodape

function setList(title, items, subs) {
    L_title = title;
    L_items = items;
    L_sub = subs;
    L_sel = 0;
    L_top = 0;
}

function drawList() {
    System.fillScreen(T.bg);
    header(fitText(L_title, 2, 216));
    var count = L_items.length;
    var end = L_top + VIS_ROWS;
    if (end > count) end = count;
    for (var i = L_top; i < end; i++) {
        var y = LIST_Y + (i - L_top) * ROW_PITCH;
        var bgc = (i === L_sel) ? T.raised : T.card;
        System.fillRoundRect(LIST_X, y, LIST_W, ROW_H, 10, bgc);
        System.drawRoundRect(LIST_X, y, LIST_W, ROW_H, 10, T.stroke);
        if (i === L_sel) System.fillRect(LIST_X + 3, y + 6, 4, ROW_H - 12, T.accent);
        // texto transparente (a linha ja pintou o fundo): com fundo opaco a
        // caixa do subtitulo apagava cedilhas/descendentes do titulo
        System.setTextColor(T.text);
        System.drawString(fitText(L_items[i], 2, LIST_W - 42), LIST_X + 14, y + 7, 2);
        if (L_sub !== null && L_sub[i]) {
            System.setTextColor(T.textDim);
            System.drawString(L_sub[i], LIST_X + 14, y + 25, 1);
        }
    }
    if (count > VIS_ROWS) {
        var trackH = VIS_ROWS * ROW_PITCH - ROW_GAP;
        System.fillRect(236, LIST_Y, 3, trackH, T.stroke);
        var thumbH = Math.max(18, Math.floor(trackH * VIS_ROWS / count));
        var maxTop = count - VIS_ROWS;
        var thumbY = LIST_Y + Math.floor((trackH - thumbH) * L_top / maxTop);
        System.fillRect(236, thumbY, 3, thumbH, T.accent);
    }
    drawBack();
}

// loop de lista com rolagem por arraste; retorna indice escolhido ou -1 (Voltar)
function listLoop() {
    drawList();
    var dragY = -1, acc = 0, moved = 0, lx = 0, ly = 0;
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            lx = t.x;
            ly = t.y;
            if (dragY < 0) {
                dragY = t.y;
                acc = 0;
                moved = 0;
            } else {
                var dy = t.y - dragY;
                dragY = t.y;
                moved += dy < 0 ? -dy : dy;
                acc += dy;
                while (acc >= ROW_PITCH) {
                    if (L_top > 0) {
                        L_top--;
                        if (L_sel < L_top) L_sel = L_top;
                        drawList();
                    }
                    acc -= ROW_PITCH;
                }
                while (acc <= -ROW_PITCH) {
                    if (L_top < L_items.length - VIS_ROWS) {
                        L_top++;
                        if (L_sel >= L_top + VIS_ROWS) L_sel = L_top + VIS_ROWS - 1;
                        drawList();
                    }
                    acc += ROW_PITCH;
                }
                if (acc > ROW_PITCH) acc = ROW_PITCH;
                if (acc < -ROW_PITCH) acc = -ROW_PITCH;
            }
        } else if (dragY >= 0) {
            dragY = -1;
            acc = 0;
            if (moved < 12) {
                if (hit({ x: lx, y: ly }, 8, 282, 84, 30)) return -1;
                var row = Math.floor((ly - LIST_Y) / ROW_PITCH);
                var idx = L_top + row;
                if (ly >= LIST_Y && row >= 0 && row < VIS_ROWS && idx < L_items.length) {
                    L_sel = idx;
                    drawList();
                    System.delay(200);  // feedback do highlight
                    return idx;
                }
            }
        }
        System.delay(20);
    }
}

// ---- leitor de topico -------------------------------------------------------
var V_title = "", V_lines = [], V_top = 0;
var TXT_X = 12, TXT_Y = 52, TXT_W = 198, LINE_H = 14, VIS_LINES = 16;

function setViewer(title, content) {
    V_title = title;
    V_lines = wrapText(content, 1, TXT_W);
    V_top = 0;
}

function drawViewer() {
    System.fillScreen(T.bg);
    header(fitText(V_title, 2, 216));
    var end = V_top + VIS_LINES;
    if (end > V_lines.length) end = V_lines.length;
    System.setTextColor(T.text, T.bg);
    for (var i = V_top; i < end; i++) {
        if (V_lines[i] !== "") {
            System.drawString(V_lines[i], TXT_X, TXT_Y + (i - V_top) * LINE_H, 1);
        }
    }
    if (V_top > 0) System.fillTriangle(228, 56, 220, 64, 236, 64, T.accent);
    if (V_lines.length > V_top + VIS_LINES) System.fillTriangle(228, 272, 220, 264, 236, 264, T.accent);
    drawBack();
}

// loop do leitor com rolagem por arraste; sai pelo "< Voltar"
function viewerLoop() {
    drawViewer();
    var dragY = -1, acc = 0, moved = 0, lx = 0, ly = 0;
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            lx = t.x;
            ly = t.y;
            if (dragY < 0) {
                dragY = t.y;
                acc = 0;
                moved = 0;
            } else {
                var dy = t.y - dragY;
                dragY = t.y;
                moved += dy < 0 ? -dy : dy;
                acc += dy;
                while (acc >= LINE_H) {
                    if (V_top > 0) {
                        V_top--;
                        drawViewer();
                    }
                    acc -= LINE_H;
                }
                while (acc <= -LINE_H) {
                    if (V_top + VIS_LINES < V_lines.length) {
                        V_top++;
                        drawViewer();
                    }
                    acc += LINE_H;
                }
                if (acc > LINE_H) acc = LINE_H;
                if (acc < -LINE_H) acc = -LINE_H;
            }
        } else if (dragY >= 0) {
            dragY = -1;
            acc = 0;
            if (moved < 12 && hit({ x: lx, y: ly }, 8, 282, 84, 30)) return;
        }
        System.delay(20);
    }
}

// ---- menu principal (uiState 0 do original) --------------------------------
function drawMenu() {
    System.fillScreen(T.bg);
    header("Central de Ajuda");
    System.setTextColor(T.textDim, T.bg);
    System.drawString("Como usar o CelerOS:", 12, 56, 1);
    System.drawString("gestos, WiFi, apps e mais.", 12, 70, 1);
    System.fillRoundRect(20, 104, 200, 32, 8, T.accent);
    ctext("Ajuda Offline", 120, 120, 2, T.onAccent, T.accent);
    System.fillRoundRect(20, 152, 200, 32, 8, T.raised);
    System.drawRoundRect(20, 152, 200, 32, 8, T.stroke);
    ctext("Ajuda Online", 120, 168, 2, T.text, T.raised);
    System.setTextColor(T.textDim, T.bg);
    System.drawString("Online lê a central de ajuda", 12, 200, 1);
    System.drawString("no hub (precisa de WiFi).", 12, 214, 1);
}

// retorna 0 = offline, 1 = online
function menuLoop() {
    drawMenu();
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 20, 104, 200, 32)) { waitRelease(); return 0; }
            if (hit(t, 20, 152, 200, 32)) { waitRelease(); return 1; }
            waitRelease();
        }
        System.delay(20);
    }
}

// ---- ajuda online (CelerOS Hub, mesmo formato do HelpCenterUI.cpp) ---------
var IDX_URL = "https://os.celer.tec.br/help/index.json";
var errMsg = "";

function loading(msg) {
    System.fillScreen(T.bg);
    header("Ajuda Online");
    ctext(msg, 120, 140, 2, T.textDim, T.bg);
    ctext("Aguarde...", 120, 170, 1, T.textDim, T.bg);
}

// busca JSON com tratamento de erro; em falha retorna null e seta errMsg
function fetchJSON(url, msg) {
    loading(msg);
    if (!Net.isConnected()) {
        errMsg = "Ligue o WiFi primeiro!";
        return null;
    }
    var d = null;
    try {
        d = Net.getJSON(url);
    } catch (e) {
        errMsg = "Erro ao ler os dados.";
        return null;
    }
    if (d === null || d === undefined) {
        errMsg = "Sem conexão. Use a Ajuda Offline.";
        return null;
    }
    return d;
}

// dialogo de erro (uiState 6 do original)
function errorLoop() {
    System.fillScreen(T.bg);
    header("Erro");
    var lines = wrapText(errMsg, 2, 200);
    var boxH = lines.length * 22 + 40;
    if (boxH < 70) boxH = 70;
    var boxY = 100;
    System.fillRoundRect(20, boxY, 200, boxH, 10, T.card);
    System.drawRoundRect(20, boxY, 200, boxH, 10, T.stroke);
    var ty = boxY + 18;
    for (var i = 0; i < lines.length; i++) {
        ctext(lines[i], 120, ty + 8, 2, T.text, T.card);
        ty += 22;
    }
    System.fillRoundRect(85, 216, 70, 32, 8, T.raised);
    System.drawRoundRect(85, 216, 70, 32, 8, T.stroke);
    ctext("OK", 120, 232, 2, T.text, T.raised);
    drawBack();
    while (true) {
        var t = System.getTouch();
        if (t.touched) {
            if (hit(t, 85, 216, 70, 32) || hit(t, 8, 282, 84, 30)) {
                waitRelease();
                return;
            }
            waitRelease();
        }
        System.delay(20);
    }
}

// indice -> categoria -> artigo; erro em qualquer etapa volta ao menu
function onlineFlow() {
    var d = fetchJSON(IDX_URL, "Buscando índice...");
    if (d === null) { errorLoop(); return; }
    var catNames = [], catUrls = [];
    var cats = d.categories || [];
    for (var i = 0; i < cats.length && i < 25; i++) {
        catNames.push(String(cats[i].name));
        catUrls.push(String(cats[i].url));
    }
    if (catNames.length === 0) { errMsg = "Índice vazio."; errorLoop(); return; }
    while (true) {
        setList("Ajuda Online", catNames, null);
        var c = listLoop();
        if (c < 0) return;
        var a = fetchJSON(catUrls[c], "Carregando tópicos...");
        if (a === null) { errorLoop(); return; }
        var artTitles = [], artUrls = [];
        var arts = a.articles || [];
        for (var j = 0; j < arts.length && j < 25; j++) {
            artTitles.push(String(arts[j].title));
            artUrls.push(String(arts[j].url));
        }
        if (artTitles.length === 0) { errMsg = "Nenhum artigo."; errorLoop(); return; }
        while (true) {
            setList(catNames[c], artTitles, null);
            var k = listLoop();
            if (k < 0) break;
            var art = fetchJSON(artUrls[k], "Carregando artigo...");
            if (art === null) { errorLoop(); return; }
            var content = "Conteúdo não encontrado.";
            if (art.content !== undefined && art.content !== null) content = String(art.content);
            setViewer(artTitles[k], content);
            viewerLoop();
        }
    }
}

// ---- fluxo offline: categorias -> topicos -> leitor ------------------------
function offlineFlow() {
    while (true) {
        var names = [], subs = [];
        for (var i = 0; i < CATS.length; i++) {
            names.push(CATS[i].name);
            subs.push(CATS[i].topics.length + (CATS[i].topics.length === 1 ? " tópico" : " tópicos"));
        }
        setList("Ajuda Offline", names, subs);
        var c = listLoop();
        if (c < 0) return;
        var tp = CATS[c].topics;
        while (true) {
            var tnames = [], tsubs = [];
            for (var j = 0; j < tp.length; j++) {
                tnames.push(tp[j].t);
                tsubs.push(tp[j].d);
            }
            setList(CATS[c].name, tnames, tsubs);
            var k = listLoop();
            if (k < 0) break;
            setViewer(tp[k].t, tp[k].c);
            viewerLoop();
        }
    }
}

// ---- main -------------------------------------------------------------------
while (true) {
    var modo = menuLoop();
    if (modo === 0) offlineFlow();
    else onlineFlow();
}
