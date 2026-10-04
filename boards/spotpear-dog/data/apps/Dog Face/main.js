// CelerOS Dog Face — a cara do cao robotico (API 11)
// ES5 puro (Duktape). Desenhada para o OLED 128x64 do spotpear-dog: o canvas
// virtual 240x320 escala x0.53/y0.20, entao TEXTO e ilegivel — a interface e
// 100% iconografica (olhos, pupilas, barras). Coordenadas: helper P() mapeia
// fisico(128x64) -> virtual, para raciocinar em pixels reais do vidro.
//
// Tambem e o "cerebro" do robo: gaits das 4 perninhas (maquina de estados,
// um quadro por volta do loop) e servidor Celer Link (comandos JSON via BLE
// pelo app Celer Remote no SmartDisplay) com pareamento por codigo (API 11):
// quem conecta digita o codigo de 6 digitos que aparece na tela; controles
// ja pareados entram direto (hold 5s no touch pad esquece todos).
//
// Repertorio 2.0: SEQUENCIADOR de passos {do:...} que alimenta os truques
// nativos (dancinha sorteada, giro, patinha, xixi...), os truques que o
// dono ensina (/local/dogtricks.json) e a coreografia que a LLM compoe
// (dog_sequence) — mais latidos WAV, emocoes na cara/anel de LED e fala
// (bolha no vidro + texto inteiro no controle pareado).
//
// Tempo real: rampas e fases andam pelo millis() (dt de cada volta), nao
// por contagem de voltas — o flush do OLED por I2C a 100 kHz (~100 ms) e o
// micLevel variam o tempo da volta e antes esticavam/encolhiam o passo.

var T = System.theme();
var PW = 128, PH = 64;  // vidro fisico do OLED

function PX(x) { return Math.round(x * 240 / PW); }
function PY(y) { return Math.round(y * 320 / PH); }
function PW_(w) { return Math.round(w * 240 / PW); }
function PH_(h) { return Math.round(h * 320 / PH); }

// ------------------------------------------------------------ pernas ------
// Pinout JTAG (repo maikramer/zzpet-s3-dog): 17=frente-esq, 13=frente-dir,
// 18=tras-esq, 14=tras-dir. 90 graus = pe de boa (1500 us).
//
// MARCHA PORTADA DO ESP-Hi (Espressif/ESP Friends, componente
// servo_dog_ctrl, gitee.com/esp-friends/esp-hi): cao de 4 SG90 com UMA
// articulacao por perna — a mesma cinematica deste. Nada inventado aqui:
// as tabelas abaixo sao as macros do C (FL/FR/BL/BR_ANGLE_STEP_FORWARD /
// BACKWARD = neutro -/+ 20, STEP_OFFSET = 5) e cada fase e o mesmo laco
// "for (i = 0; i < 40; i++) angulo = inicio +/- i; delay(500/speed)" com o
// speed default do produto (80 -> 6 ms por grau).
//
// O que faz o passo do ESP-Hi andar: o pace alterna as metades e o
// STEP_OFFSET desloca a faixa do FR/BL em 5 graus (salto de ~10 graus na
// troca de fase) — a assimetria quebra o cancelamento dos atritos, e os
// pes curvos das pernas do ESP-Hi fazem o resto.
//
// Offsets sao crus em relacao ao neutro, na convencao de sinais do ESP-Hi
// (FL e BR "pra frente" = angulo menor; FR e BL = angulo maior).
//
// PERNAS DO ESP-HI (2026-10): as perninhas plasticas foram trocadas pelas
// do proprio ESP-Hi, entao a marcha dele tem de valer aqui 1:1 (e o modo
// "esphi" virou o default). A montagem DESTE corpo e POR LADO (bench
// 2026-10-03: com o SIGN diagonal as 4 patas iam JUNTAS pra frente e pra
// tras — tesoura simetrica, so balancava): nas esquerdas angulo MAIOR =
// pata pra FRENTE, nas direitas angulo MENOR = pata pra FRENTE (o mesmo
// espelho do firmware-irmao xiaozhi-pet, que aplica 180-angulo nos servos
// DIREITOS). A convencao ESP-Hi (FL/BR menor = frente, FR/BL maior =
// frente) pede entao o SIGN por lado {-1,-1,+1,+1} — com ele o pace sai
// como no ESP-Hi: esquerdas balancam pra frente enquanto as direitas
// varrem pra tras, e vice-versa (nunca as 4 juntas).
//
// CALIBRACAO: mande {"type":"calib"} pelo Celer Remote/nRF Connect — as
// QUATRO patas (FL, FR, BL, BR, nessa ordem) devem ir 25 graus pra FRENTE
// e voltar. Todas pra tras de uma vez = {"type":"tune","flip":true} (vale
// pra creep, esphi e posturas); pata individual errada = inverta o SIGN
// dela; inclinacao de pe = {"type":"tune","lean":graus} (20 = postura do
// ESP-Hi; 0 = perna vertical).
// Postura de pe: NAO e perna vertical. A pose de instalacao do ESP-Hi
// (servo_dog_installation) estica as pernas em linha com o corpo nos
// extremos do servo (0/180); o neutro dele (70/110/110/70) fica 20 graus
// antes da vertical — as 4 pernas INCLINADAS ~20 graus pra FRENTE (com a
// montagem espelhada, o neutro de cada uma pende pro nariz). E nessa zona
// que o pe redondo dele empurra: em pe reto o varredor dos dois lados
// agarra e o corpo so torce (bench 2026-10-03: pace certo, zero deslocamento).
// No nosso espelho por lado (esq sobe = frente): esquerdas 90+LEAN,
// direitas 90-LEAN. LEAN e afinavel ao vivo: {"type":"tune","lean":0..40}.
var LEAN = 20;   // inclinacao fisica pra frente (postura do ESP-Hi = 20)
var NEUTRAL = { FL: 110, FR: 70, BL: 110, BR: 70 };   // = 90 +/- LEAN
function setLean(l) {
    LEAN = l;
    NEUTRAL.FL = 90 + l; NEUTRAL.FR = 90 - l;
    NEUTRAL.BL = 90 + l; NEUTRAL.BR = 90 - l;
    if (!legsLimp) legsHold();   // ja reescreve a postura nova
}
var PIN = { FL: 17, FR: 13, BL: 18, BR: 14 };
var KEYS = ["FL", "FR", "BL", "BR"];
var SIGN = { FL: -1, FR: -1, BL: 1, BR: 1 };  // convencao ESP-Hi -> fisica (POR LADO)
var FLIP = 1;  // -1 = inverte a direcao fisica de TUDO (tune "flip", bancada)

// API 10: servos moram no sub-objeto gpio (desligavel por Kconfig)
var SERVO = (typeof System.gpio !== "undefined" && System.gpio.servo)
    ? System.gpio.servo : null;
System.print('[dog] servo api: ' + (SERVO ? 'ok' : 'NULL'));

var servoFails = 0;
var failLogged = 0;

function leg(pin, angle) {
    if (!SERVO) return;
    if (!SERVO(pin, angle)) {
        servoFails++;
        if (failLogged < 12) { System.print('[dog] servo FAIL pin ' + pin + '@' + angle); failLogged++; }
    }
}

var off = { FL: 0, FR: 0, BL: 0, BR: 0 };     // offset atual por perna
var target = { FL: 0, FR: 0, BL: 0, BR: 0 };  // alvo das posturas (rampa)
var sent = { FL: -1, FR: -1, BL: -1, BR: -1 };  // ultimo angulo escrito

function put(k, o) {
    off[k] = o;
    var a = Math.round(NEUTRAL[k] + FLIP * SIGN[k] * o);
    if (a !== sent[k]) {
        sent[k] = a;
        leg(PIN[k], a);
    }
}

// Reescreve a pose atual (servo solto depois do sono nao sabe onde esta).
function legsHold() {
    for (var i = 0; i < KEYS.length; i++) {
        sent[KEYS[i]] = -1;
        put(KEYS[i], off[KEYS[i]]);
    }
}

// Solta os servos (sem torque = sem corrente de sustentacao).
var legsLimp = false;
function legsRelease() {
    if (legsLimp || !System.gpio || !System.gpio.servoOff) return;
    for (var i = 0; i < KEYS.length; i++) System.gpio.servoOff(PIN[KEYS[i]]);
    legsLimp = true;
}

// ---- tabelas do ESP-Hi ----
var STEP_MS = 6;       // 500 / speed(80), arredondado pra baixo como no C
var STEP_N = 40;       // graus varridos por fase
var PAUSE_MS = 50;     // pausa entre fases (andar pra frente/tras)
var S = 5;             // STEP_OFFSET
var F = { FL: -20, FR: 20, BL: 20, BR: -20 };   // *_ANGLE_STEP_FORWARD - neutro
var B = { FL: 20, FR: -20, BL: -20, BR: 20 };   // *_ANGLE_STEP_BACKWARD - neutro

// Fase = [[inicio FL, FR, BL, BR], [passo por grau FL, FR, BL, BR]]:
// angulo(i) = inicio + passo * i, i = 0..39. Transcricao linha a linha de
// servo_dog_forward/backward/turn_left/turn_right.
var WALKS = {
    walk: { pause: true, phases: [
        [[B.FL, F.FR - S, B.BL - S, F.BR], [-1, -1, +1, +1]],
        [[F.FL, B.FR + S, F.BL + S, B.BR], [+1, +1, -1, -1]]
    ]},
    back: { pause: true, phases: [
        [[F.FL - S, B.FR, F.BL, B.BR - S], [+1, +1, -1, -1]],
        [[B.FL + S, F.FR, B.BL, F.BR + S], [-1, -1, +1, +1]]
    ]},
    left: { pause: false, phases: [
        [[B.FL + S, B.FR, B.BL, B.BR + S], [-1, +1, +1, -1]],
        [[F.FL - S, F.FR, F.BL, F.BR - S], [+1, -1, -1, +1]]
    ]},
    right: { pause: false, phases: [
        [[F.FL, F.FR + S, F.BL + S, F.BR], [+1, -1, -1, +1]],
        [[B.FL - S, B.FR, B.BL, B.BR - S], [-1, +1, +1, -1]]
    ]}
};

// Posturas (alvo da rampa), transcricao do servo_dog_ctrl.c: lay_down varre
// 60 graus; bow/lean_back varrem BOW_OFFSET = 50 (macro real do C — o 30
// daqui era chute de quando o fonte ainda nao tinha sido lido).
var BOW = 50;
var POSES = {
    stand: [0, 0, 0, 0],
    lie: [-60, 60, 60, -60],            // servo_dog_lay_down
    stretch: [-BOW, BOW, -BOW, BOW],    // servo_dog_bow (play bow)
    sit: [BOW, -BOW, BOW, -BOW],        // servo_dog_lean_back
    // Repertorio 2.0 (mesma convencao de offsets; angulos de partida para
    // validar/afinar no vidro pela bancada):
    beg: [65, -65, 55, -55],        // sentado ereto: dobra mais a dianteira
    crouch: [30, -30, -30, 30],     // agachado (4 encurtadas): base do pushup
    pee: [55, -5, 10, -25],         // xixi: encurta a FL (ergue o canto BR)
    shake_hi: [-40, -50, 50, -50],  // sentado oferecendo a pata FL (alta)
    shake_lo: [-52, -50, 50, -50]   // ...e abanando (baixa)
};
var RATE = 6;   // graus por 60 ms nas posturas (lay_down: 1 grau / 10 ms)

// Varredura de UMA fase, bloqueante (~240 ms): igual ao laco do C. O
// relogio manda: se uma volta atrasar (GC), o proximo grau sai no ponto
// certo da reta em vez de acumular atraso.
function runPhase(ph) {
    var st = ph[0], dv = ph[1];
    var t0 = System.millis();
    for (var i = 0; i < STEP_N; i++) {
        put("FL", st[0] + dv[0] * i);
        put("FR", st[1] + dv[1] * i);
        put("BL", st[2] + dv[2] * i);
        put("BR", st[3] + dv[3] * i);
        var wait = t0 + (i + 1) * STEP_MS - System.millis();
        if (wait > 0) System.delay(wait);
    }
}

// Volta ao neutro como o fim de servo_dog_forward: 20 passos de STEP_MS.
function settle() {
    var from = [off.FL, off.FR, off.BL, off.BR];
    for (var i = 1; i <= 20; i++) {
        var f = 1 - i / 20;
        put("FL", from[0] * f);
        put("FR", from[1] * f);
        put("BL", from[2] * f);
        put("BR", from[3] * f);
        System.delay(STEP_MS);
    }
    for (var k = 0; k < KEYS.length; k++) target[KEYS[k]] = 0;
}

// Postura: rampa pelo relogio (nao bloqueia a cara).
function legTick(dt) {
    var step = RATE * dt / 60;
    for (var i = 0; i < KEYS.length; i++) {
        var k = KEYS[i];
        var d = target[k] - off[k];
        if (d > step) d = step;
        else if (d < -step) d = -step;
        if (d !== 0) put(k, off[k] + d);
    }
}

function setPose(p) {
    target.FL = p[0]; target.FR = p[1]; target.BL = p[2]; target.BR = p[3];
}

var gait = null, gaitName = "", gaitPhase = 0, repeatGait = false;
var moveDir = null;   // direcao corrente do movimento (olhos acompanham)
// Dono da gait: "move" do link exige keepalive (linkDrive); gait repetida
// pedida pelo link para se o link cair (linkGait). Gait do pad nao depende
// de link nenhum.
var linkDrive = false, linkGait = false;

function walking() { return gait !== null; }

// name: walk/back/left/right (ciclicas, ESP-Hi) ou uma postura de POSES.
// repeat: ciclica segue ate stop; sem repeat anda 2 ciclos (repeat_count
// default do ESP-Hi) e volta ao neutro.
var cyclesLeft = 0;
function startGait(name, repeat) {
    if (!WALKS[name] && !POSES[name]) return false;
    linkDrive = false;
    linkGait = false;
    if (legsLimp) { legsLimp = false; legsHold(); }
    if (WALKS[name]) {
        if (gait !== null && gaitName === name) {
            repeatGait = repeatGait || !!repeat;
            return true;  // mesma marcha em curso: nao reinicia o passo no meio
        }
        // modo creep: quadros gerados a cada ciclo; esphi: tabelas do C
        gait = walkMode === "creep" ? { frames: true, d: DIRS[name], list: null } : WALKS[name];
        gaitPhase = 0;
        cyclesLeft = 2;
    } else {
        if (gait !== null) settle();
        gait = null;
        setPose(POSES[name]);
    }
    gaitName = name;
    repeatGait = !!repeat;
    moveDir = (name === "walk") ? 1 : (name === "back" ? -1 :
               (name === "left" ? -1 : (name === "right" ? 1 : null)));
    return true;
}

function stopGait() {
    linkDrive = false;
    linkGait = false;
    repeatGait = false;
    if (gait !== null) settle();
    else setPose(POSES.stand);
    gait = null;
    gaitName = "";
    moveDir = null;
}

// Uma volta do loop: marcha = UMA fase inteira (bloqueante, ~240-290 ms,
// o link e a cara rodam entre as fases); postura = rampa por dt.
function gaitTick(dt) {
    if (legsLimp) return;
    if (!gait) {
        legTick(dt);
        return;
    }
    var len;
    if (gait.frames) {
        if (gaitPhase === 0 || !gait.list) gait.list = creepFrames(gait.d);
        runFrame(gait.list[gaitPhase]);
        len = gait.list.length;
    } else {
        runPhase(gait.phases[gaitPhase]);
        if (gait.pause) System.delay(PAUSE_MS);
        len = gait.phases.length;
    }
    gaitPhase++;
    if (gaitPhase >= len) {
        gaitPhase = 0;
        if (!repeatGait && --cyclesLeft <= 0) stopGait();
    }
}

// Calibracao: cada perna vai 25 graus pra FRENTE fisica (a mesma "frente"
// da marcha centopeia, via putA) e volta, na ordem FL, FR, BL, BR. Pata
// indo pra tras = inverta o FWD dela; todas pra tras = {"type":"tune","flip":true}.
function calibrate() {
    if (gait !== null) stopGait();
    if (legsLimp) { legsLimp = false; legsHold(); }
    for (var i = 0; i < KEYS.length; i++) {
        var k = KEYS[i];
        System.print('[dog] calib ' + k + ' (pino ' + PIN[k] + ') pra frente');
        for (var a = 1; a <= 25; a++) { putA(k, a); System.delay(20); }
        System.delay(600);
        for (var b = 24; b >= 0; b--) { putA(k, b); System.delay(20); }
        System.delay(300);
    }
}

// ---------------------------------------------- marcha centopeia ------
// Convencao FISICA: a > 0 = pata pra FRENTE (rumo ao focinho), 0 = perna
// vertical. FWD por perna (o flip global mora no put): putA com a>0 sobe o
// angulo nas ESQUERDAS e desce nas DIREITAS — frente fisica por lado.
//
// Fisica da perna de 1 articulacao: altura do quadril = L * cos(a). Perna
// vertical = mais comprida; inclinada = mais curta. Nao da pra "levantar"
// uma pata, mas da pra ENCURTAR um canto: o corpo gira sobre a diagonal e
// o canto OPOSTO sobe. Encurtar o traseiro-esq ergue o dianteiro-dir (o
// corpo pivota na diagonal FL-BR), e vice-versa:
//
//   pata a mover   canto encurtado (diagonal oposta)
//   FR             BL
//   FL             BR
//   BL             FR
//   BR             FL
//
// Ciclo (d = +1 empurra o corpo pra frente, -1 pra tras; por perna):
//   1. REMADA: as 4 patas no chao varrem de +P*d a -P*d juntas — o atrito
//      de todas empurra pro mesmo lado e o corpo avanca ~2*L*sin(P).
//   2. RECUPERACAO, uma pata por vez (ordem configuravel, default
//      BL, FL, BR, FR = creep classico): inclina o canto oposto mais T
//      graus (encurta, a pata alvo sai do chao), leva a pata alvo de volta
//      a +P*d no ar, desinclina (a pata pousa na frente).
// Movimento quase estatico, lento e robusto; nada empurra pra tras.
//
// Historico: no putA com o sinal literal de F do ESP-Hi a pata erguida
// POUSAVA ATRAS (validado no cao na 1.3) — por isso FWD ficou invertido em
// relacao a F, com o SIGN compensando no caminho contrario; o produto dos
// dois reproduz os angulos do C fielmente.
var FWD = { FL: -1, FR: 1, BL: 1, BR: -1 };
function putA(k, a) { put(k, FWD[k] * a); }
function angA(k) { return FWD[k] * off[k]; }

var UNLOAD = { FR: "BL", FL: "BR", BL: "FR", BR: "FL" };
var CREEP = { P: 20, T: 25, power: 600, tilt: 180, swing: 240, order: ["BL", "FL", "BR", "FR"] };
var DIRS = {
    walk: { FL: 1, FR: 1, BL: 1, BR: 1 },
    back: { FL: -1, FR: -1, BL: -1, BR: -1 },
    left: { FL: -1, FR: 1, BL: -1, BR: 1 },    // lado direito empurra, esquerdo recua
    right: { FL: 1, FR: -1, BL: 1, BR: -1 }
};
var WALK_MODES = ["creep", "esphi"];
// Marchas que o botao do Remote alterna (tel.modes). Default = esphi desde
// a troca das perninhas pelas do ESP-Hi (a geometria dos pes dele e o que
// faz o pace dele sair do lugar). O creep (centopeia, quase estatico) fica
// como alternativa pra chao/peso que o pace nao agarar.
var MODES_OK = ["esphi", "creep"];
var walkMode = "esphi";
var FRAME_STEP_MS = 10;

function copyPose(p) { return { FL: p.FL, FR: p.FR, BL: p.BL, BR: p.BR }; }

// Quadros de UM ciclo: [{p: pose fisica, ms}]. Refeito a cada ciclo para
// o tune ao vivo valer ja no proximo passo.
function creepFrames(d) {
    var P = CREEP.P, T = CREEP.T, fr = [];
    var a0 = {}, a1 = {};
    for (var i = 0; i < KEYS.length; i++) {
        a0[KEYS[i]] = P * d[KEYS[i]];
        a1[KEYS[i]] = -P * d[KEYS[i]];
    }
    fr.push({ p: copyPose(a1), ms: CREEP.power });           // 1. remada
    var cur = copyPose(a1);
    for (var j = 0; j < CREEP.order.length; j++) {           // 2. recuperacao
        var k = CREEP.order[j], u = UNLOAD[k];
        var tilt = copyPose(cur);
        tilt[u] = cur[u] + (cur[u] >= 0 ? T : -T);            // encurta o canto oposto
        fr.push({ p: tilt, ms: CREEP.tilt });
        var swing = copyPose(tilt);
        swing[k] = a0[k];                                     // pata alvo vai a frente no ar
        fr.push({ p: swing, ms: CREEP.swing });
        cur = copyPose(swing);
        cur[u] = tilt[u] - (cur[u] >= 0 ? T : -T);           // desinclina: pata pousa
        fr.push({ p: copyPose(cur), ms: CREEP.tilt });
    }
    return fr;
}

// Interpola da pose atual ate fr.p em fr.ms (bloqueante, passo de 10 ms,
// guiado pelo relogio como o runPhase).
function runFrame(fr) {
    var from = { FL: angA("FL"), FR: angA("FR"), BL: angA("BL"), BR: angA("BR") };
    var n = Math.max(1, Math.round(fr.ms / FRAME_STEP_MS));
    var t0 = System.millis();
    for (var i = 1; i <= n; i++) {
        var f = i / n;
        for (var q = 0; q < KEYS.length; q++) {
            var k = KEYS[q];
            putA(k, from[k] + (fr.p[k] - from[k]) * f);
        }
        var wait = t0 + i * FRAME_STEP_MS - System.millis();
        if (wait > 0) System.delay(wait);
    }
}

// ---- ajuste ao vivo ({"type":"tune",...}) e persistencia ----
var TUNE_FILE = "/local/dogtune.json";
var TUNE_LEGS = "esphi";  // marca da calibracao vigente: trocou perna, arquivo velho nao vale

function clampNum(v, lo, hi, dflt) {
    v = Number(v);
    if (!(v >= lo)) return dflt;  // NaN/undefined tambem
    return v > hi ? hi : v;
}

function applyTune(t) {
    if (!t) return;
    CREEP.P = clampNum(t.P, 5, 45, CREEP.P);
    CREEP.T = clampNum(t.T, 0, 50, CREEP.T);
    CREEP.power = clampNum(t.power, 60, 3000, CREEP.power);
    CREEP.tilt = clampNum(t.tilt, 30, 3000, CREEP.tilt);
    CREEP.swing = clampNum(t.swing, 30, 3000, CREEP.swing);
    if (t.order && t.order.length === 4) {
        var seen = {}, ok = true;
        for (var i = 0; i < 4; i++) {
            if (UNLOAD[t.order[i]] === undefined || seen[t.order[i]]) ok = false;
            seen[t.order[i]] = true;
        }
        if (ok) CREEP.order = [t.order[0], t.order[1], t.order[2], t.order[3]];
    }
    if (t.flip === true) FLIP = -FLIP;   // alterna a direcao fisica de tudo
    else if (t.flip === 1 || t.flip === -1) FLIP = t.flip;
    if (t.lean !== undefined) setLean(clampNum(t.lean, 0, 40, LEAN));
    if (t.mode && WALK_MODES.indexOf(t.mode) >= 0) walkMode = t.mode;
}

function saveTune() {
    if (typeof FS === "undefined" || !FS.writeTextFile) return;
    try {
        FS.writeTextFile(TUNE_FILE, JSON.stringify({
            legs: TUNE_LEGS, lean: LEAN,
            P: CREEP.P, T: CREEP.T, power: CREEP.power, tilt: CREEP.tilt,
            swing: CREEP.swing, order: CREEP.order, mode: walkMode, flip: FLIP
        }));
    } catch (e) {}
}

function loadTune() {
    if (typeof FS === "undefined" || !FS.exists || !FS.exists(TUNE_FILE)) return;
    try {
        var t = JSON.parse(FS.readTextFile(TUNE_FILE));
        // o P/T/flip/mode salvos com as pernas do ZZPET nao valem mais:
        // so aplica o que foi gravado com as pernas atuais (TUNE_LEGS)
        if (t && t.legs === TUNE_LEGS) applyTune(t);
    } catch (e) {}
}
loadTune();

// ------------------------------------------- repertorio 2.0 -----------------
// Truques coreografados, latidos de verdade (WAVs do app, gerados por
// tools/dog/barks.py), emocoes (cara + anel de LED) e o SEQUENCIADOR: a
// maquina que roda uma fila de passos {do:...} no ritmo do loop principal.
// A mesma fila alimenta os truques nativos (TRICKS), os truques que o dono
// ensina (/local/dogtricks.json) e a coreografia que a LLM compoe na hora
// (dog_sequence). Tudo clampado: atos fora da whitelist sao descartados, a
// fila nao passa de 12 passos nem de 12 s de relogio.

// ---- latidos ----
var BARK_DIR = "/local/apps/Dog Face/";
var BARKS = ["woof", "yip", "growl", "whine", "howl"];
var BARK_FALLBACK = {  // sem WAV (imagem antiga): melodia aproximada
    woof: [[220, 90], [150, 130]],
    yip: [[900, 50], [600, 70]],
    growl: [[110, 160], [95, 180], [110, 160]],
    whine: [[900, 160], [1250, 220], [800, 260]],
    howl: [[420, 300], [560, 720], [500, 260]]
};
function playBark(kind, n) {
    if (BARKS.indexOf(kind) < 0) kind = "woof";
    n = Math.round(clampNum(n, 1, 3, 1));
    for (var i = 0; i < n; i++) {
        var played = false;
        try { played = System.playWav(BARK_DIR + "bark_" + kind + ".wav"); } catch (e) { played = false; }
        if (!played) System.playTone(BARK_FALLBACK[kind]);
        if (i + 1 < n) System.delay(140);
    }
}

// ---- anel de LED (2 fitas x 4) ----
var ledAnim = "off", ledAt = 0, ledFrame = 0;
var LED_COLORS = { happy: 0x20C020, alert: 0xFF2000, heart: 0xE02070,
                   listen: 0x0040FF, err: 0xFF2000, calm: 0x00A0A0 };
function ledDim(c, f) {
    return ((Math.round(((c >> 16) & 255) * f) << 16) |
            (Math.round(((c >> 8) & 255) * f) << 8) | Math.round((c & 255) * f));
}
function ledHsv(h) {   // h 0..1 -> 0xRRGGBB (arco-iris da dancinha)
    var i = Math.floor(h * 6), f = h * 6 - i;
    var v = 0.65, p = 0, q = v * (1 - f);
    var r = 0, g = 0, b = 0;
    var m = i % 6;
    if (m === 0) { r = v; g = v * f; }
    else if (m === 1) { r = q; g = v; }
    else if (m === 2) { g = v; b = v * f; }
    else if (m === 3) { g = q; b = v; }
    else if (m === 4) { r = v * f; b = v; }
    else { r = v; b = p; }
    return (Math.round(r * 255) << 16) | (Math.round(g * 255) << 8) | Math.round(b * 255);
}
function setLeds(anim) {
    if (ledAnim === anim) return;
    ledAnim = anim;
    ledFrame = 0;
    if (anim === "off") { System.neopixel(0, [0, 0, 0, 0]); System.neopixel(1, [0, 0, 0, 0]); }
}
function ledsOff() { setLeds("off"); }
function ledTick(now) {   // um quadro a cada 120 ms
    if (ledAnim === "off" || now - ledAt < 120) return;
    ledAt = now;
    ledFrame++;
    var solid = LED_COLORS[ledAnim];
    for (var s = 0; s < 2; s++) {
        var px = [];
        for (var i = 0; i < 4; i++) {
            var c = 0;
            if (ledAnim === "rainbow") c = ledHsv((ledFrame * 0.06 + i * 0.125) % 1);
            else if (ledAnim === "happy" || ledAnim === "alert") c = ((ledFrame >> 1) & 1) === 0 ? solid : 0;
            else if (ledAnim === "heart") c = ledDim(solid, 0.35 + 0.65 * Math.abs(Math.sin(ledFrame * 0.35)));
            else c = solid;
            px.push(c);
        }
        System.neopixel(s, px);
    }
}

// ---- emocoes (cara + LED) ----
var MOODS = ["happy", "love", "curious", "sad", "angry", "sleepy", "alert"];
var mood = "", moodUntil = 0;
function setMood(m, ms) {
    if (MOODS.indexOf(m) < 0) return false;
    moodUntil = System.millis() + (ms || 4000);
    lastActivity = System.millis();
    if (m === "happy") {   // feliz ja tem cara propria (happyUntil)
        mood = "";
        happyUntil = System.millis() + (ms || 1600);
        setLeds("happy");
        return true;
    }
    mood = m;
    if (m === "love") setLeds("heart");
    else if (m === "angry" || m === "alert") setLeds("alert");
    else if (m === "sad" || m === "sleepy") ledsOff();
    else if (m === "curious") setLeds("calm");
    return true;
}

// ---- fala do cao ----
// Bolha na cara + ate 5 caracteres em sete-segmentos (texto e ilegivel no
// vidro; OLA/SIM/87% cabem) + o texto INTEIRO no controle pareado via link.
var sayText = "", sayRaw = "", sayUntil = 0;
function showSay(text) {
    sayRaw = String(text || "");
    var ok = "";
    for (var i = 0; i < sayRaw.length && ok.length < 5; i++) {
        var ch = sayRaw.charAt(i).toUpperCase();
        if (SEG[ch - "0"] !== undefined || SEG7[ch] !== undefined) ok += ch;
    }
    sayText = ok;
    sayUntil = System.millis() + 3500;
    lastActivity = System.millis();
    reply({ type: "say", text: sayRaw.substring(0, 120) });
    playBark("yip", 1);
}

// ---- sequenciador ----
var SEQ_MAX_STEPS = 12;     // passos por fila (dono ensina ate 20)
var SEQ_STEP_MS = 3000;     // teto de UM passo
var SEQ_TOTAL_MS = 12000;   // teto da fila inteira
var seqSteps = null, seqIdx = 0, seqUntil = 0, seqMove = false;
function seqActive() { return seqSteps !== null; }
function seqClear() { seqSteps = null; seqIdx = 0; seqUntil = 0; }

function clampMs(v, dflt) { return Math.round(clampNum(v, 150, SEQ_STEP_MS, dflt)); }

// Valida UM passo cru (JSON da LLM ou do dono); truque devolve array expandido.
function seqStep(s, depth) {
    if (!s || typeof s !== "object") return null;
    var act = String(s.do || "");
    if (act === "move") {
        if (!WALKS[s.dir]) return null;
        return { do: "move", dir: String(s.dir), ms: clampMs(s.ms, 900) };
    }
    if (act === "pose") {
        if (!POSES[s.name]) return null;
        return { do: "pose", name: String(s.name), ms: clampMs(s.ms, 700) };
    }
    if (act === "trick") {
        if (depth >= 2) return null;   // truque dentro de truque dentro de truque: nao
        return trickSteps(String(s.name || ""), depth);
    }
    if (act === "bark") {
        var k = BARKS.indexOf(s.kind) >= 0 ? String(s.kind) : "woof";
        return { do: "bark", kind: k, n: Math.round(clampNum(s.n, 1, 3, 1)) };
    }
    if (act === "leds") {
        var an = ["off", "happy", "rainbow", "alert", "heart"].indexOf(s.anim) >= 0
            ? String(s.anim) : "happy";
        return { do: "leds", anim: an };
    }
    if (act === "emotion") {
        if (MOODS.indexOf(s.mood) < 0) return null;
        return { do: "emotion", mood: String(s.mood) };
    }
    if (act === "wait") return { do: "wait", ms: clampMs(s.ms, 300) };
    if (act === "say") {
        if (!s.text) return null;
        return { do: "say", text: String(s.text).substring(0, 60) };
    }
    return null;
}

// Fila valida: descarta passos invalidos, expande truques, respeta os
// tetos de passos e de duracao total.
function buildSeq(raw, maxSteps, depth) {
    var out = [];
    if (!raw || typeof raw.length !== "number") return out;
    for (var i = 0; i < raw.length && out.length < maxSteps; i++) {
        var st = seqStep(raw[i], depth || 0);
        if (st === null) continue;
        if (st.length !== undefined) {   // truque expandido: absorve os passos
            for (var j = 0; j < st.length && out.length < maxSteps; j++) out.push(st[j]);
        } else out.push(st);
    }
    var fin = [], total = 0;
    for (var q = 0; q < out.length; q++) {
        var ms = out[q].ms || (out[q].do === "bark" ? 600 * out[q].n : 0);
        if (total + ms > SEQ_TOTAL_MS) break;
        total += ms;
        fin.push(out[q]);
    }
    return fin;
}

function runSeq(steps, kind) {
    seqClear();
    if (!steps || !steps.length) return false;
    if (legsLimp) { legsLimp = false; legsHold(); }
    seqSteps = steps;
    seqIdx = 0;
    seqUntil = 0;
    System.print('[dog] sequencia ' + (kind || '') + ': ' + steps.length + ' passos');
    return true;
}

// Uma volta do loop: passos instantaneos (bark/leds/emotion/say) saem em
// rajada ate o proximo passo com duracao; move/pose/wait ocupam a fila ate
// seqUntil. Bark e bloqueante (playWav toca inteiro): a dancinha da uma
// pausa natural a cada latido.
function seqTick(now) {
    if (!seqSteps) return;
    if (seqUntil > now) return;
    if (seqUntil !== 0) {
        seqUntil = 0;
        seqIdx++;
        if (seqMove) { seqMove = false; stopGait(); }
        if (seqIdx >= seqSteps.length) { seqEnd(); return; }
    }
    while (seqIdx < seqSteps.length) {
        var s = seqSteps[seqIdx];
        if (s.do === "move") {
            if (startGait(s.dir, true)) { seqMove = true; seqUntil = now + s.ms; break; }
        } else if (s.do === "pose") {
            if (startGait(s.name, false)) { seqUntil = now + s.ms; break; }
        } else if (s.do === "wait") {
            seqUntil = now + s.ms;
            break;
        } else if (s.do === "bark") {
            playBark(s.kind, s.n);
        } else if (s.do === "leds") {
            setLeds(s.anim);
        } else if (s.do === "emotion") {
            setMood(s.mood, 4000);
        } else if (s.do === "say") {
            showSay(s.text);
        }
        seqIdx++;
    }
    if (seqIdx >= seqSteps.length) seqEnd();
}

function seqEnd() {
    seqClear();
    if (seqMove) { seqMove = false; stopGait(); }
    ledsOff();
}

// ---- truques nativos ----
// A danca e uma funcao: sorteia a coreografia a cada chamada (nunca a
// mesma dancinha). Os demais sao filas fixas.
var HEAVY_TRICKS = ["dance", "spin", "excited"];
var TRICKS = {
    dance: function () {
        var pool = [
            { do: "pose", name: "sit", ms: 420 },
            { do: "pose", name: "stand", ms: 320 },
            { do: "pose", name: "stretch", ms: 400 },
            { do: "pose", name: "crouch", ms: 400 },
            { do: "move", dir: "left", ms: 420 },
            { do: "move", dir: "right", ms: 420 },
            { do: "move", dir: "walk", ms: 480 },
            { do: "bark", kind: "yip", n: 1 }
        ];
        var steps = [{ do: "leds", anim: "rainbow" }];
        var n = 6 + Math.floor(Math.random() * 3);
        for (var i = 0; i < n; i++) steps.push(pool[Math.floor(Math.random() * pool.length)]);
        steps.push({ do: "emotion", mood: "happy" });
        steps.push({ do: "leds", anim: "off" });
        return steps;
    },
    spin: [
        { do: "bark", kind: "yip", n: 1 },
        { do: "move", dir: "left", ms: 2200 },
        { do: "emotion", mood: "happy" }
    ],
    shake: function () {   // patinha: senta e oferece a FL abanando
        var s = [{ do: "pose", name: "sit", ms: 600 }];
        for (var i = 0; i < 4; i++) s.push({ do: "pose", name: i % 2 ? "shake_lo" : "shake_hi", ms: 200 });
        s.push({ do: "pose", name: "sit", ms: 300 });
        s.push({ do: "bark", kind: "yip", n: 1 });
        return s;
    },
    pushup: [
        { do: "pose", name: "crouch", ms: 420 },
        { do: "pose", name: "stand", ms: 320 },
        { do: "pose", name: "crouch", ms: 420 },
        { do: "pose", name: "stand", ms: 320 },
        { do: "pose", name: "crouch", ms: 420 },
        { do: "pose", name: "stand", ms: 320 },
        { do: "bark", kind: "woof", n: 1 }
    ],
    excited: [
        { do: "bark", kind: "yip", n: 2 },
        { do: "move", dir: "walk", ms: 320 },
        { do: "move", dir: "back", ms: 320 },
        { do: "move", dir: "walk", ms: 320 },
        { do: "move", dir: "back", ms: 320 },
        { do: "emotion", mood: "happy" }
    ],
    hello: [
        { do: "pose", name: "sit", ms: 500 },
        { do: "pose", name: "shake_hi", ms: 220 },
        { do: "pose", name: "shake_lo", ms: 220 },
        { do: "pose", name: "shake_hi", ms: 220 },
        { do: "say", text: "OLA" },
        { do: "bark", kind: "yip", n: 1 }
    ],
    pee: [
        { do: "pose", name: "pee", ms: 1200 },
        { do: "bark", kind: "yip", n: 1 },
        { do: "pose", name: "stand", ms: 500 }
    ]
};

function trickNames() {
    var n = [];
    for (var k in TRICKS) n.push(k);
    return n;
}

// Expandir truque pelo nome -> fila valida (ou null)
function trickSteps(name, depth) {
    var def = TRICKS[String(name || "").toLowerCase()];
    if (def === undefined) return null;
    var raw = typeof def === "function" ? def() : def;
    return buildSeq(raw, SEQ_MAX_STEPS, (depth || 0) + 1);
}

function battPct() {
    if (lastBatt < 0) return 100;
    var f = (lastBatt - 3300) / 900;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return Math.round(f * 100);
}

function runTrick(name) {
    name = String(name || "").toLowerCase();
    // bateria fraca recusa truque pesado com drama (e evita brownout)
    if (HEAVY_TRICKS.indexOf(name) >= 0 && battPct() < 15) {
        playBark("whine", 1);
        setMood("sad", 3000);
        showSay("cansado");
        return false;
    }
    var steps = trickSteps(name, 0);
    if (!steps || !steps.length) return false;
    happyUntil = System.millis() + 900;
    return runSeq(steps, "trick:" + name);
}

// ------------------------------------------------------------ estado ------
var blinkAt = System.millis() + 1800;
var blinkUntil = 0;
var happyUntil = 0;
var lookX = 0, lookY = 0;
var lookT = System.millis() + 900;
var breath = 0;
var lastMic = 0, micAt = 0;
var lastBatt = -1, battAt = -100000;
var battWarned = false;      // ganido da bateria fraca: uma vez por cruzamento
var padZeros = 0;           // leituras soltas seguidas (debounce da soltura)
var lastActivity = System.millis();
var sleeping = false;
var padHeld = false, padDownAt = 0;
var GAIT_CYCLE = ["stand", "walk", "sit", "lie", "stretch"];
var gaitIdx = 0;

function cycleGait() {
    seqClear();   // o pad manda: interrompe truque/sequencia em curso
    gaitIdx = (gaitIdx + 1) % GAIT_CYCLE.length;
    var name = GAIT_CYCLE[gaitIdx];
    System.print('[dog] touch gait: ' + name);
    startGait(name, name === "walk");
    happyUntil = System.millis() + 800;
}

function drawEyes(open, pupilR) {
    if (sleeping) open = false;
    var ey = 24 + Math.round(Math.sin(breath) * 1.5);
    var eh = open ? 22 : 3;
    System.fillRoundRect(PX(18), PY(ey - eh / 2), PW_(34), PH_(eh), PH_(6), 0xFFFF);
    System.fillRoundRect(PX(76), PY(ey - eh / 2), PW_(34), PH_(eh), PH_(6), 0xFFFF);
    if (open) {
        var px = Math.round(lookX * 6);
        var py = Math.round(lookY * 3);
        System.fillCircle(PX(35 + px), PY(ey + py), PW_(pupilR), 0xFFFF);
        System.fillCircle(PX(93 + px), PY(ey + py), PW_(pupilR), 0xFFFF);
    }
}

function drawHappy() {
    System.fillRect(PX(22), PY(20), PW_(10), PH_(3), 0xFFFF);
    System.fillRect(PX(32), PY(23), PW_(10), PH_(3), 0xFFFF);
    System.fillRect(PX(80), PY(20), PW_(10), PH_(3), 0xFFFF);
    System.fillRect(PX(90), PY(23), PW_(10), PH_(3), 0xFFFF);
    System.fillRoundRect(PX(52), PY(40), PW_(24), PH_(8), PH_(3), 0xFFFF);
}

// ---- emocoes na cara (repositorio 2.0) ----
function drawHeart(cx, cy, s) {
    System.fillCircle(PX(cx - s / 2 + 1), PY(cy - s / 4), PW_(s / 2 + 1), 0xFFFF);
    System.fillCircle(PX(cx + s / 2 - 1), PY(cy - s / 4), PW_(s / 2 + 1), 0xFFFF);
    System.fillTriangle(PX(cx - s), PY(cy - 2), PX(cx + s), PY(cy - 2),
                        PX(cx), PY(cy + s), 0xFFFF);
}

function drawZ(x, y, s, now) {   // "Z" com 3 tracos, flutuando
    var bob = Math.round(Math.sin(now / 400) * 2);
    System.drawLine(PX(x), PY(y + bob), PX(x + s), PY(y + bob), 0xFFFF);
    System.drawLine(PX(x + s), PY(y + bob), PX(x), PY(y + s + bob), 0xFFFF);
    System.drawLine(PX(x), PY(y + s + bob), PX(x + s), PY(y + s + bob), 0xFFFF);
}

function drawMood(now) {
    if (mood === "love") {
        drawHeart(38, 22, 14);
        drawHeart(90, 22, 14);
    } else if (mood === "angry") {
        drawEyes(true, 8);
        // sobrancelhas em V (grossas, caindo pro centro)
        System.fillTriangle(PX(14), PY(8), PX(52), PY(15), PX(14), PY(15), 0xFFFF);
        System.fillTriangle(PX(114), PY(8), PX(76), PY(15), PX(114), PY(15), 0xFFFF);
    } else if (mood === "sad") {
        drawEyes(true, 6);
        var ty = 30 + (((now / 350) | 0) % 6);   // lagrima caindo
        System.fillRect(PX(98), PY(ty), PW_(3), PH_(4), 0xFFFF);
    } else if (mood === "sleepy") {
        drawEyes(false, 0);
        drawZ(96, 8, 9, now);
        drawZ(110, 1, 6, now);
    } else {   // curious / alert: olhos abertos, pupilas grandes
        drawEyes(true, mood === "alert" ? 13 : 12);
    }
    System.fillRect(PX(60), PY(36), PW_(8), PH_(3), 0xFFFF);   // nariz
}

// arco de cima (leque de WiFi) em segmentos, coordenadas do vidro 128x64:
// sem apagar nada embaixo (a cara mora ali)
function wifiArc(cx, cy, r) {
    var px = cx + r * Math.cos(Math.PI * 1.20), py = cy + r * Math.sin(Math.PI * 1.20);
    for (var a = 1.30; a <= 1.81; a += 0.10) {
        var x = cx + r * Math.cos(Math.PI * a), y = cy + r * Math.sin(Math.PI * a);
        System.drawLine(PX(px), PY(py), PX(x), PY(y), 0xFFFF);
        px = x; py = y;
    }
}

function drawStatus() {
    // bateria: canto superior DIREITO (deixa o alto-esquerdo limpo pra cara)
    if (lastBatt >= 0) {
        var frac = (lastBatt - 3300) / (4200 - 3300);
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        System.drawRect(PX(100), PY(2), PW_(20), PH_(8), 0xFFFF);
        System.fillRect(PX(121), PY(4), PW_(2), PH_(4), 0xFFFF);   // bico
        System.fillRect(PX(102), PY(4), Math.max(1, PW_(16 * frac)), PH_(4), 0xFFFF);
        if (frac < 0.15 && ((System.millis() / 500) | 0) % 2 === 0) {
            System.fillRect(PX(100), PY(2), PW_(23), PH_(8), 0xFFFF);  // pisca
        }
    }
    // link BLE: pontinho aceso quando conectado (esquerda da bateria)
    if (linkUp) System.fillCircle(PX(93), PY(6), PW_(2), 0xFFFF);
    // internet: leque de WiFi (3 arcos + ponto) a esquerda do link
    if (netUp) {
        System.fillCircle(PX(74), PY(9), PW_(1), 0xFFFF);
        wifiArc(74, 9, 4);
        wifiArc(74, 9, 7);
    }
    // saude dos servos: so aparece se algo falhar (canto alto-esquerdo)
    if (!SERVO || servoFails > 0) {
        System.fillRect(PX(3), PY(3), PW_(2), PH_(5), 0xFFFF);
        System.fillRect(PX(7), PY(3), PW_(2), PH_(5), 0xFFFF);
    }
    if (lastMic > 0) {
        System.fillRect(PX(0), PY(62), Math.min(PW, Math.round(lastMic * PW / 100)), PH_(2), 0xFFFF);
    }
    // wake word escutando: microfone pequeno a esquerda do ponto do link —
    // aceso = pode dizer "hi celer"
    if (voiceReady && !voiceBusy) {
        System.fillRoundRect(PX(83), PY(2), PW_(4), PH_(6), PW_(2), 0xFFFF);
        System.fillRect(PX(84), PY(9), PW_(2), PH_(1), 0xFFFF);
    }
}

// Comando de voz na tela (o texto e ilegivel no vidro: so desenho).
// Gravando = microfone grande + barras que seguem o volume (fale agora);
// esperando a IA = tres pontos andando.
function drawVoice(now) {
    if (voiceRec) {
        System.fillRoundRect(PX(58), PY(12), PW_(12), PH_(24), PW_(6), 0xFFFF);
        System.drawRoundRect(PX(54), PY(24), PW_(20), PH_(18), PW_(9), 0xFFFF);
        System.fillRect(PX(63), PY(42), PW_(2), PH_(8), 0xFFFF);
        System.fillRect(PX(56), PY(50), PW_(16), PH_(2), 0xFFFF);
        for (var i = 0; i < 3; i++) {
            var h = Math.round(4 + (voiceLvl / 100) * (30 - i * 7));
            if (h > 34) h = 34;
            var y = 30 - Math.round(h / 2);
            System.fillRect(PX(40 - i * 8), PY(y), PW_(3), PH_(h), 0xFFFF);
            System.fillRect(PX(85 + i * 8), PY(y), PW_(3), PH_(h), 0xFFFF);
        }
        return;
    }
    var on = ((now / 250) | 0) % 3;
    for (var k = 0; k < 3; k++) {
        System.fillCircle(PX(46 + k * 18), PY(32), PW_(k === on ? 5 : 3), 0xFFFF);
    }
}

// Fala do cao na tela: balao + rabo + ate 5 caracteres em sete-segmentos
// (o texto inteiro vai pro controle pareado via link).
function drawSay() {
    System.drawRoundRect(PX(4), PY(6), PW_(120), PH_(42), PW_(8), 0xFFFF);
    System.fillTriangle(PX(28), PY(48), PX(40), PY(48), PX(33), PY(57), 0xFFFF);
    var n = sayText.length;
    var x0 = Math.round((PW - n * 14) / 2);
    for (var i = 0; i < n; i++) drawSegDigit(x0 + i * 14, 14, sayText.charAt(i));
}

function draw() {
    System.fillScreen(0);
    var now = System.millis();
    if (pairShow) {
        drawPair();      // codigo de pareamento no lugar da cara
    } else if (voiceBusy) {
        drawVoice(now);  // ouvindo o comando / esperando a IA
    } else if (now < sayUntil) {
        drawSay();       // respondendo (dog_say)
    } else if (now < happyUntil) {
        drawHappy();
    } else if (mood) {
        drawMood(now);   // emocao corrente (love/angry/sad/sleepy/...)
    } else {
        drawEyes(now >= blinkUntil, 10);
        System.fillRect(PX(60), PY(36), PW_(8), PH_(3), 0xFFFF);
    }
    drawStatus();
}

// ------------------------------------------------------- celer link -------
var linkUp = false;
var hasLink = (typeof CelerLink !== "undefined");
// API 11: pareamento por codigo — quem conecta digita o codigo que aparece
// AQUI na tela antes de poder mandar qualquer coisa. Controles ja
// pareados (memoria do firmware) entram direto.
if (hasLink) CelerLink.start("Celer-Dog", { pairing: true });

// Tela de pareamento: cadeado + codigo em sete-segmentos (texto e ilegivel
// no vidro). pairShow fica aceso enquanto o handshake pendura.
var pairShow = false, pairWas = false, pairCode = "";

// Segmentos de cada digito (a=topo, b/c=dir, d=base, e/f=esq, g=meio) e
// das letras que a fala do cao usa (OLA/SIM/NAO/87%...); M e Q nao dao
var SEG = {
    0: "abcdef", 1: "bc", 2: "abdeg", 3: "abcdg", 4: "bcfg",
    5: "acdfg", 6: "acdefg", 7: "abc", 8: "abcdefg", 9: "abcdfg"
};
var SEG7 = {
    A: "abcefg", B: "bcdef", C: "adef", D: "bcdeg", E: "adefg", F: "aefg",
    G: "acdef", H: "bcefg", I: "bc", L: "def", N: "ceg", O: "abcdef",
    P: "abefg", R: "eg", S: "acdfg", T: "defg", U: "bdef", Y: "bcdfg"
};
function drawSegDigit(x, y, ch) {
    var on = SEG[ch - "0"] || SEG7[ch] || SEG[8];
    // celula 12x22 fisica, trilho 2px; horizontal no meio da largura
    if (on.indexOf("a") >= 0) System.fillRect(PX(x + 2), PY(y), PW_(8), PH_(2), 0xFFFF);
    if (on.indexOf("g") >= 0) System.fillRect(PX(x + 2), PY(y + 10), PW_(8), PH_(2), 0xFFFF);
    if (on.indexOf("d") >= 0) System.fillRect(PX(x + 2), PY(y + 20), PW_(8), PH_(2), 0xFFFF);
    if (on.indexOf("f") >= 0) System.fillRect(PX(x), PY(y + 2), PW_(2), PH_(8), 0xFFFF);
    if (on.indexOf("b") >= 0) System.fillRect(PX(x + 10), PY(y + 2), PW_(2), PH_(8), 0xFFFF);
    if (on.indexOf("e") >= 0) System.fillRect(PX(x), PY(y + 12), PW_(2), PH_(8), 0xFFFF);
    if (on.indexOf("c") >= 0) System.fillRect(PX(x + 10), PY(y + 12), PW_(2), PH_(8), 0xFFFF);
}

// Cadeado pulsando (o codigo expira em 60 s: chama atencao) + 6 digitos em
// 2 grupos de 3: 2*(3*12+2*2) + 8 de respiro = 88px, centrado no vidro.
function drawPair() {
    var now = System.millis();
    if (((now / 500) | 0) % 2 === 0) {  // corpo do cadeado pisca
        System.fillRect(PX(56), PY(11), PW_(16), PH_(9), 0xFFFF);
        System.fillRect(PX(59), PY(14), PW_(10), PH_(3), 0);      // buraco da fechadura
        System.fillRect(PX(59), PY(5), PW_(3), PH_(6), 0xFFFF);   // arco
        System.fillRect(PX(68), PY(5), PW_(3), PH_(6), 0xFFFF);
        System.fillRect(PX(59), PY(5), PW_(12), PH_(3), 0xFFFF);
    }
    for (var i = 0; i < 6; i++) {
        if (i >= pairCode.length) break;
        drawSegDigit(20 + i * 14 + (i >= 3 ? 8 : 0), 28, pairCode.charAt(i));
    }
}

function reply(obj) {
    if (hasLink && linkUp) CelerLink.send(obj);
}

// dir do Celer Remote -> gait (todas continuas, param no stop)
var MOVE2GAIT = { up: "walk", down: "back", left: "left", right: "right" };

function sendTel() {
    reply({ type: "tel", batt: lastBatt, mic: lastMic, state: gaitName || "stand",
            sleep: sleeping, mode: walkMode, modes: MODES_OK,
            wifi: canWifi, net: hasNet && Net.isConnected() });
}

// ---- WiFi pelo Celer Remote (API 21) ---------------------------------------
// O cao nao tem teclado: o Remote pede a lista de redes que ELE ve
// ({type:"wifi_scan"}, aberto — SSID nao e segredo) e manda a senha SELADA
// (CelerLink.sendSealed: AES-GCM com a chave do pareamento). A credencial so
// e aceita pelo pollSealed() — um {type:"wifi"} em texto aberto e ignorado.
var hasNet = (typeof Net !== "undefined" && typeof Net.isConnected === "function");
var netUp = false;  // internet no ar (icone na barra; relido a cada 2 s)
var netAt = 0;
var canWifi = hasLink && hasNet && typeof CelerLink.pollSealed === "function" &&
              typeof Net.wifiConnect === "function" && typeof Net.wifiScan === "function";

function wifiScanReply() {
    if (!canWifi) { reply({ type: "wifi_list", nets: [], err: "sem suporte" }); return; }
    stopGait();
    var list = Net.wifiScan() || [];
    list.sort(function (a, b) { return b.rssi - a.rssi; });
    var nets = [], seen = {};
    for (var i = 0; i < list.length; i++) {
        var n = list[i];
        if (!n.ssid || seen[n.ssid]) continue;
        seen[n.ssid] = true;
        nets.push([String(n.ssid).substring(0, 24), n.rssi, n.secure ? 1 : 0]);
        // cabe numa mensagem do link (240 B) com folga para o envelope
        if (JSON.stringify({ type: "wifi_list", nets: nets }).length > 220) { nets.pop(); break; }
    }
    reply({ type: "wifi_list", nets: nets });
}

function wifiApply(m) {
    if (!canWifi || !m.ssid) return;
    stopGait();
    System.neopixel(0, [0x00A0A0, 0x00A0A0, 0x00A0A0, 0x00A0A0]);  // ciano: conectando
    System.neopixel(1, [0x00A0A0, 0x00A0A0, 0x00A0A0, 0x00A0A0]);
    var ok = Net.wifiConnect(String(m.ssid), String(m.pass || ""));  // salva + conecta (ate 15 s)
    m.pass = null;
    System.neopixel(0, [0, 0, 0, 0]);
    System.neopixel(1, [0, 0, 0, 0]);
    if (ok) { happyUntil = System.millis() + 1400; netUp = true; }
    System.print("[wifi] " + m.ssid + ": " + (ok ? "conectado " + System.getIPAddress() : "falhou"));
    reply({ type: "wifi_res", ok: !!ok, ssid: String(m.ssid), ip: ok ? System.getIPAddress() : "" });
}

function handleMsg(m) {
    if (m && m.type === "wifi_scan") { wifiScanReply(); return; }
    if (!m || !m.type) {
        // compat: forma antiga {cmd:"gait"|"stop"|"pet"|"info"}
        if (m && m.cmd === "gait") {
            seqClear();
            if (startGait(String(m.name), !!m.repeat)) {
                linkGait = !!m.repeat;
                happyUntil = System.millis() + 800;
            }
        } else if (m && m.cmd === "stop") {
            seqClear();
            stopGait();
        } else if (m && m.cmd === "pet") {
            setMood("happy", 1600);
        } else if (m && m.cmd === "info") {
            sendTel();
        }
        return;
    }
    switch (m.type) {
        case "move":
            // O Remote repete o move a cada ~250 ms enquanto a seta esta
            // pressionada: a mesma gait em curso so renova o keepalive
            // (reiniciar no quadro 0 travaria o passo no meio).
            seqClear();   // o D-pad manda: interrompe truque/sequencia
            var g = MOVE2GAIT[m.dir] || "walk";
            var same = gaitName === g && repeatGait && linkDrive;
            moveAt = System.millis();
            if (startGait(g, true)) {
                linkDrive = true;
                if (!same) happyUntil = System.millis() + 600;
            }
            break;
        case "stop":
            seqClear();
            stopGait();
            break;
        case "pet":
            setMood("happy", 1600);
            break;
        case "trick":
            // {"type":"trick","name":"dance"} — truque coreografado (o
            // Remote 1.5 e a voz chamam pela mesma porta)
            var tok = runTrick(m.name);
            reply({ type: "trick_res", ok: !!tok, name: String(m.name || "") });
            break;
        case "gait":
            if (startGait(String(m.name), !!m.repeat)) {
                linkGait = !!m.repeat;
                happyUntil = System.millis() + 800;
            }
            break;
        case "info":
            sendTel();
            break;
        case "calib":
            calibrate();
            break;
        case "mode":
            // {"type":"mode","walk":"esphi"|"creep"} nomeado ou sem walk =
            // proximo das marchas liberadas (MODES_OK); escolha fica salva
            // no dogtune.json (junto com a marca das pernas atuais)
            if (gait !== null) stopGait();
            var wm = WALK_MODES.indexOf(String(m.walk));
            walkMode = wm >= 0 ? WALK_MODES[wm]
                : MODES_OK[(MODES_OK.indexOf(walkMode) + 1) % MODES_OK.length];
            System.print('[dog] marcha: ' + walkMode);
            saveTune();
            sendTel();
            break;
        case "tune":
            // {"type":"tune","lean":20,"flip":true,"P":20,"T":25,"power":600,
            //  "tilt":180,"swing":240,"order":["BL","FL","BR","FR"]} — vale no
            // proximo ciclo e fica salvo (lean/flip ja valem na hora)
            applyTune(m);
            saveTune();
            reply({ type: "tune", lean: LEAN, P: CREEP.P, T: CREEP.T, power: CREEP.power,
                    tilt: CREEP.tilt, swing: CREEP.swing, order: CREEP.order, mode: walkMode,
                    flip: FLIP });
            break;
    }
}

// ------------------------------------------------------------ voz -------
// Comandos por voz (API 20): "hi celer" detectado NO CHIP (WakeWord, modelo
// proprio microWakeWord) abre uma janela de gravacao; o audio vai pro
// qwen omni (AI.chat com tools) e o tool_call dog_command dispara o gait —
// "hi celer, senta" faz sentar. Tudo feature-detect: sem WakeWord (build
// sem CELEROS_WAKE_WORD), sem microfone ou sem IA o cao segue igual.
var voiceReady = (typeof WakeWord !== "undefined" && typeof Mic !== "undefined");
var voiceBusy = false;        // da janela de escuta ate a resposta da IA
var voiceRec = false;
var voiceHeard = false;       // nivel subiu ao menos 1x (tem alguem falando)
var voiceQuietAt = 0;
var voiceWalkUntil = 0;       // andar por voz dura no maximo 3 s (sem keepalive)
var VOICE_WALK_MS = 3000;
var hasAI = (typeof AI !== "undefined" && typeof Net !== "undefined");
var voiceErrorAt = 0;
var voiceLvl = 0;             // volume ao vivo da janela de gravacao (tela)

function cueVoice(ok) {
    // ack do wake: duas notas subindo (ouvindo) ou duas graves (erro)
    // (playTone nao aceita frequencia 0 como pausa: RangeError derrubava o
    // app justo no momento do wake)
    if (ok) System.playTone([[900, 60], [1350, 90]]);
    else System.playTone([[400, 90], [330, 120]]);
}

function voiceRing(on, err) {
    // anel de LED: azul = ouvindo/pensando, vermelho = erro (os anims do
    // repertorio 2.0 seguem no ledTick; aqui o quadro sai na hora)
    setLeds(err ? "err" : (on ? "listen" : "off"));
    if (on) {
        var c = err ? LED_COLORS.err : LED_COLORS.listen;
        System.neopixel(0, [c, c, c, c]);
        System.neopixel(1, [c, c, c, c]);
    }
}

// "sentar"/"deitar"/... -> gait. O fallback por palavra-chave cobre o caso
// do modelo responder texto em vez de tool_call (e o PT e o EN).
var VOICE_GAITS = {
    sit: "sit", senta: "sit", sentar: "sit", sentado: "sit", "senta aí": "sit",
    lie: "lie", down: "lie", deita: "lie", deitar: "lie", deitado: "lie",
    "lay down": "lie",
    stand: "stand", levanta: "stand", levantar: "stand", em_pe: "stand", up: "stand",
    stretch: "stretch", alonga: "stretch", alongar: "stretch", bow: "stretch",
    walk: "walk", anda: "walk", andar: "walk", "vai": "walk", frente: "walk",
    back: "back", tras: "back", recua: "back",
    stop: "stop", para: "stop", pare: "stop", passo: "stop", quieta: "stop"
};

function voiceRunGait(cmd) {
    var name = VOICE_GAITS[String(cmd).toLowerCase()];
    System.print('[voz] comando "' + cmd + '" -> ' + (name || "desconhecido"));
    if (!name) return false;
    if (name === "stop") { stopGait(); voiceWalkUntil = 0; return true; }
    var cont = (name === "walk" || name === "back" || name === "left" || name === "right");
    if (startGait(name, cont)) {
        if (cont) voiceWalkUntil = System.millis() + VOICE_WALK_MS;
        happyUntil = System.millis() + 900;
        return true;
    }
    return false;
}

function voiceFromText(t) {
    if (!t) return false;
    t = t.toLowerCase();
    var keys = [];
    for (var k in VOICE_GAITS) keys.push(k);
    keys.sort(function (a, b) { return b.length - a.length; });
    for (var i = 0; i < keys.length; i++) {
        if (t.indexOf(keys[i]) >= 0) return voiceRunGait(keys[i]);
    }
    return false;
}

function voiceRequest(audioB64) {
    if (!hasAI) { voiceDone(false); return; }
    try {
        var started = AI.chat({
            provider: "openrouter",
            messages: [
                { role: "system", content: "Voce comanda um cachorro robotico. O audio e o dono falando um comando " +
                  "(em portugues ou ingles). Responda SEMPRE chamando a ferramenta dog_command com um unico comando. " +
                  "Se o audio estiver vazio, for so ruido ou nao for um pedido de movimento, use command none — " +
                  "nunca invente um movimento." },
                { role: "user", content: [
                    { type: "input_audio", input_audio: { data: audioB64, format: "wav" } }
                ]}
            ],
            tools: [{ type: "function", function: {
                name: "dog_command",
                description: "Executa um comando de movimento no cachorro",
                parameters: { type: "object", properties: {
                    command: { type: "string",
                               enum: ["sit", "lie", "stand", "stretch", "walk", "back", "stop", "none"] }
                }, required: ["command"] }
            } }],
            tool_choice: "auto",
            max_tokens: 150,
            reasoning: { effort: "low" }
        }, function (r) {
            var done = false;
            // o que o modelo respondeu (diagnostico no logcat: sem isto um
            // "nao deu certo" nao tinha como ser explicado)
            System.print('[voz] ia: ' + JSON.stringify({
                ok: r && r.ok, status: r && r.status, fim: r && r.finishReason,
                tools: r && r.toolCalls, txt: r && r.content ? String(r.content).substring(0, 160) : null,
                erro: r && r.error ? String(r.error).substring(0, 120) : null }));
            if (r && r.ok && r.toolCalls && r.toolCalls.length) {
                done = voiceRunGait(r.toolCalls[0].args && r.toolCalls[0].args.command);
            }
            if (!done && r && r.ok) done = voiceFromText(r.content);
            voiceDone(done);
        });
        if (!started) voiceDone(false);
    } catch (e) {
        System.print('[voz] erro: ' + e);
        voiceDone(false);
    }
}

function voiceDone(ok) {
    voiceBusy = false;
    voiceRing(false, !ok);
    cueVoice(ok);
    if (ok) happyUntil = System.millis() + 1600;
    lastActivity = System.millis();
    voiceErrorAt = ok ? 0 : System.millis();
}

function voiceTick(now) {
    if (!voiceReady) return;
    // janela de gravacao: fim por teto, por silencio pos-fala ou por tempo
    if (voiceRec) {
        var lvl = Mic.level();
        voiceLvl = lvl > 0 ? lvl : 0;
        if (lvl > 22) { voiceHeard = true; voiceQuietAt = 0; }
        else if (voiceHeard && lvl < 5) {
            if (!voiceQuietAt) voiceQuietAt = now + 550;
            else if (now >= voiceQuietAt) { voiceRec = false; voiceRequest(Mic.stop()); }
        }
        if (voiceRec && !Mic.recording()) { voiceRec = false; voiceRequest(Mic.stop()); }
        return;
    }
    // wake -> abre a janela (a task do detector descansa sozinha durante
    // a gravacao — micRecActive no firmware)
    if (!voiceBusy && (voiceErrorAt === 0 || now - voiceErrorAt > 2500) && WakeWord.poll()) {
        if (seqActive()) { seqClear(); stopGait(); }   // "hi celer" corta a dancinha
        voiceBusy = true;
        voiceHeard = false;
        voiceQuietAt = 0;
        lastActivity = now;
        cueVoice(true);
        voiceRing(true, false);
        if (Mic.start({ ms: 3500 })) {
            voiceRec = true;
        } else {
            voiceDone(false);
        }
    }
}

if (voiceReady) {
    if (WakeWord.start()) {
        System.print('[voz] wake word "hi celer" ativo');
    } else {
        System.print('[voz] wake word indisponivel (modelo/RAM)');
        voiceReady = false;
    }
}

var telAt = 0;
var moveAt = 0;
var MOVE_KEEPALIVE_MS = 900;  // sem move novo nesse prazo = para (failsafe)
function linkTick(now) {
    if (!hasLink) return;
    var st = CelerLink.status();
    linkUp = !!(st && st.connected);
    // Handshake pendente: status.code so existe no periferico (API 11);
    // firmware derruba sozinho em 60 s ou 3 codigos errados
    pairShow = !!(st && st.pairing && st.code);
    if (pairShow) pairCode = String(st.code);
    if (pairWas && !pairShow && linkUp) {
        happyUntil = System.millis() + 1400;  // codigo aceito: festa
    }
    pairWas = pairShow;
    if (pairShow) lastActivity = now;  // alguem vai digitar: sem dormir
    // esvazia a fila: o loop e lento (~60 ms + desenho) e o Remote manda
    // em rajadas; so um por volta acumularia comandos velhos
    for (var k = 0; k < 8; k++) {
        var msg = CelerLink.poll();
        if (msg === null || msg === undefined) break;
        if (msg === "") continue;
        var m = null;
        try { m = JSON.parse(msg); } catch (e) { m = null; }
        if (m) handleMsg(m);
    }
    // canal selado: so credenciais autenticadas com o pareamento
    if (canWifi) {
        for (var q = 0; q < 2; q++) {
            var sm = CelerLink.pollSealed();
            if (sm === null || sm === undefined) break;
            var sv = null;
            try { sv = JSON.parse(sm); } catch (e2) { sv = null; }
            sm = null;
            if (sv && sv.type === "wifi") wifiApply(sv);
        }
    }
    // failsafe: link caiu ou o controle parou de repetir o move (soltou a
    // seta e o stop se perdeu) -> o robo nao sai andando sozinho
    if (linkDrive && (!linkUp || System.millis() - moveAt > MOVE_KEEPALIVE_MS)) {
        stopGait();
    } else if (linkGait && !linkUp) {
        stopGait();
    }
    if (hasNet && now - netAt > 2000) {
        netAt = now;
        netUp = !!Net.isConnected();
    }
    // telemetria periodica enquanto conectado (o remote mostra batt/state)
    if (linkUp) lastActivity = now;
    if (linkUp && now - telAt > 1500) {
        telAt = now;
        sendTel();
    }
}

// ------------------------------------------------------------------ main ---
// despertar: pulso de centro em cada perna (servo solto nao segura posicao),
// olhos fecham e abrem devagar
legsHold();
System.delay(250);
blinkUntil = System.millis() + 900;
happyUntil = System.millis() + 1400;

var FRAME_MS = 60;
var SLEEP_MS = 120000;      // 2 min sem comando/toque e sem link
var lastLoop = System.millis();
while (true) {
    var now = System.millis();
    var dt = now - lastLoop;
    lastLoop = now;
    if (dt > 150) dt = 150;     // volta travada (scan, flush longo): sem salto de pose

    if (now >= blinkAt) {
        blinkUntil = now + 140;
        blinkAt = now + 1600 + Math.floor(Math.random() * 3200);
    }
    if (moveDir !== null) {
        lookX = moveDir;            // andando: olhos na direcao do movimento
        lookY = 0;
    } else if (now >= lookT) {
        lookT = now + 700 + Math.floor(Math.random() * 1500);
        lookX = Math.random() * 2 - 1;
        lookY = Math.random() * 1.4 - 0.7;
    }
    breath += 0.13 * dt / 60;

    // sono: sem comando/toque e sem link = olhos fechados, servos soltos
    // (parado em pe o servo gasta corrente segurando), mic mais espacado
    if (!sleeping && now - lastActivity > SLEEP_MS && !linkUp && !pairShow && !gait && !seqActive()) {
        sleeping = true;
        seqClear();
        ledsOff();
        legsRelease();
    }
    if (sleeping && (linkUp || pairShow || happyUntil > now)) {
        sleeping = false;
        lastActivity = now;
        if (legsLimp) { legsLimp = false; legsHold(); }
    }

    // andando: mic mede o ruido dos proprios servos, e cada leitura atrasa
    // a proxima fase — pula
    if (!walking() && !voiceRec && now - micAt > (sleeping ? 1500 : 400)) {
        micAt = now;
        lastMic = System.micLevel();
        if (lastMic < 0) lastMic = 0;
        if (sleeping && lastMic > 45) happyUntil = now + 1200;   // barulho acorda
        else if (lastMic > 30 && happyUntil < now) lookY = -1;
    }
    if (now - battAt > 10000) {
        battAt = now;
        lastBatt = System.battery();
        // primeira vez abaixo de 20%: ganido + cara triste (uma vez so;
        // rearma acima de 25% — histerese contra o ruido do divisor)
        if (battPct() < 20 && !battWarned) {
            battWarned = true;
            playBark("whine", 1);
            setMood("sad", 2500);
        } else if (battPct() > 25) battWarned = false;
    }
    // pad lido toda volta; a soltura so vale apos 2 leituras soltas
    // seguidas (um 0 espurio no meio do toque nao vira "carinho")
    var pad = System.touchPad();
    if (pad === 1) {
        padZeros = 0;
        if (!padDownAt) padDownAt = now;
        padHeld = true;
    } else if (padHeld && ++padZeros >= 2) {
        // soltou: >5s = esquece os controles pareados; longo (>600ms) =
        // ciclo de gait; curto = carinho
        lastActivity = now;
        if (padDownAt && now - padDownAt > 5000) {
            if (hasLink && CelerLink.unpair) CelerLink.unpair();
            System.print('[dog] pareamentos esquecidos (hold 5s)');
            happyUntil = now + 2000;
        } else if (padDownAt && now - padDownAt > 600) cycleGait();
        else happyUntil = now + 1600;
        padHeld = false;
        padDownAt = 0;
        padZeros = 0;
    }

    voiceTick(now);
    if (voiceWalkUntil && now > voiceWalkUntil) {
        voiceWalkUntil = 0;
        seqClear();
        stopGait();
    }
    linkTick(now);
    if (mood && now > moodUntil) mood = "";
    ledTick(now);
    seqTick(now);   // sequenciador roda ANTES da gait: o passo move ja vale
    gaitTick(dt);
    // andando, o flush do OLED (~100 ms no I2C) entre as fases viraria
    // pausa extra no meio do passo: desenha so uma vez por ciclo
    if (!walking() || gait.frames || gaitPhase === 0) draw();
    var spent = System.millis() - now;
    System.delay(spent < FRAME_MS - 10 ? FRAME_MS - spent : 10);
}
