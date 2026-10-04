#!/usr/bin/env python3
"""Gera NEGATIVOS sinteticos adversariais para o treino do "hi celer".

O modelo v1 aprendeu um atalho: todos os positivos eram voz piper e nenhum
negativo era — "soa como TTS" bastava para disparar (frases sinteticas
quaisquer passavam de 250/255 e a voz real do dono nao passava de 59).
Aqui as MESMAS vozes dos positivos falam coisas que NAO sao a palavra:
confusoes foneticas ("hi", "celery", "cellar", "oi", "celular"...) e frases
soltas EN/PT. Assim a unica pista que separa as classes passa a ser a
palavra em si.

Uso (venv do treino, ver tools/wake/README.md):
  python3 tools/wake/generate_negatives.py /media/.../wake/mwwrun
Gera <workdir>/adversarial/{train,validation}/*.wav
"""
import argparse
import json
import random
import wave
from pathlib import Path

from piper import PiperVoice, SynthesisConfig

# confusoes foneticas: pedacos e vizinhos de "hi celer" (sem nada que SOE
# como a palavra inteira — "hi seller"/"hi cellar" ficam de fora de proposito)
CONFUSAO_EN = [
    "hi.", "hey.", "hello.", "high.", "hi there.", "hi sir.", "hey siri.",
    "celery.", "cellar.", "seller.", "sailor.", "salary.", "clear.", "color.",
    "hi kelly.", "hi sally.", "hey sarah.", "hi daniel.", "cereal.", "solar.",
    "hi google.", "alexa.", "okay.", "hi celia.", "high seller rates.",
]
FRASES_EN = [
    "what time is it?", "turn on the light.", "sit down.", "good morning.",
    "let's go for a walk.", "stop right there.", "how are you doing today?",
    "the weather is nice.", "play some music.", "where is my phone?",
    "come here, buddy.", "that is a good dog.", "I need a coffee.",
    "open the door please.", "see you later.", "thank you very much.",
]
CONFUSAO_PT = [
    "oi.", "olá.", "ei.", "hein?", "ai.", "celular.", "célula.", "selo.",
    "celeiro.", "cecília.", "seleção.", "sereia.", "cela.", "oi, célia.",
    "ei, celso.", "oi, selma.", "hi.", "salada.", "solar.", "sempre.",
]
FRASES_PT = [
    "senta.", "anda.", "para.", "deita.", "bom dia.", "boa noite.",
    "que horas são?", "liga a luz.", "vem cá.", "muito bem.",
    "cadê o controle?", "vamos passear.", "obrigado.", "tudo bem com você?",
    "tá calor hoje.", "desliga a televisão.",
]


def synth(voice, speaker, texts, n, out_dir, seed, prefix):
    rng = random.Random(seed)
    out_dir.mkdir(parents=True, exist_ok=True)
    k = tentativas = 0
    while k < n and tentativas < n * 10 + 20:
        tentativas += 1
        cfg = SynthesisConfig(
            speaker_id=speaker,
            length_scale=rng.uniform(0.85, 1.30),
            noise_scale=rng.uniform(0.30, 0.95),
            noise_w_scale=rng.uniform(0.10, 0.90),
        )
        try:
            chunks = list(voice.synthesize(rng.choice(texts), cfg))
        except Exception:
            continue
        if not chunks:
            continue
        audio = b"".join(c.audio_int16_bytes for c in chunks)
        with wave.open(str(out_dir / f"{prefix}_{seed}_{k:05d}.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(chunks[0].sample_rate)
            w.writeframes(audio)
        k += 1


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir")
    ap.add_argument("--train", type=int, default=3000, help="amostras de treino (todas as vozes)")
    ap.add_argument("--val", type=int, default=300)
    args = ap.parse_args()
    wd = Path(args.workdir)
    vd = wd / "voices"
    out = wd / "adversarial"

    en = PiperVoice.load(vd / "en_US-libritts_r-medium.onnx")
    pts = [PiperVoice.load(p) for p in sorted(vd.glob("pt_BR-*.onnx"))]
    outros_en = [PiperVoice.load(p) for p in sorted(vd.glob("en_*.onnx"))
                 if "libritts" not in p.name]

    textos_en = CONFUSAO_EN * 2 + FRASES_EN  # confusoes pesam o dobro
    textos_pt = CONFUSAO_PT * 2 + FRASES_PT

    # mesma divisao de falantes dos positivos: < 850 no treino
    rng = random.Random(7)
    for split, total, spk_range, seed0 in (("train", args.train, range(0, 800), 100),
                                            ("validation", args.val, range(800, 850), 900)):
        n_en = int(total * 0.55)
        n_pt = int(total * 0.35)
        n_out = total - n_en - n_pt
        spks = list(spk_range)
        per = max(1, n_en // 200)
        feitos = 0
        while feitos < n_en:
            s = rng.choice(spks)
            synth(en, s, textos_en, per, out / split, seed0 + feitos, "en")
            feitos += per
        for i, v in enumerate(pts):
            synth(v, None, textos_pt, max(1, n_pt // len(pts)), out / split, seed0 + 50000 + i, f"pt{i}")
        for i, v in enumerate(outros_en):
            synth(v, None, textos_en, max(1, n_out // max(1, len(outros_en))), out / split,
                  seed0 + 60000 + i, f"en{i}")

    print(json.dumps({d.name: len(list(d.glob("*.wav"))) for d in sorted(out.iterdir())}, indent=1))


if __name__ == "__main__":
    main()
