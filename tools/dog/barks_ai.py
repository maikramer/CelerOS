#!/usr/bin/env python3
"""Latidos do Dog Face por IA (text2sound / Stable Audio SFX) + masterizacao.

Os bark_*.wav do app sao renders UNICOS de IA (como o modelo do wake word,
nao sao byte-reproduziveis). Este script documenta e reproduz o PIPELINE:
os prompts abaixo geram os takes (`text2sound generate --profile effects`),
e a masterizacao converte pro cao (16 kHz mono PCM16, janela mais forte de
cada take, high-pass 250 Hz — o falante pequeno nao reproduz o grave —
saturacao tanh e pico em -0.13 dBFS: volume maximo sem clipping).

    text2sound generate --profile effects -d 1.0 -o /tmp/barks/woof.wav \
        "single big dog bark, one deep woof, close microphone, dry recording, no background"
    ... (os 5 prompts em PROMPTS abaixo)
    python3 tools/dog/barks_ai.py            # /tmp/barks/*.wav -> app

Requer numpy + soundfile (o text2sound ja os traz). O barks.py sintetico
permanece como fallback offline/deterministico.
"""
import os

import numpy as np
import soundfile as sf

SR = 16000
SRC = "/tmp/barks"
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "boards", "spotpear-dog", "data", "apps", "Dog Face", "assets")

# (nome, duracao pedida ao gerador, teto final s, respiro nas pontas s, prompt)
PROMPTS = [
    ("woof", 1.0, 0.55, 0.06,
     "single big dog bark, one deep woof, close microphone, dry recording, no background"),
    ("yip", 0.8, 0.40, 0.05,
     "small dog single yip bark, sharp high pitched, close mic, dry"),
    ("growl", 2.0, 1.10, 0.08,
     "dog growling, low rumbling threat growl, close mic"),
    ("whine", 1.5, 1.20, 0.15,
     "dog whining whimper, sad short whine, close mic"),
    ("howl", 3.0, 1.80, 0.20,
     "single dog howl, sustained, mournful, close mic"),
]


def load_mono(path):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    n = int(len(x) * SR / sr)
    idx = np.linspace(0, len(x) - 1, n)
    return x[np.clip(idx.astype(int), 0, len(x) - 1)]


def highpass(x, hz):
    rc = 1.0 / (2 * np.pi * hz)
    a = rc / (rc + 1.0 / SR)
    y = np.empty_like(x)
    px = py = 0.0
    for i, v in enumerate(x):
        py = a * (py + v - px)
        px = v
        y[i] = py
    return y


def render(name, cap_s, pad_s):
    x = load_mono(os.path.join(SRC, "%s.wav" % name))
    env = np.abs(x)
    thr = 0.04 * env.max()
    i = int(np.argmax(env > thr))
    j = len(x) - int(np.argmax(env[::-1] > thr)) - 1
    x = x[max(0, i): j + 1]
    w = int(cap_s * SR)
    if len(x) > w:  # fica so a janela de maior energia (takes de IA Alongam)
        e2 = np.convolve(x ** 2, np.ones(w), "valid")
        s = int(np.argmax(e2))
        x = x[s: s + w]
    p = int(pad_s * SR)
    x = np.concatenate([np.zeros(p), x, np.zeros(p)])
    x = highpass(x, 250.0)                       # grave que o falante nao usa
    x = np.tanh(4.4 * x) / np.tanh(4.4)          # RMS alto sem clipar
    x = 0.985 * x / np.abs(x).max()              # pico -0.13 dBFS
    f = int(0.004 * SR)
    x[:f] *= np.linspace(0, 1, f)
    x[-f:] *= np.linspace(1, 0, f)
    pth = os.path.join(OUT_DIR, "bark_%s.qoa" % name)
    wav = pth + ".tmp.wav"
    sf.write(wav, x.astype(np.float32), SR, subtype="PCM_16")
    # QOA (5x menor, decoder de ~60 linhas no firmware): qoaenc compila da
    # implementacao de referencia (tools/dog/qoaenc.c + main/Hardware/Audio/qoa.h)
    here = os.path.dirname(os.path.abspath(__file__))
    os.system("gcc -O2 -o /tmp/qoaenc %s/qoaenc.c && /tmp/qoaenc %s %s" % (here, wav, pth))
    os.remove(wav)
    rms = 20 * np.log10(np.sqrt((x ** 2).mean()))
    print("bark_%-6s.qoa  %4.0f ms  RMS %5.2f dBFS  (%d B)"
          % (name, 1000 * len(x) / SR, rms, os.path.getsize(pth)))


if __name__ == "__main__":
    for nome, _dur, cap, pad, _prompt in PROMPTS:
        render(nome, cap, pad)
