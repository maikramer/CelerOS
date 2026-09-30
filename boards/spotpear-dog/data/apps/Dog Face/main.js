// CelerOS Dog Face — a cara do cao robotico (API 10)
// ES5 puro (Duktape). Desenhada para o OLED 128x64 do spotpear-dog: o canvas
// virtual 240x320 escala x0.53/y0.20, entao TEXTO e ilegivel — a interface e
// 100% iconografica (olhos, pupilas, barras). Coordenadas: helper P() mapeia
// fisico(128x64) -> virtual, para raciocinar em pixels reais do vidro.
//
// Tambem e o "cerebro" do robo: gaits das 4 perninhas (maquina de estados,
// um quadro por volta do loop) e servidor Celer Link (comandos JSON via BLE
// pelo app Celer Remote no SmartDisplay).
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
// Por que a marcha antiga nao saia do lugar: as duas metades eram
// espelhos exatos (tesoura simetrica), entao o atrito de uma cancelava o
// da outra e o corpo so balancava. A do ESP-Hi e ASSIMETRICA: o
// STEP_OFFSET desloca a faixa de duas pernas em 5 graus e elas dao um
// salto de ~10 graus na troca de fase — a assimetria e o que empurra.
//
// Offsets sao crus em relacao ao neutro, na convencao de sinais do ESP-Hi
// (FL e BR "pra frente" = angulo menor; FR e BL = angulo maior).
//
// SIGN espelhado: o ESP-Hi assume uma montagem "torcida" (FL/BR giram num
// sentido, FR/BL no outro). O ZZPET NAO e assim: os servos de um lado sao
// montados espelhados (o firmware-irmao xiaozhi-pet, mesma familia do stock,
// aplica 180-angulo nos servos DIREITOS), e no comando o mesmo offset move
// as 4 patas na MESMA direcao fisica. Sem inverter FR/BL aqui, o pace do
// ESP-Hi degenera em "dianteiras juntas vs traseiras juntas": os atritos se
// cancelam e o corpo so balanca pra frente e pra tras sem sair do lugar —
// o sintoma da marcha 1.2.0.
//
// CALIBRACAO: mande {"type":"calib"} pelo Celer Remote/nRF Connect — as
// QUATRO patas (FL, FR, BL, BR, nessa ordem) devem ir 25 graus pra FRENTE
// e voltar. Pata indo pra tras = inverta o SIGN dela; ajuste NEUTRAL ate o
// cao ficar reto em pe.
var PIN = { FL: 17, FR: 13, BL: 18, BR: 14 };
var KEYS = ["FL", "FR", "BL", "BR"];
var NEUTRAL = { FL: 90, FR: 90, BL: 90, BR: 90 };   // trim por perna
var SIGN = { FL: 1, FR: -1, BL: -1, BR: 1 };  // espelho dos FR/BL (ver acima)

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
    var a = Math.round(NEUTRAL[k] + SIGN[k] * o);
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

// Posturas (alvo da rampa). lay_down do ESP-Hi = 60 graus "pra frente";
// bow/lean_back usam o BOW_OFFSET dele (nao publicado no header: 30 aqui,
// ajuste na calibracao).
var BOW = 30;
var POSES = {
    stand: [0, 0, 0, 0],
    lie: [-60, 60, 60, -60],            // servo_dog_lay_down
    stretch: [-BOW, BOW, -BOW, BOW],    // servo_dog_bow (play bow)
    sit: [BOW, -BOW, BOW, -BOW]         // servo_dog_lean_back
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
// vertical. Cru = FWD * a (FWD = sinal de F, "frente" do ESP-Hi); o SIGN
// la em cima cuida do espelho da montagem.
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
// FWD invertido em relacao a F do ESP-Hi: validado no cao — com o sinal do
// ESP-Hi a pata erguida POUSAVA ATRAS (a recuperacao ia pra tras e a
// remada empurrava ao contrario). FLIP (tune "flip") inverte ao vivo.
var FWD = { FL: 1, FR: -1, BL: -1, BR: 1 };
var FLIP = 1;
function putA(k, a) { put(k, FLIP * FWD[k] * a); }
function angA(k) { return FLIP * FWD[k] * off[k]; }

var UNLOAD = { FR: "BL", FL: "BR", BL: "FR", BR: "FL" };
var CREEP = { P: 20, T: 25, power: 600, tilt: 180, swing: 240, order: ["BL", "FL", "BR", "FR"] };
var DIRS = {
    walk: { FL: 1, FR: 1, BL: 1, BR: 1 },
    back: { FL: -1, FR: -1, BL: -1, BR: -1 },
    left: { FL: -1, FR: 1, BL: -1, BR: 1 },    // lado direito empurra, esquerdo recua
    right: { FL: 1, FR: -1, BL: 1, BR: -1 }
};
var WALK_MODES = ["creep", "esphi"];
var walkMode = "creep";
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
    if (t.flip === true) FLIP = -FLIP;              // alterna
    else if (t.flip === 1 || t.flip === -1) FLIP = t.flip;
    if (t.mode && WALK_MODES.indexOf(t.mode) >= 0) walkMode = t.mode;
}

function saveTune() {
    if (typeof FS === "undefined" || !FS.writeTextFile) return;
    try {
        FS.writeTextFile(TUNE_FILE, JSON.stringify({
            P: CREEP.P, T: CREEP.T, power: CREEP.power, tilt: CREEP.tilt,
            swing: CREEP.swing, order: CREEP.order, mode: walkMode, flip: FLIP
        }));
    } catch (e) {}
}

function loadTune() {
    if (typeof FS === "undefined" || !FS.exists || !FS.exists(TUNE_FILE)) return;
    try { applyTune(JSON.parse(FS.readTextFile(TUNE_FILE))); } catch (e) {}
}
loadTune();

// ------------------------------------------------------------ estado ------
var blinkAt = System.millis() + 1800;
var blinkUntil = 0;
var happyUntil = 0;
var lookX = 0, lookY = 0;
var lookT = System.millis() + 900;
var breath = 0;
var lastMic = 0, micAt = 0;
var lastBatt = -1, battAt = -100000;
var padZeros = 0;           // leituras soltas seguidas (debounce da soltura)
var lastActivity = System.millis();
var sleeping = false;
var padHeld = false, padDownAt = 0;
var GAIT_CYCLE = ["stand", "walk", "sit", "lie", "stretch"];
var gaitIdx = 0;

function cycleGait() {
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
    // saude dos servos: so aparece se algo falhar (canto alto-esquerdo)
    if (!SERVO || servoFails > 0) {
        System.fillRect(PX(3), PY(3), PW_(2), PH_(5), 0xFFFF);
        System.fillRect(PX(7), PY(3), PW_(2), PH_(5), 0xFFFF);
    }
    if (lastMic > 0) {
        System.fillRect(PX(0), PY(62), Math.min(PW, Math.round(lastMic * PW / 100)), PH_(2), 0xFFFF);
    }
}

function draw() {
    System.fillScreen(0);
    var now = System.millis();
    if (now < happyUntil) {
        drawHappy();
    } else {
        drawEyes(now >= blinkUntil, 10);
        System.fillRect(PX(60), PY(36), PW_(8), PH_(3), 0xFFFF);
    }
    drawStatus();
}

// ------------------------------------------------------- celer link -------
var linkUp = false;
var hasLink = (typeof CelerLink !== "undefined");
if (hasLink) CelerLink.start("Celer-Dog");

function reply(obj) {
    if (hasLink && linkUp) CelerLink.send(obj);
}

// dir do Celer Remote -> gait (todas continuas, param no stop)
var MOVE2GAIT = { up: "walk", down: "back", left: "left", right: "right" };

function sendTel() {
    reply({ type: "tel", batt: lastBatt, mic: lastMic, state: gaitName || "stand",
            sleep: sleeping, mode: walkMode });
}

function handleMsg(m) {
    if (!m || !m.type) {
        // compat: forma antiga {cmd:"gait"|"stop"|"pet"|"info"}
        if (m && m.cmd === "gait") {
            if (startGait(String(m.name), !!m.repeat)) {
                linkGait = !!m.repeat;
                happyUntil = System.millis() + 800;
            }
        } else if (m && m.cmd === "stop") {
            stopGait();
        } else if (m && m.cmd === "pet") {
            happyUntil = System.millis() + 1600;
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
            var g = MOVE2GAIT[m.dir] || "walk";
            var same = gaitName === g && repeatGait && linkDrive;
            moveAt = System.millis();
            if (startGait(g, true)) {
                linkDrive = true;
                if (!same) happyUntil = System.millis() + 600;
            }
            break;
        case "stop":
            stopGait();
            break;
        case "pet":
            happyUntil = System.millis() + 1600;
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
            // {"type":"mode","walk":"creep"|"esphi"} ou sem walk = proximo
            if (gait !== null) stopGait();
            var wm = WALK_MODES.indexOf(String(m.walk));
            walkMode = wm >= 0 ? WALK_MODES[wm]
                : WALK_MODES[(WALK_MODES.indexOf(walkMode) + 1) % WALK_MODES.length];
            System.print('[dog] marcha: ' + walkMode);
            saveTune();
            sendTel();
            break;
        case "tune":
            // {"type":"tune","P":20,"T":25,"power":600,"tilt":180,"swing":240,
            //  "order":["BL","FL","BR","FR"]} — vale no proximo ciclo e fica salvo
            applyTune(m);
            saveTune();
            reply({ type: "tune", P: CREEP.P, T: CREEP.T, power: CREEP.power,
                    tilt: CREEP.tilt, swing: CREEP.swing, order: CREEP.order, mode: walkMode,
                    flip: FLIP });
            break;
    }
}

var telAt = 0;
var moveAt = 0;
var MOVE_KEEPALIVE_MS = 900;  // sem move novo nesse prazo = para (failsafe)
function linkTick(now) {
    if (!hasLink) return;
    var st = CelerLink.status();
    linkUp = !!(st && st.connected);
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
    // failsafe: link caiu ou o controle parou de repetir o move (soltou a
    // seta e o stop se perdeu) -> o robo nao sai andando sozinho
    if (linkDrive && (!linkUp || System.millis() - moveAt > MOVE_KEEPALIVE_MS)) {
        stopGait();
    } else if (linkGait && !linkUp) {
        stopGait();
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
    if (!sleeping && now - lastActivity > SLEEP_MS && !linkUp && !gait) {
        sleeping = true;
        legsRelease();
    }
    if (sleeping && (linkUp || happyUntil > now)) {
        sleeping = false;
        lastActivity = now;
        if (legsLimp) { legsLimp = false; legsHold(); }
    }

    // andando: mic mede o ruido dos proprios servos, e cada leitura atrasa
    // a proxima fase — pula
    if (!walking() && now - micAt > (sleeping ? 1500 : 400)) {
        micAt = now;
        lastMic = System.micLevel();
        if (lastMic < 0) lastMic = 0;
        if (sleeping && lastMic > 45) happyUntil = now + 1200;   // barulho acorda
        else if (lastMic > 30 && happyUntil < now) lookY = -1;
    }
    if (now - battAt > 10000) {
        battAt = now;
        lastBatt = System.battery();
    }
    // pad lido toda volta; a soltura so vale apos 2 leituras soltas
    // seguidas (um 0 espurio no meio do toque nao vira "carinho")
    var pad = System.touchPad();
    if (pad === 1) {
        padZeros = 0;
        if (!padDownAt) padDownAt = now;
        padHeld = true;
    } else if (padHeld && ++padZeros >= 2) {
        // soltou: longo (>600ms) = ciclo de gait; curto = carinho
        lastActivity = now;
        if (padDownAt && now - padDownAt > 600) cycleGait();
        else happyUntil = now + 1600;
        padHeld = false;
        padDownAt = 0;
        padZeros = 0;
    }

    linkTick(now);
    gaitTick(dt);
    // andando, o flush do OLED (~100 ms no I2C) entre as fases viraria
    // pausa extra no meio do passo: desenha so uma vez por ciclo
    if (!walking() || gait.frames || gaitPhase === 0) draw();
    var spent = System.millis() - now;
    System.delay(spent < FRAME_MS - 10 ? FRAME_MS - spent : 10);
}
