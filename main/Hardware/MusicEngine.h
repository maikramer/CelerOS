#pragma once

// Motor chiptune do System.playMusic (API 25): a LLM orquestra um "MIDI de
// poucos canais" — ate 4 trilhas de notas [midi, semicolcheias] (0 = pausa)
// misturadas ao vivo no alto-falante I2S. Este header e a PARTE PURA
// (compilacao da musica em eventos + renderizacao por amostra, inteira e
// deterministica): roda no host (test/cpp) e no firmware sem incluir nada
// do ESP — a cola de task/I2S vive em MusicSynth.cpp.
//
// Timbres: onda quadrada 50% (tema), 25% (2o voz, mais suave), triangular
// (baixo) e dente-de-serra (tema brilhante). Percussao (trilha drum:true)
// usa as notas GM: <=36 bumbo (senoide com varredura de afinacao),
// 37..41 caixa (ruido + tom) e >=42 chimbal (ruido curto).
//
// Tudo clampado aqui (o binding nao confia no app): bpm 60..200, loops
// 1..8 com teto total de 120 s, 4 trilhas x 48 notas, midi 0..96,
// duracao 1..64 semicolcheias, volume 0..100.

#include <stdint.h>
#include <stddef.h>

namespace MusicEngine {

constexpr int kMaxTracks = 4;
constexpr int kMaxNotes = 48;      // por trilha
constexpr int kMinBpm = 60, kMaxBpm = 200;
constexpr int kMaxLoops = 8;
constexpr uint32_t kMaxTotalMs = 120000;

enum Wave : uint8_t { kWaveSq = 0, kWaveSq25, kWaveTri, kWaveSaw };

struct Note { uint8_t midi; uint8_t len16; };  // midi 0 = pausa

struct Track {
    bool drum = false;   // notas viram percussao GM (wave ignorada)
    uint8_t wave = kWaveSq;
    uint8_t vol = 80;    // 0..100
    uint8_t count = 0;
    Note notes[kMaxNotes];
};

struct Song {
    uint16_t bpm = 120;
    uint8_t loops = 4;   // repeticoes da volta inteira
    uint8_t nTracks = 0;
    Track tracks[kMaxTracks];
};

// Evento compilado: nota com inicio/fim em AMOSTRAS dentro de UMA volta
// (ordenados; pausas ficam entre eventos). start==end seria nota nula.
struct Ev { uint32_t start, end; uint8_t midi; };

struct TrackEv {
    uint8_t vol = 80;    // 0..256 (vol% escalado)
    bool drum = false;
    uint8_t wave = kWaveSq;
    uint16_t count = 0;
    Ev evs[kMaxNotes];
};

struct Compiled {
    uint32_t rate = 0;          // taxa do canal (16k codec / 44,1k amp direto)
    uint32_t loopSamples = 0;   // volta = trilha mais longa
    uint32_t totalSamples = 0;  // loopSamples x loops (teto de 120 s corta loops)
    uint8_t loops = 1;
    uint8_t nTracks = 0;
    TrackEv tracks[kMaxTracks];
};

// Valida/clampa e compila a musica. Retorna false se nao sobrou nenhuma
// trilha com nota (null-music nao acorda o alto-falante).
inline bool compile(const Song& in, uint32_t rate, Compiled& out) {
    if (rate < 8000 || in.nTracks == 0 || in.nTracks > kMaxTracks) return false;
    out = {};
    out.rate = rate;
    uint32_t bpm = in.bpm < kMinBpm ? kMinBpm : (in.bpm > kMaxBpm ? kMaxBpm : in.bpm);
    // semicolcheia em amostras: (60/bpm)/4 * rate, Q8 p/ nao truncar feio
    const uint32_t s16Samples = (uint32_t)(((uint64_t)rate * 60 * 256 / 4) / (bpm * 256));
    if (s16Samples == 0) return false;
    for (int t = 0; t < in.nTracks; t++) {
        const Track& src = in.tracks[t];
        TrackEv& dst = out.tracks[out.nTracks];
        dst.drum = src.drum;
        dst.wave = src.wave > kWaveSaw ? (uint8_t)kWaveSq : src.wave;
        dst.vol = src.vol > 100 ? 100 : src.vol;
        dst.vol = (uint8_t)(dst.vol * 256 / 100);
        uint32_t at = 0;
        for (int i = 0; i < src.count && i < kMaxNotes; i++) {
            uint32_t len = src.notes[i].len16;
            if (len < 1) len = 1;
            if (len > 64) len = 64;
            const uint32_t dur = len * s16Samples;
            if (src.notes[i].midi != 0) {  // pausa so avanca o tempo
                dst.evs[dst.count].start = at;
                dst.evs[dst.count].end = at + dur;
                dst.evs[dst.count].midi = src.notes[i].midi > 96 ? 96 : src.notes[i].midi;
                dst.count++;
            }
            at += dur;
        }
        if (dst.count > 0) {
            if (at > out.loopSamples) out.loopSamples = at;
            out.nTracks++;
        }
    }
    if (out.nTracks == 0 || out.loopSamples == 0) return false;
    uint32_t loops = in.loops < 1 ? 1 : (in.loops > kMaxLoops ? kMaxLoops : in.loops);
    const uint32_t totalMs = (uint32_t)(((uint64_t)out.loopSamples * loops * 1000) / rate);
    if (totalMs > kMaxTotalMs) loops = kMaxTotalMs * rate / 1000 / out.loopSamples;
    if (loops < 1) loops = 1;
    out.loops = (uint8_t)loops;
    out.totalSamples = out.loopSamples * loops;
    return true;
}

// ---------------------------------------------------------------- render ---
// Estado do mixador: uma voz por trilha com acumulador de fase Q32 (1 ciclo
// = 2^32), envelope por nota e LFSR compartilhado da percussao.
struct Renderer {
    const Compiled* c = nullptr;
    int32_t master = 20000;      // vol% x 200: 1 voz ~5000, 4 vozes ~20000
    uint32_t pos = 0;           // amostra absoluta desde o inicio
    uint32_t loopStart = 0;
    uint16_t idx[kMaxTracks] = {};
    uint32_t phase[kMaxTracks] = {};   // oscilador melodicico / fase do bumbo
    uint32_t freqStep[kMaxTracks] = {};  // por amostra (Q32), da nota corrente
    uint32_t noise = 0x12345u;  // LFSR da percussao

    void reset(const Compiled* song, int volPct) {
        c = song;
        pos = loopStart = 0;
        for (int i = 0; i < kMaxTracks; i++) { idx[i] = 0; phase[i] = 0; freqStep[i] = 0; }
        noise = 0x12345u;
        if (volPct < 0) volPct = 0;
        if (volPct > 100) volPct = 100;
        master = volPct * 200;   // mesmo territory de amplitude do playTone
    }

    static uint32_t midiStep(uint8_t midi, uint32_t rate) {  // Q32 por amostra
        // 440 Hz x 2^((m-69)/12): multiplicacao iterativa (sem pow — so na
        // troca de nota, mas mantem o header livre de math.h)
        double f = 440.0;
        int m = midi - 69;
        while (m > 0) { f *= 1.0594630943592953; m--; }
        while (m < 0) { f /= 1.0594630943592953; m++; }
        return (uint32_t)((uint64_t)(f * 4294967296.0) / rate);
    }

    // Amostra crua -256..256 de uma voz melodica
    inline int32_t waveSample(uint8_t wave, uint32_t ph) const {
        switch (wave) {
            case kWaveSq25: return (int32_t)((ph >> 30) == 0 ? 256 : -256);
            case kWaveTri: {
                const uint32_t t = ph >> 24;   // 0..255
                return (int32_t)(t < 128 ? t * 4 - 256 : 768 - (int32_t)t * 4);
            }
            case kWaveSaw: return (int32_t)((ph >> 24) * 2) - 256;
            default: return (int32_t)(ph & 0x80000000u ? -256 : 256);
        }
    }

    inline int32_t nextNoise() {
        // LFSR xorshift de 32 bits: ruido bruto da percussao
        uint32_t x = noise;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        noise = x;
        return (int32_t)(x >> 24) - 128;   // -128..127
    }

    inline static int32_t sinTbl(uint32_t ph) {  // fase Q32 -> -256..255
        static int16_t tbl[257];
        static bool init = false;
        if (!init) {
            for (int i = 0; i <= 256; i++)
                tbl[i] = (int16_t)(__builtin_sin(i * 6.283185307179586 / 256.0) * 256.0);
            init = true;
        }
        return tbl[ph >> 24];
    }

    // Envelope 0..256 da nota melodica: ataque 2 ms, release 4 ms no fim.
    inline int32_t noteEnv(uint32_t relStart, uint32_t relNow, uint32_t relEnd) const {
        const uint32_t atk = c->rate / 500;      // 2 ms
        const uint32_t rel = c->rate / 250;      // 4 ms
        int32_t e = 256;
        if (relNow - relStart < atk) e = (int32_t)((relNow - relStart) * 256 / (atk ? atk : 1));
        else if (relEnd - relNow < rel) e = (int32_t)((relEnd - relNow) * 256 / (rel ? rel : 1));
        return e < 0 ? 0 : e;
    }

    // Percussao GM relativa ao inicio da nota (dt em amostras).
    inline int32_t drumSample(uint8_t midi, uint32_t dt, uint32_t& ph) {
        const uint32_t rate = c->rate;
        if (midi <= 36) {  // bumbo: senoide 110->40 Hz com decaimento quadratico
            const uint32_t dur = rate / 5;                     // 200 ms
            if (dt >= dur) return 0;
            const uint32_t x = dt * 256 / dur;                 // 0..256
            const int32_t env = (int32_t)((256 - x) * (256 - x) / 256);
            const double f = 40.0 + 70.0 * (256 - x) / 256;
            ph += (uint32_t)(f * 4294967296.0 / rate);
            return sinTbl(ph) * env / 256;
        }
        if (midi <= 41) {  // caixa: ruido + tom de 190 Hz, decaimento longo
            const uint32_t dur = rate * 9 / 50;                // 180 ms
            if (dt >= dur) return 0;
            const uint32_t x = dt * 256 / dur;
            const int32_t env = (256 - x);
            ph += (uint32_t)(190.0 * 4294967296.0 / rate);
            return (nextNoise() * env / 128 + sinTbl(ph) * env / 256) / 2;
        }
        // chimbal: ruido curto e agudo
        const uint32_t dur = rate / 20;                        // 50 ms
        if (dt >= dur) return 0;
        const uint32_t x = dt * 256 / dur;
        return nextNoise() * (256 - x) / 64;                   // brilhante
    }

    // Renderiza n amostras MONO s16 (o chamador duplica p/ estereo do I2S).
    void render(int16_t* out, size_t n) {
        for (size_t i = 0; i < n; i++) {
            if (c->nTracks == 0 || pos >= c->totalSamples) { out[i] = 0; continue; }
            const uint32_t rel = pos - loopStart;
            if (rel >= c->loopSamples) {  // volta seguinte: bobina as trilhas
                loopStart += c->loopSamples;
                for (int t = 0; t < c->nTracks; t++) idx[t] = 0;
            }
            const uint32_t r = pos - loopStart;
            int32_t sum = 0;
            for (int t = 0; t < c->nTracks; t++) {
                const TrackEv& tr = c->tracks[t];
                // avanca ponteiro: pula eventos que ja acabaram
                while (idx[t] < tr.count && r >= tr.evs[idx[t]].end) idx[t]++;
                if (idx[t] >= tr.count) continue;  // silencio ate a proxima volta
                const Ev& e = tr.evs[idx[t]];
                if (r < e.start) continue;         // pausa antes da nota
                const uint32_t dt = r - e.start;
                int32_t s;
                if (tr.drum) {
                    s = drumSample(e.midi, dt, phase[t]);
                } else {
                    if (dt == 0) freqStep[t] = midiStep(e.midi, c->rate);
                    phase[t] += freqStep[t];
                    s = waveSample(tr.wave, phase[t]) *
                        noteEnv(e.start, r, e.end) / 256;
                }
                sum += s * (int32_t)tr.vol / 256;
            }
            pos++;
            int32_t v = sum * master / 1024;
            if (v > 24000) v = 24000;
            if (v < -24000) v = -24000;
            out[i] = (int16_t)v;
        }
    }
};

}  // namespace MusicEngine
