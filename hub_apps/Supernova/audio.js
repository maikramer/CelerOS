// audio.js — trilha chiptune (API 25) + relogio de batida + efeitos.
// A trilha e o metronomo do jogo: o motor sincroniza os spawns com
// audio.beat() (musicPos / bpm) e o fx pulsa as estrelas no bumbo.
// playTone e playMusic dividem o alto-falante: os efeitos curtos so
// tocam com a musica parada (menu, supernova, fim de jogo).

var BPM = 132;
var SIXTEENTH = 60000 / BPM / 4;   // ms de uma semicolcheia

// Lá menor, 2 compassos de loop. Bumbo/caixa/chimbal na bateria
// (notas GM 36/38/42), baixo em triângulo, melodia curta em quadrada
// e um arpejo sq25 no contratempo.
var SONG = {
    bpm: BPM,
    loops: 8,
    tracks: [
        { drum: true, vol: 100, notes: [
            [36, 2], [42, 1], [42, 1], [38, 2], [42, 1], [42, 1],
            [36, 1], [36, 1], [42, 1], [42, 1], [38, 2], [42, 2],
            [36, 2], [42, 1], [42, 1], [38, 2], [36, 1], [36, 1],
            [42, 1], [42, 1], [38, 1], [38, 1], [42, 1], [42, 1], [42, 2]
        ] },
        { wave: "tri", vol: 92, notes: [       // baixo: A F G A
            [45, 2], [45, 2], [57, 2], [45, 2], [45, 2], [57, 2], [45, 2], [45, 2],
            [41, 2], [41, 2], [53, 2], [41, 2], [41, 2], [53, 2], [41, 2], [41, 2],
            [43, 2], [43, 2], [55, 2], [43, 2], [43, 2], [55, 2], [43, 2], [43, 2],
            [45, 2], [45, 2], [57, 2], [45, 2], [45, 2], [57, 2], [45, 2], [45, 2]
        ] },
        { wave: "sq", vol: 68, notes: [        // melodia esparsa
            [69, 2], [0, 2], [72, 2], [0, 2], [76, 4], [74, 4],
            [72, 2], [0, 2], [69, 2], [0, 2], [71, 4], [69, 4],
            [72, 2], [0, 2], [76, 2], [0, 2], [79, 4], [77, 4],
            [76, 2], [74, 2], [72, 2], [71, 2], [69, 6], [0, 2]
        ] },
        { wave: "sq25", vol: 42, notes: [      // arpejo no contratempo
            [57, 1], [60, 1], [64, 1], [60, 1], [57, 1], [64, 1], [60, 1], [64, 1],
            [53, 1], [57, 1], [60, 1], [57, 1], [53, 1], [60, 1], [57, 1], [60, 1],
            [55, 1], [59, 1], [62, 1], [59, 1], [55, 1], [62, 1], [59, 1], [62, 1],
            [57, 1], [60, 1], [64, 1], [60, 1], [57, 1], [64, 1], [60, 1], [64, 1]
        ] }
    ]
};

var hasMusic = (typeof System.playMusic === "function");
var lastRestart = 0;

function start(fromMs) {
    if (!hasMusic) return false;
    try {
        if (fromMs > 0) return System.playMusic(SONG, { startMs: fromMs });
        return System.playMusic(SONG);
    } catch (e) { return false; }
}

function stop() {
    if (!hasMusic) return;
    try { System.musicStop(); } catch (e) {}
}

// Mantem a trilha rodando durante o jogo (loops acabam): retoma de onde
// parou (startMs, API 27) quando o firmware oferece.
function keepAlive(now) {
    if (!hasMusic) return;
    try {
        if (System.musicPos() < 0 && now - lastRestart > 400) {
            lastRestart = now;
            start(0);
        }
    } catch (e) {}
}

// Batidas (negras) desde o inicio da musica; -1 com a trilha parada.
// O motor cruza inteiros para sincronizar spawns; o fx pula a fracao.
function beat() {
    if (!hasMusic) return -1;
    try {
        var p = System.musicPos();
        if (p < 0) return -1;
        return p / (60000 / BPM);
    } catch (e) { return -1; }
}

// Efeitos curtos (so com o alto-falante livre — a musica usa o canal):
//   boom    — supernova detonada
//   hit     — perdeu um escudo
//   over    — fim de jogo
//   ui      — toque em botao do menu
//   record  — recorde batido
function sfx(name) {
    if (typeof System.playTone !== "function") return;
    var n = null;
    if (name === "boom") n = [[220, 90], [140, 120], [90, 160], [60, 260]];
    else if (name === "hit") n = [[300, 70], [180, 140]];
    else if (name === "over") n = [[392, 120], [330, 120], [262, 160], [196, 320]];
    else if (name === "ui") n = [[880, 25]];
    else if (name === "record") n = [[784, 80], [988, 80], [1319, 140]];
    if (!n) return;
    try { System.playTone(n); } catch (e) {}
}

module.exports = {
    start: start,
    stop: stop,
    keepAlive: keepAlive,
    beat: beat,
    sfx: sfx,
    SIXTEENTH: SIXTEENTH
};
