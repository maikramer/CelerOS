#!/usr/bin/env python3
"""Pipeline de treino do wake word "hi celer" (microWakeWord) — v2.

Rodar DENTRO da venv do treino (ver tools/wake/README.md):
  python3 tools/wake/train_hi_celer.py WORKDIR --real DIR_GRAVACOES [--steps 20000]

O v1 aprendeu "soa como TTS" em vez da palavra: positivos 100% piper, nenhum
negativo sintetico, ZERO augmentacao (o Augmentation do notebook oficial nao
era chamado) e um gate feito so de vozes piper. Resultado no cao: 27% de
falsos em frases sinteticas quaisquer e 0/25 do dono. O v2:

  1. negativos do HF (speech/dinner_party/no_speech) como antes;
  2. gravacoes REAIS do dono pelo microfone do proprio cao (gravador da
     bancada): pos_NN.wav ("hi celer") e neg_NN.wav (outras falas) —
     limpas (sem o transiente de partida do canal + DC) e divididas: os
     primeiros 15 de cada vao para o treino, os outros 10 sao o GATE;
  3. positivos piper (generate_positives.py) + mais vozes pt_BR;
  4. negativos piper ADVERSARIAIS (generate_negatives.py): as mesmas vozes
     dizendo confusoes foneticas e frases soltas — mata o atalho do TTS;
  5. Augmentation do notebook oficial em tudo que e treino (RIR do MIT,
     ruido de fundo ESC-50, EQ, distorcao, pitch, ganho -45..0 dB, jitter);
  6. SpecAugment ligado;
  7. gate = eval_device.py (a cadeia do firmware) sobre as gravacoes reais
     separadas + adversariais de validacao, com varredura de cutoff.

Etapas idempotentes (cada produto existente e reaproveitado).
"""
import argparse
import random
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import numpy as np
import soundfile as sf
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))

NEGATIVES = ["dinner_party", "dinner_party_eval", "no_speech", "speech"]
N_REAL_TRAIN = 15  # por classe; o resto das gravacoes e o gate


def ensure_negatives(wd: Path) -> None:
    nd = wd / "negative_datasets"
    nd.mkdir(exist_ok=True)
    for name in NEGATIVES:
        if (nd / name / "training").exists():
            continue
        zpath = nd / f"{name}.zip"
        if not zpath.exists():
            sys.exit(f"falta {zpath} (baixe de huggingface.co/datasets/kahrendt/microwakeword)")
        print(f"[treino] descompactando {name}...", flush=True)
        with zipfile.ZipFile(zpath) as z:
            z.extractall(nd)


def prepare_real(wd: Path, real: Path) -> Path:
    """Limpa e divide as gravacoes do cao: tira os 500 ms de partida (o canal
    I2S assentando — nao existe no streaming do detector) e o DC/deriva do
    MEMS (passa-altas 60 Hz)."""
    import scipy.signal as ss
    out = wd / "real_clean"
    if out.exists():
        return out
    b, a = ss.butter(2, 60, "highpass", fs=16000)
    for kind in ("pos", "neg"):
        files = sorted(real.glob(f"{kind}_*.wav"))
        if not files:
            sys.exit(f"sem {kind}_*.wav em {real}")
        for i, f in enumerate(files):
            x, sr = sf.read(f, dtype="float32")
            if sr != 16000:
                sys.exit(f"{f}: esperado 16 kHz (gravador do cao), veio {sr}")
            x = ss.lfilter(b, a, x[int(0.5 * sr):] - np.mean(x))
            split = "train" if i < N_REAL_TRAIN else "test"
            d = out / f"{kind}_{split}"
            d.mkdir(parents=True, exist_ok=True)
            sf.write(d / f.name, x.astype(np.float32), sr, subtype="PCM_16")
    print(f"[treino] gravacoes reais em {out}", flush=True)
    return out


def ensure_extra_pt(wd: Path, per_voice: int) -> None:
    """Positivos com as vozes pt_BR alem da faber (sotaque do dono)."""
    from generate_positives import PHRASES_PT, synth_batch
    from piper import PiperVoice
    d = wd / "positives" / "train_pt_extra"
    if d.exists():
        return
    for i, p in enumerate(sorted((wd / "voices").glob("pt_BR-*.onnx"))):
        if "faber" in p.name:
            continue  # ja esta no positives/train
        synth_batch(PiperVoice.load(p), None, per_voice, PHRASES_PT, d, seed=500 + i,
                    prefix=f"pt{i}")
    print(f"[treino] positivos pt_BR extras: {len(list(d.glob('*.wav')))}", flush=True)


def make_augmenter(wd: Path):
    from microwakeword.audio.augmentation import Augmentation
    return Augmentation(
        augmentation_duration_s=3.2,
        augmentation_probabilities={
            "SevenBandParametricEQ": 0.1,
            "TanhDistortion": 0.1,
            "PitchShift": 0.1,
            "BandStopFilter": 0.1,
            "AddColorNoise": 0.1,
            "AddBackgroundNoise": 0.75,
            "Gain": 1.0,
            "RIR": 0.5,
        },
        impulse_paths=[str(wd / "rir" / "16khz")],
        background_paths=[str(wd / "background")],
        background_min_snr_db=-5,
        background_max_snr_db=10,
        min_jitter_s=0.195,
        max_jitter_s=0.205,
    )


def build_mmap(dst: Path, src_dirs, augmenter, slide: int, repeat: int) -> None:
    """Features (ragged mmap) de uma ou mais pastas de wav."""
    from mmap_ninja.ragged import RaggedMmap
    from microwakeword.audio.clips import Clips
    from microwakeword.audio.spectrograms import SpectrogramGeneration
    if dst.exists():
        return
    dst.parent.mkdir(parents=True, exist_ok=True)

    def gen():
        for src in src_dirs:
            clips = Clips(input_directory=str(src), file_pattern="*.wav")
            sg = SpectrogramGeneration(clips=clips, augmenter=augmenter, step_ms=10,
                                       slide_frames=slide)
            yield from sg.spectrogram_generator(split=None, repeat=repeat)

    print(f"[treino] features -> {dst}", flush=True)
    RaggedMmap.from_generator(out_dir=str(dst), sample_generator=gen(), batch_size=100, verbose=False)


def ensure_features(wd: Path, rc: Path, real_repeat: int) -> Path:
    aug = make_augmenter(wd)
    ft = wd / "features_v2"
    pos, adv, rneg = ft / "positive", ft / "adversarial", ft / "real_negative"
    # positivos: TTS (en + pt) e o dono repetido com augmentacao diferente a
    # cada repeticao — 15 gravacoes viram centenas de variantes de sala/ruido
    build_mmap(pos / "training" / "tts_mmap",
               [wd / "positives" / "train", wd / "positives" / "train_pt_extra"], aug, 10, 2)
    build_mmap(pos / "training" / "real_mmap", [rc / "pos_train"], aug, 10, real_repeat)
    build_mmap(pos / "validation" / "tts_mmap", [wd / "positives" / "validation"], aug, 10, 1)
    build_mmap(pos / "testing" / "real_mmap", [rc / "pos_test"], None, 1, 1)  # gate: sem aug
    # adversariais sinteticos
    build_mmap(adv / "training" / "adv_mmap", [wd / "adversarial" / "train"], aug, 10, 1)
    build_mmap(adv / "validation" / "adv_mmap", [wd / "adversarial" / "validation"], aug, 10, 1)
    build_mmap(adv / "testing" / "adv_mmap", [wd / "adversarial" / "validation"], None, 1, 1)
    # negativos reais do dono (outras falas pelo mesmo microfone)
    build_mmap(rneg / "training" / "real_mmap", [rc / "neg_train"], aug, 10, real_repeat)
    build_mmap(rneg / "validation" / "real_mmap", [rc / "neg_train"], None, 10, 1)
    build_mmap(rneg / "testing" / "real_mmap", [rc / "neg_test"], None, 1, 1)
    return ft


def write_yaml(wd: Path, ft: Path, steps: int) -> Path:
    def feat(d, w, truth, trunc="random", penalty=1.0):
        return {"features_dir": str(d), "sampling_weight": w, "penalty_weight": penalty,
                "truth": truth, "truncation_strategy": trunc, "type": "mmap"}
    nd = wd / "negative_datasets"
    cfg = {
        "window_step_ms": 10,
        "train_dir": str(wd / "trained_models" / "wakeword_v2"),
        "features": [
            feat(ft / "positive", 2.0, True, "truncate_start"),
            feat(ft / "adversarial", 3.0, False),
            feat(ft / "real_negative", 1.0, False),
            feat(nd / "speech", 10.0, False),
            feat(nd / "dinner_party", 10.0, False),
            feat(nd / "no_speech", 5.0, False),
            feat(nd / "dinner_party_eval", 0.0, False, "split"),
        ],
        "training_steps": [steps],
        "positive_class_weight": [1],
        "negative_class_weight": [20],
        "learning_rates": [0.001],
        "batch_size": 128,
        # SpecAugment (v1: tudo 0)
        "time_mask_max_size": [5],
        "time_mask_count": [2],
        "freq_mask_max_size": [5],
        "freq_mask_count": [2],
        "eval_step_interval": 500,
        "clip_duration_ms": 1500,
        "target_minimization": 0.9,
        "minimization_metric": None,
        "maximization_metric": "average_viable_recall",
    }
    p = wd / "training_parameters_v2.yaml"
    p.write_text(yaml.dump(cfg))
    return p


def train(wd: Path, yml: Path) -> Path:
    model_dir = wd / "trained_models" / "wakeword_v2" / "tflite_stream_state_internal_quant"
    tflite = model_dir / "stream_state_internal_quant.tflite"
    if tflite.exists():
        return tflite
    print("[treino] model_train_eval (treina + quantiza + testa)...", flush=True)
    rc = subprocess.call(
        [sys.executable, "-m", "microwakeword.model_train_eval",
         f"--training_config={yml}", "--train", "1", "--restore_checkpoint", "1",
         "--test_tf_nonstreaming", "0", "--test_tflite_nonstreaming", "0",
         "--test_tflite_nonstreaming_quantized", "0", "--test_tflite_streaming", "0",
         "--test_tflite_streaming_quantized", "1", "--use_weights", "best_weights",
         "mixednet",
         "--pointwise_filters", "64,64,64,64",
         "--repeat_in_block", "1, 1, 1, 1",
         "--mixconv_kernel_sizes", "[5], [7,11], [9,15], [23]",
         "--residual_connection", "0,0,0,0",
         "--first_conv_filters", "32",
         "--first_conv_kernel_size", "5",
         "--stride", "3"],
        cwd=wd)
    if rc != 0 or not tflite.exists():
        sys.exit(f"model_train_eval falhou (rc={rc})")
    return tflite


def gate(wd: Path, rc: Path, tflite: Path) -> None:
    from eval_device import report, score_dirs
    pos = score_dirs(str(tflite), [rc / "pos_test"])
    neg = score_dirs(str(tflite), [rc / "neg_test"])
    rng = random.Random(3)
    adv_files = sorted((wd / "adversarial" / "validation").glob("*.wav"))
    sub = wd / "gate_adv"
    if not sub.exists():
        sub.mkdir()
        for f in rng.sample(adv_files, min(100, len(adv_files))):
            shutil.copy(f, sub / f.name)
    adv = score_dirs(str(tflite), [sub])
    print("[gate] gravacoes REAIS separadas do treino (cadeia do firmware):")
    print(report(pos, neg))
    print("[gate] adversariais sinteticos (100 de validacao):")
    print(report(pos, adv))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir", type=Path)
    ap.add_argument("--real", type=Path, required=True, help="pasta com pos_NN.wav/neg_NN.wav do cao")
    ap.add_argument("--steps", type=int, default=20000)
    ap.add_argument("--pt-extra", type=int, default=300, help="positivos por voz pt_BR extra")
    ap.add_argument("--real-repeat", type=int, default=40, help="variantes aumentadas por gravacao real")
    args = ap.parse_args()
    wd = args.workdir

    ensure_negatives(wd)
    for need in ("rir", "background", "adversarial"):
        if not (wd / need).exists():
            sys.exit(f"falta {wd / need} (ver tools/wake/README.md)")
    rc = prepare_real(wd, args.real)
    ensure_extra_pt(wd, args.pt_extra)
    ft = ensure_features(wd, rc, args.real_repeat)
    yml = write_yaml(wd, ft, args.steps)
    tflite = train(wd, yml)
    out = wd / "hi_celer_v2.tflite"
    shutil.copy(tflite, out)
    print(f"[treino] modelo: {out} ({out.stat().st_size} bytes)")
    gate(wd, rc, tflite)


if __name__ == "__main__":
    main()
