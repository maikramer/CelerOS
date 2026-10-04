// CelerOS Help (Central de Ajuda) — app de sistema. Porte do HelpCenterUI.cpp
// (commit 64ccfc8): menu Offline/Online, categorias e topicos offline (textos
// originais resumidos em PT-BR), ajuda online buscada no hub via Net.getJSON
// (indice -> categoria -> artigo) e leitor com rolagem por inercia.
// Interface no toolkit UI (API 22). X no canto sup. direito sai.

var T = System.theme();
var LX = 8, LW = 224, TOP = 48;

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
          c: "No watch, Sensors.accel le o acelerômetro, Sensors.steps conta passos e Sensors.temp a temperatura. System.getWeekday devolve o dia da semana, System.keepAwake(ms) segura a tela acordada por ms milissegundos (ou true/false) e o RTC mantém a hora entre reboots." }
    ]},
    { name: "Problemas Comuns", topics: [
        { t: "Tela preta", d: "Travamento em apps",
          c: "Se um app travar ou ficar sem memória, o sistema mostra uma tela de erro: toque para voltar ao launcher. Em placas sem PSRAM (como a CYD), apps muito grandes podem não caber; a loja marca esses apps como Requer PSRAM." }
    ]}
];

// ---- lista generica: devolve o indice tocado ou -1 (voltar) -----------------
var L_title = "", L_items = [], L_sub = null, L_id = 0;

function setList(title, items, subs) {
    L_title = title;
    L_items = items;
    L_sub = subs;
    L_id++;  // lista nova comeca no topo (estado de rolagem por id)
}
function listLoop() {
    var rows = [];
    for (var i = 0; i < L_items.length; i++) {
        rows.push({ label: L_items[i], sub: L_sub !== null ? (L_sub[i] || "") : "" });
    }
    UI.invalidate();
    while (true) {
        UI.begin(T.bg);
        if (UI.header(L_title, { back: true })) return -1;
        var k = UI.list("l" + L_id, LX, TOP, LW, 312 - TOP, rows);
        if (k >= 0) return k;
        UI.end();
    }
}

// ---- leitor de topico: paragrafos com quebra por palavra + rolagem --------
var V_title = "", V_paras = [], V_h = 0, V_id = 0;

function setViewer(title, content) {
    V_title = title;
    V_paras = String(content).replace(/\r/g, "").split("\n");
    V_h = 0;
    V_id++;
}
function viewerLoop() {
    UI.invalidate();
    var top = TOP, h = 312 - TOP;
    while (true) {
        var full = UI.begin(T.bg);
        if (UI.header(V_title, { back: true })) return;
        var off = UI.scrollBegin("v" + V_id, LX, top, LW, h, Math.max(h, V_h));
        if (full) {
            var y = top + 4 - off;
            for (var i = 0; i < V_paras.length; i++) {
                if (V_paras[i] === "") { y += 10; continue; }
                y += UI.text(V_paras[i], LX + 4, y, { w: LW - 14, lines: 64, id: i }) + 6;
            }
            V_h = y + off - top + 8;
        }
        UI.scrollEnd();
        UI.end();
    }
}

// ---- menu principal -----------------------------------------------------
// devolve 0 = offline, 1 = online
function menuLoop() {
    UI.invalidate();
    while (true) {
        UI.begin(T.bg);
        UI.header("Central de Ajuda");
        UI.card(LX, TOP, LW, 64);
        UI.text("Como usar o CelerOS:", LX + 12, TOP + 10, { role: "title" });
        UI.text("gestos, Wi-Fi, apps e mais.", LX + 12, TOP + 38, { role: "caption", color: T.textDim });
        UI.cardEnd();
        if (UI.button("Ajuda Offline", LX, TOP + 80, LW, 44)) return 0;
        if (UI.button("Ajuda Online", LX, TOP + 134, LW, 44, { style: "ghost" })) return 1;
        UI.text("A ajuda online lê a central no hub (precisa de Wi-Fi).", 120, TOP + 192,
                { role: "caption", align: "center", color: T.textDim, w: LW, lines: 2 });
        UI.end();
    }
}

// ---- ajuda online (CelerOS Hub, mesmo formato do HelpCenterUI.cpp) ---------
var IDX_URL = "https://os.celer.tec.br/help/index.json";
var errMsg = "";

function loading(msg) {
    UI.invalidate();
    UI.begin(T.bg);
    UI.header("Ajuda Online");
    UI.spinner(120, 140, 18);
    UI.text(msg, 120, 172, { align: "center", color: T.textDim });
    UI.end();
}

// busca JSON com tratamento de erro; em falha retorna null e seta errMsg
function fetchJSON(url, msg) {
    loading(msg);
    if (!Net.isConnected()) {
        errMsg = "Ligue o Wi-Fi primeiro!";
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
function showError() {
    UI.alert("Ajuda Online", errMsg);
}

function onlineFlow() {
    var d = fetchJSON(IDX_URL, "Buscando índice...");
    if (d === null) { showError(); return; }
    var catNames = [], catUrls = [];
    var cats = d.categories || [];
    for (var i = 0; i < cats.length && i < 25; i++) {
        catNames.push(String(cats[i].name));
        catUrls.push(String(cats[i].url));
    }
    if (catNames.length === 0) { errMsg = "Índice vazio."; showError(); return; }
    while (true) {
        setList("Ajuda Online", catNames, null);
        var c = listLoop();
        if (c < 0) return;
        var a = fetchJSON(catUrls[c], "Carregando tópicos...");
        if (a === null) { showError(); return; }
        var artTitles = [], artUrls = [];
        var arts = a.articles || [];
        for (var j = 0; j < arts.length && j < 25; j++) {
            artTitles.push(String(arts[j].title));
            artUrls.push(String(arts[j].url));
        }
        if (artTitles.length === 0) { errMsg = "Nenhum artigo."; showError(); return; }
        while (true) {
            setList(catNames[c], artTitles, null);
            var k = listLoop();
            if (k < 0) break;
            var art = fetchJSON(artUrls[k], "Carregando artigo...");
            if (art === null) { showError(); return; }
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
