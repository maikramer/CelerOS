#!/usr/bin/env python3
"""Gerador dos latidos do Dog Face (bark_*.wav, 16 kHz mono 16-bit).

Sintese aditiva stdlib-only (math/random/wave): fundamental com varredura de
frequencia + harmonicos com decaimento proprio + rajada de ruido no ataque
(o "rough" do latido) + saturacao tanh (corpo). Deterministico: random.seed
fixo gera sempre os mesmos arquivos (WAVs sao commitados no app; rodar de
novo tem de reproduzir byte a byte o que esta no git).

    python3 tools/dog/barks.py          # (re)gera os 5 arquivos no app

O app toca com System.playWav("/local/apps/Dog Face/bark_<kind>.wav") e cai
para playTone se o arquivo nao existir (build antigo / imagem sem data novo).
"""
import math
import os
import random
import struct
import wave

SR = 16000
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "boards", "spotpear-dog", "data", "apps", "Dog Face")
random.seed(20261004)  # ruido reproduzivel


# ---------------------------------------------------------------- helpers ---
def env_asr(n, attack, release, sustain_level=1.0):
    """Envelope ataque-sustain-release em fracao da duracao total (0..1)."""
    out = []
    na = max(1, int(n * attack))
    nr = max(1, int(n * release))
    for i in range(n):
        if i < na:
            out.append(i / na)
        elif i >= n - nr:
            out.append(sustain_level * (n - 1 - i) / nr)
        else:
            out.append(sustain_level)
    return out


def env_decay(n, attack, curve):
    """Ataque linear + caida exponencial (latido curto: woof/yip)."""
    out = []
    na = max(1, int(n * attack))
    for i in range(n):
        if i < na:
            out.append(i / na)
        else:
            out.append(math.exp(-curve * (i - na) / (n - na)))
    return out


def tone(dur, freq, harms, vib_depth=0.0, vib_hz=0.0):
    """Varredura f(t) por amostra com fase acumulada por harmonico."""
    n = int(dur * SR)
    phase = [0.0] * len(harms)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        f = freq(t)
        if vib_depth:
            f += vib_depth * math.sin(2 * math.pi * vib_hz * t)
        for h, amp in enumerate(harms):
            if amp:
                out[i] += amp * math.sin(phase[h])
                phase[h] += 2 * math.pi * f * (h + 1) / SR
    return out


def noise_burst(n, sharp=45.0, gain=1.0):
    """Ruido branco com caida exponencial (ataque do latido / rouquidao)."""
    out = []
    dec = math.exp(-sharp / SR * 1000 / 1000 * 60)  # decai em ~dezenas de ms
    e = gain
    for _ in range(n):
        out.append(random.uniform(-1, 1) * e)
        e *= dec
    return out


def mix(base, extra, at=0.0, gain=1.0):
    off = int(at * SR)
    need = off + len(extra)
    while len(base) < need:
        base.append(0.0)
    for i, v in enumerate(extra):
        base[off + i] += v * gain
    return base


def save(name, samples, gain=0.85):
    # tanh satura o pico antes da normalizacao (corpo "rouco"), depois
    # normaliza para gain (0.85 = folga contra clipping no amplificador)
    sat = [math.tanh(v * 1.6) for v in samples]
    peak = max(1e-6, max(abs(v) for v in sat))
    g = gain / peak
    path = os.path.join(OUT_DIR, name)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(
            struct.pack("<h", int(max(-1.0, min(1.0, v * g)) * 32767)) for v in sat))
    print("%-16s %5.2f s  %6d bytes" % (name, len(samples) / SR, os.path.getsize(path)))


# ------------------------------------------------------------------ sons -----
def woof():
    """Latido cheio: pitch caindo 155->85 Hz e o corpo no ataque."""
    dur = 0.26
    body = tone(dur, lambda t: 155 - 70 * (t / dur), [1.0, 0.75, 0.5, 0.3, 0.15])
    env = env_decay(len(body), 0.05, 5.5)
    body = [b * e for b, e in zip(body, env)]
    return mix(body, noise_burst(int(0.030 * SR), sharp=50.0), 0.0, 0.35)


def yip():
    """Latidinho agudo curto (animacao)."""
    dur = 0.12
    body = tone(dur, lambda t: 780 - 350 * (t / dur), [1.0, 0.30, 0.12])
    env = env_decay(len(body), 0.04, 6.0)
    body = [b * e for b, e in zip(body, env)]
    return mix(body, noise_burst(int(0.015 * SR), sharp=60.0), 0.0, 0.22)


def growl():
    """Rosnado: grave sustentado com trinado (AM ~13 Hz) e ruido continuo."""
    dur = 0.55
    body = tone(dur, lambda t: 92, [1.0, 0.85, 0.65, 0.4, 0.2, 0.1],
                vib_depth=6.0, vib_hz=9.0)
    env = env_asr(len(body), 0.12, 0.25)
    out = []
    for i, (b, e) in enumerate(zip(body, env)):
        t = i / SR
        am = 0.55 + 0.45 * math.sin(2 * math.pi * 13 * t)  # trinado da garganta
        out.append(b * e * am)
    # rouquidao: ruido tracao baixa, modulado pelo mesmo trinado
    rough = noise_burst(len(out), sharp=0.8, gain=0.001)
    for i in range(len(out)):
        t = i / SR
        out[i] += rough[i] * (0.55 + 0.45 * math.sin(2 * math.pi * 13 * t + 0.7))
    return out


def whine():
    """Ganido: glissando sobe-desce com vibrato."""
    dur = 0.65

    def f(t):
        u = t / dur
        if u < 0.35:
            return 850 + 400 * (u / 0.35)
        return 1250 - 500 * ((u - 0.35) / 0.65)

    body = tone(dur, f, [1.0, 0.08], vib_depth=25.0, vib_hz=6.0)
    env = env_asr(len(body), 0.15, 0.30)
    return [b * e for b, e in zip(body, env)]


def howl():
    """Uivo: sobe e sustenta com vibrato lento."""
    dur = 1.25

    def f(t):
        u = t / dur
        return 380 + 260 * min(1.0, u / 0.24)

    body = tone(dur, f, [1.0, 0.45, 0.22, 0.10], vib_depth=14.0, vib_hz=5.5)
    env = env_asr(len(body), 0.12, 0.24)
    return [b * e for b, e in zip(body, env)]


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    save("bark_woof.wav", woof())
    save("bark_yip.wav", yip())
    save("bark_growl.wav", growl())
    save("bark_whine.wav", whine())
    save("bark_howl.wav", howl())
