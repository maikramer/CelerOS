#!/usr/bin/env python3
"""Avalia um modelo do wake word como o FIRMWARE o roda (gate do treino).

Mesma cadeia do main/Hardware/WakeWord.cpp: audio 16 kHz -> microfrontend
(pymicro_features, a mesma pipeline do treino e do C++ vendorizado) ->
quantizacao int8 da entrada do modelo -> invokes de `stride` slices ->
probabilidade uint8 -> deteccao quando kSlidingWindow invokes seguidos
ficam >= cutoff.

Uso:
  python3 tools/wake/eval_device.py MODELO.tflite --pos DIR [--pos DIR] --neg DIR [--neg DIR]
Imprime recall nos positivos, falsos nos negativos e a varredura de cutoff
(o firmware usa kProbCutoff=190 e kSlidingWindow=5).
"""
import argparse
import glob
import os

import numpy as np
import soundfile as sf

WINDOW = 5


def load_16k(path):
    import librosa
    a, sr = sf.read(path, dtype="float32")
    if a.ndim > 1:
        a = a[:, 0]
    if sr != 16000:
        a = librosa.resample(a, orig_sr=sr, target_sr=16000)
    return a


def clip_score(interp_factory, audio):
    """Maior 'minimo da janela de 5 invokes' do clipe (0..255): o clipe
    dispara no firmware se isto for >= cutoff."""
    import pymicro_features
    a = np.concatenate([np.zeros(16000, np.float32), audio, np.zeros(16000, np.float32)])
    pcm = (np.clip(a, -1, 1) * 32767).astype(np.int16).tobytes()
    fe = pymicro_features.MicroFrontend()
    feats, i = [], 0
    while i + 320 <= len(pcm):
        o = fe.process_samples(pcm[i:i + 320])
        i += o.samples_read * 2
        if o.features:
            feats.append(o.features)
    it = interp_factory()
    ind, outd = it.get_input_details()[0], it.get_output_details()[0]
    sc, zp = ind["quantization"]
    stride = ind["shape"][1]
    probs = []
    for k in range(0, len(feats) - stride + 1, stride):
        x = np.array(feats[k:k + stride], np.float32) / sc + zp
        it.set_tensor(ind["index"], np.clip(np.round(x), -128, 127).astype(np.int8)[None])
        it.invoke()
        probs.append(int(it.get_tensor(outd["index"]).reshape(-1)[0]))
    if len(probs) < WINDOW:
        return 0
    return max(min(probs[j:j + WINDOW]) for j in range(len(probs) - WINDOW + 1))


def score_dirs(model, dirs):
    import tensorflow as tf

    def factory():
        it = tf.lite.Interpreter(model_path=model)
        it.allocate_tensors()
        return it

    out = []
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d, "*.wav"))):
            out.append((f, clip_score(factory, load_16k(f))))
    return out


def report(pos, neg, cutoffs=(150, 170, 190, 210, 230)):
    lines = []
    for c in cutoffs:
        rec = sum(s >= c for _, s in pos) / max(1, len(pos))
        fa = sum(s >= c for _, s in neg)
        lines.append(f"  cutoff {c:3d}: recall {rec:6.1%} ({sum(s >= c for _, s in pos)}/{len(pos)})"
                     f"   falsos {fa}/{len(neg)}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--pos", action="append", default=[])
    ap.add_argument("--neg", action="append", default=[])
    ap.add_argument("-v", action="store_true", help="lista o score de cada clipe")
    a = ap.parse_args()
    pos = score_dirs(a.model, a.pos)
    neg = score_dirs(a.model, a.neg)
    if a.v:
        for f, s in pos + neg:
            print(f"{s:4d}  {f}")
    print(f"modelo {a.model}: {len(pos)} positivos, {len(neg)} negativos")
    print(report(pos, neg))


if __name__ == "__main__":
    main()
