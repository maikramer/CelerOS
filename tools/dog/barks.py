#!/usr/bin/env python3
"""Gerador dos latidos do Dog Face (bark_*.wav, 16 kHz mono 16-bit) — v2.

v2 (2026-10-04, "ta muito baixinho"): os primeiros latidos tinham
fundamental em 95-220 Hz — ABAIXO da resposta do alto-falante pequeno do
cao (a energia vira movimento de cone inaudivel). Redesenho de timbre +
masterizacao:

- Fundamentais uma oitava acima (400-1500 Hz), onde o driver entrega.
- Formante de latido real (pico de energia ~1-1.8 kHz) por harmonico
  ponderado, nao por filtro.
- Ataque com rajada de ruido em ALTA frequencia (o "rough" do latido).
- Masterizacao: high-pass 250 Hz (tira grave que o falante nao usa e libera
  headroom) > saturacao tanh (sobe o RMS, o volume percebido) >
  normalizacao do pico em 0.985. Alvo: RMS -11..-8 dBFS nos fortes.

Deterministico: random.seed fixo. Rodar de novo reproduz byte a byte (v2).

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
random.seed(20261005)  # jitter/vibrado reproduziveis (v2)


# ---------------------------------------------------------------- helpers ---
def glide(points, ms):
    """f0 no instante ms pela curva piecewise-linear [(ms, f0), ...]."""
    if ms <= points[0][0]:
        return float(points[0][1])
    for i in range(1, len(points)):
        if ms <= points[i][0]:
            t0, f0 = points[i - 1]
            t1, f1 = points[i]
            frac = (ms - t0) / max(1e-9, t1 - t0)
            return f0 + (f1 - f0) * frac
    return float(points[-1][1])


def env_asr(n, attack_ms, release_ms):
    """Envelope ataque-sustain-release por MILISSEGUNDOS (0..1)."""
    out = []
    a = max(1, int(attack_ms * SR / 1000))
    r = max(1, int(release_ms * SR / 1000))
    for i in range(n):
        v = 1.0
        if i < a:
            v = i / a
        elif i > n - r:
            v = (n - i) / r
        out.append(v)
    return out


def synth(dur_ms, pitch, harmonics, formant_hz=0.0, formant_gain=0.0,
          noise_ms=0, noise_amp=0.0, am_hz=0.0, am_depth=0.0,
          vib_hz=0.0, vib_depth=0.0, jitter_cents=8.0, attack_ms=5, release_ms=80):
    """Voz do latido: pilha de harmonicos com glide de f0, formante por
    harmonico, vibrato, jitter por segmento de 10 ms, AM de rugosidade e
    rajada de ruido no ataque (diferencada = so agudos)."""
    n = int(dur_ms * SR / 1000)
    env = env_asr(n, attack_ms, release_ms)
    noise = [random.uniform(-1, 1) for _ in range(n)]
    n_att = max(1, int(noise_ms * SR / 1000))
    out = []
    seg_f = 0.0  # jitter persistente (random walk por segmento)
    last_seg = -1
    norm = max(1e-9, sum(abs(w) for w in harmonics))
    for i in range(n):
        ms = i * 1000.0 / SR
        seg = int(ms // 10)
        if seg != last_seg:
            seg_f = random.uniform(-jitter_cents, jitter_cents)
            last_seg = seg
        f0 = glide(pitch, ms) * (2.0 ** (seg_f / 1200.0))
        if vib_hz:
            f0 *= 2.0 ** (vib_depth * math.sin(2 * math.pi * vib_hz * ms / 1000.0) / 1200.0)
        v = 0.0
        for k, w in enumerate(harmonics, start=1):
            fk = f0 * k
            if fk > SR / 2 - 200:
                break
            g = w
            if formant_hz:
                g *= 1.0 + formant_gain * math.exp(-((fk - formant_hz) / (formant_hz * 0.35)) ** 2)
            v += g * math.sin(2 * math.pi * fk * i / SR + 0.31 * k)
        amp = env[i]
        if am_hz:
            amp *= 1.0 - am_depth * (0.5 + 0.5 * math.sin(2 * math.pi * am_hz * ms / 1000.0))
        v *= amp / norm
        if i < n_att:
            d = noise[i] - noise[i - 1] if i else 0.0
            v += noise_amp * (1 - i / n_att) * d * 4.0
        out.append(v)
    return out


def highpass(x, hz):
    """One-pole: remove o grave que o alto-falante do cao nao reproduz."""
    rc = 1.0 / (2 * math.pi * hz)
    alpha = rc / (rc + 1.0 / SR)
    y, prev_x, prev_y = [], 0.0, 0.0
    for v in x:
        cur = alpha * (prev_y + v - prev_x)
        y.append(cur)
        prev_x, prev_y = v, cur
    return y


def master(x, drive=2.0):
    """HP 250 Hz > saturacao tanh (RMS alto sem clipar) > pico 0.985."""
    x = highpass(x, 250.0)
    k = drive * 2.2
    tk = math.tanh(k)
    x = [math.tanh(k * v) / tk for v in x]
    peak = max(abs(v) for v in x) or 1.0
    g = 0.985 / peak
    fade = max(1, int(0.003 * SR))
    for i in range(len(x)):
        v = x[i] * g
        if i < fade:
            v *= i / fade
        if i > len(x) - fade:
            v *= (len(x) - i) / fade
        x[i] = v
    return x


def stats(x):
    peak = max(abs(v) for v in x)
    rms = math.sqrt(sum(v * v for v in x) / len(x))
    return (20 * math.log10(peak or 1e-9), 20 * math.log10(rms or 1e-9))


def write(name, x):
    path = os.path.join(OUT_DIR, "bark_%s.wav" % name)
    with wave.open(path, "w") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SR)
        f.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v)) * 32767)) for v in x))
    p, r = stats(x)
    print("bark_%-6s.wav  %5.0f ms  pico %6.2f dBFS  RMS %6.2f dBFS  (%d B)"
          % (name, 1000 * len(x) / SR, p, r, os.path.getsize(path)))


# ------------------------------------------------------------------ vozes ---
# woof: queda de pitch grande (520->340), corpo medio, formante 1.2 kHz
write("woof", master(synth(
    240, [(0, 520), (60, 430), (240, 340)],
    [1.0, 0.55, 0.38, 0.26, 0.18, 0.12, 0.08],
    formant_hz=1200, formant_gain=1.8,
    noise_ms=18, noise_amp=0.5, jitter_cents=10, attack_ms=4, release_ms=120)))

# yip: sobe e cai rapido (900->1350->1100), agudo e curto
write("yip", master(synth(
    130, [(0, 900), (50, 1350), (130, 1100)],
    [1.0, 0.7, 0.5, 0.35, 0.25, 0.16, 0.1, 0.07],
    formant_hz=1800, formant_gain=1.6,
    noise_ms=12, noise_amp=0.45, jitter_cents=14, attack_ms=3, release_ms=60)))

# growl: grave roco sustentado com AM de rugosidade; harmonicos altos fortes
write("growl", master(synth(
    650, [(0, 210), (300, 195), (650, 170)],
    [1.0, 0.75, 0.62, 0.5, 0.42, 0.33, 0.25, 0.18],
    formant_hz=950, formant_gain=1.2,
    am_hz=27, am_depth=0.4, jitter_cents=18,
    attack_ms=60, release_ms=160)))

# whine: gemido em glissando (1150->1550->950) com vibrato, sem ruido
write("whine", master(synth(
    800, [(0, 1150), (350, 1550), (800, 950)],
    [1.0, 0.3],
    vib_hz=5.5, vib_depth=25, jitter_cents=6,
    attack_ms=120, release_ms=260)))

# howl: uivo longo em swell (470->640->560) com vibrato lento
write("howl", master(synth(
    1500, [(0, 470), (500, 640), (1200, 590), (1500, 545)],
    [1.0, 0.45, 0.3, 0.2, 0.12],
    formant_hz=1100, formant_gain=0.8,
    vib_hz=5.0, vib_depth=35, jitter_cents=5,
    attack_ms=250, release_ms=450)))
