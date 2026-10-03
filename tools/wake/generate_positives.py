#!/usr/bin/env python3
"""Gera amostras sinteticas de "hi celer" para treinar o microWakeWord.

Usa a API python do piper-tts (mesma voz multi-falante do gerador oficial:
en_US-libritts_r, 904 falantes) com variacao de speaker/velocidade/ruido por
amostra, mais a voz pt_BR/faber para o sotaque do dono ("hi celer" dito por
um brasileiro). Falantes >= 850 ficam DE FORA do treino: o gate de qualidade
so aceita o modelo se ele acertar vozes nunca vistas.

Uso (venv do treino, ver tools/wake/README.md):
  python3 tools/wake/generate_positives.py /tmp/mwwrun
Gera /tmp/mwwrun/positives/{train,validation,testing}/... e
positives_testing_unseen/ (o gate).
"""
import argparse
import json
import random
import wave
from pathlib import Path

from piper import PiperVoice, SynthesisConfig

PHRASES = ["hi celer.", "hi celer!", "hi, celer.", "hi celer?"]
# frases-pt: o texto em ingles atravessa a voz pt_BR => fonemas com sotaque
PHRASES_PT = ["hi celer.", "hi, celer.", "ei celer.", "hai celer."]

# falantes da libritts_r: 0..903; >=850 sao reservadas para o gate
UNSEEN_FROM = 850


def synth_batch(voice, speaker, n, phrases, out_dir, seed, prefix="celer"):
    rng = random.Random(seed)
    out_dir.mkdir(parents=True, exist_ok=True)
    k = 0
    tentativas = 0
    while k < n and tentativas < n * 10 + 20:  # speaker ruim nao trava o laco
        tentativas += 1
        text = rng.choice(phrases)
        cfg = SynthesisConfig(
            speaker_id=speaker,
            length_scale=rng.uniform(0.85, 1.30),
            noise_scale=rng.uniform(0.30, 0.95),
            noise_w_scale=rng.uniform(0.10, 0.90),
        )
        try:
            chunks = list(voice.synthesize(text, cfg))
        except Exception:
            continue
        if not chunks:
            continue
        audio = b"".join(c.audio_int16_bytes for c in chunks)
        sr = chunks[0].sample_rate
        path = out_dir / f"{prefix}_{seed}_{k:05d}.wav"
        with wave.open(str(path), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(sr)
            w.writeframes(audio)
        k += 1


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir", help="/tmp/mwwrun (vozes em voices/)")
    ap.add_argument("--per-train", type=int, default=2200)
    ap.add_argument("--per-val", type=int, default=250)
    ap.add_argument("--per-test", type=int, default=250)
    ap.add_argument("--unseen", type=int, default=200)
    ap.add_argument("--pt", type=int, default=400, help="amostras pt_BR no treino")
    args = ap.parse_args()

    wd = Path(args.workdir)
    en = PiperVoice.load(wd / "voices/en_US-libritts_r-medium.onnx")
    pt = PiperVoice.load(wd / "voices/pt_BR-faber-medium.onnx")
    out = wd / "positives"

    rng = random.Random(42)
    spk = list(range(0, UNSEEN_FROM))
    rng.shuffle(spk)

    def take(n):
        return [spk.pop() for _ in range(min(n, len(spk)))]

    # treino: espalha os ~2200 en + 400 pt entre falantes variados
    train_spk = take(len(spk))
    per_speaker = max(1, (args.per_train - args.pt) // max(1, len(train_spk)))
    synth = 0
    for i, s in enumerate(train_spk):
        n = per_speaker if i < len(train_spk) - 1 else args.per_train - args.pt - synth
        if n <= 0:
            break
        synth_batch(en, s, n, PHRASES, out / "train", seed=1000 + i)
        synth += n
    synth_batch(pt, None, args.pt, PHRASES_PT, out / "train", seed=77, prefix="pt")

    # validacao/teste: falantes fora do treino mas < 850 (vistos na
    # distribuicao, nao nos pesos)
    synth_batch(en, UNSEEN_FROM - 50, args.per_val, PHRASES, out / "validation", seed=8001)
    synth_batch(en, UNSEEN_FROM - 50, args.per_test, PHRASES, out / "testing", seed=9001)

    # gate: falantes NUNCA vistos (>=850) + pt com outra "leitura"
    for j, s in enumerate(range(UNSEEN_FROM, UNSEEN_FROM + 60)):
        synth_batch(en, s, max(1, args.unseen // 60), PHRASES,
                    out / "testing_unseen", seed=20000 + j)
    synth_batch(pt, None, args.unseen // 4, ["hi celer.", "ei celer."],
                out / "testing_unseen", seed=21000, prefix="pt")

    counts = {d.name: len(list(d.glob("*.wav"))) for d in sorted(out.iterdir())}
    print(json.dumps(counts, indent=1))


if __name__ == "__main__":
    main()
