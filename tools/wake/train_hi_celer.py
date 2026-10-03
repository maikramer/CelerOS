#!/usr/bin/env python3
"""Pipeline de treino do wake word "hi celer" (microWakeWord).

Rodar DENTRO da venv do treino (ver tools/wake/README.md):
  python3 tools/wake/train_hi_celer.py /tmp/mwwrun [--steps 12000]

Etapas (idempotentes, cada uma pula se o produto ja existe):
  1. descompacta os datasets negativos (kahrendt/microwakeword do HF);
  2. gera as features (ragged mmap) dos positivos de
     positives/{train,validation,testing_unseen} — a saida "testing" do
     treinador aponta para testing_unseen (vozes NUNCA vistas no treino:
     falantes >= 850 da libritts_r + leituras pt_BR diferentes);
  3. escreve training_parameters.yaml (pesos do notebook basico);
  4. treina mixednet e quantiza (python -m microwakeword.model_train_eval);
  5. copia o tflite final para <workdir>/hi_celer.tflite e imprime as
     metricas do teste quantizado em streaming — o GATE: recall das vozes
     nao vistas >= 0.95 e falsos positivos/hora em ambiente baixo.
"""
import argparse
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import yaml

NEGATIVES = ["dinner_party", "dinner_party_eval", "no_speech", "speech"]


def ensure_negatives(wd: Path) -> None:
    nd = wd / "negative_datasets"
    nd.mkdir(exist_ok=True)
    for name in NEGATIVES:
        zdir = nd / name
        if (zdir / "training").exists():
            continue
        zpath = nd / f"{name}.zip"
        if not zpath.exists():
            sys.exit(f"falta {zpath} (baixe de huggingface.co/datasets/kahrendt/microwakeword)")
        print(f"[treino] descompactando {name}...", flush=True)
        with zipfile.ZipFile(zpath) as z:
            z.extractall(nd)


def ensure_positive_features(wd: Path) -> None:
    from mmap_ninja.ragged import RaggedMmap
    from microwakeword.audio.clips import Clips
    from microwakeword.audio.spectrograms import SpectrogramGeneration

    out = wd / "generated_features"
    plan = [
        ("train", "training", 10),
        ("validation", "validation", 10),
        ("testing_unseen", "testing", 1),
    ]
    for src_split, dst_split, slide in plan:
        dst = out / dst_split / "wakeword_mmap"
        if dst.exists():
            continue
        print(f"[treino] features de positives/{src_split} -> {dst}", flush=True)
        clips = Clips(
            input_directory=str(wd / "positives" / src_split),
            file_pattern="*.wav",
        )
        gen = SpectrogramGeneration(clips=clips, step_ms=10, slide_frames=slide)
        dst.parent.mkdir(parents=True, exist_ok=True)
        RaggedMmap.from_generator(
            out_dir=str(dst),
            sample_generator=gen.spectrogram_generator(split=None, repeat=2 if slide > 1 else 1),
            batch_size=100,
            verbose=True,
        )


def write_yaml(wd: Path, steps: int) -> Path:
    cfg = {
        "window_step_ms": 10,
        "train_dir": str(wd / "trained_models" / "wakeword"),
        "features": [
            {"features_dir": str(wd / "generated_features"),
             "sampling_weight": 2.0, "penalty_weight": 1.0, "truth": True,
             "truncation_strategy": "truncate_start", "type": "mmap"},
            {"features_dir": str(wd / "negative_datasets" / "speech"),
             "sampling_weight": 10.0, "penalty_weight": 1.0, "truth": False,
             "truncation_strategy": "random", "type": "mmap"},
            {"features_dir": str(wd / "negative_datasets" / "dinner_party"),
             "sampling_weight": 10.0, "penalty_weight": 1.0, "truth": False,
             "truncation_strategy": "random", "type": "mmap"},
            {"features_dir": str(wd / "negative_datasets" / "no_speech"),
             "sampling_weight": 5.0, "penalty_weight": 1.0, "truth": False,
             "truncation_strategy": "random", "type": "mmap"},
            {"features_dir": str(wd / "negative_datasets" / "dinner_party_eval"),
             "sampling_weight": 0.0, "penalty_weight": 1.0, "truth": False,
             "truncation_strategy": "split", "type": "mmap"},
        ],
        "training_steps": [steps],
        "positive_class_weight": [1],
        "negative_class_weight": [20],
        "learning_rates": [0.001],
        "batch_size": 128,
        "time_mask_max_size": [0],
        "time_mask_count": [0],
        "freq_mask_max_size": [0],
        "freq_mask_count": [0],
        "eval_step_interval": 500,
        "clip_duration_ms": 1500,
        "target_minimization": 0.9,
        "minimization_metric": None,
        "maximization_metric": "average_viable_recall",
    }
    p = wd / "training_parameters.yaml"
    p.write_text(yaml.dump(cfg))
    return p


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir", type=Path)
    ap.add_argument("--steps", type=int, default=12000)
    args = ap.parse_args()
    wd = args.workdir

    ensure_negatives(wd)
    ensure_positive_features(wd)
    yml = write_yaml(wd, args.steps)

    model_dir = wd / "trained_models" / "wakeword" / "tflite_stream_state_internal_quant"
    tflite = model_dir / "stream_state_internal_quant.tflite"
    if not tflite.exists():
        print("[treino] model_train_eval (treina + quantiza + testa)...", flush=True)
        rc = subprocess.call(
            [sys.executable, "-m", "microwakeword.model_train_eval",
             f"--training_config={yml}", "--train", "1",
             "--restore_checkpoint", "1",
             "--test_tf_nonstreaming", "0",
             "--test_tflite_nonstreaming", "0",
             "--test_tflite_nonstreaming_quantized", "0",
             "--test_tflite_streaming", "0",
             "--test_tflite_streaming_quantized", "1",
             "--use_weights", "best_weights",
             "mixednet",
             "--pointwise_filters", "64,64,64,64",
             "--repeat_in_block", "1, 1, 1, 1",
             "--mixconv_kernel_sizes", "[5], [7,11], [9,15], [23]",
             "--residual_connection", "0,0,0,0",
             "--first_conv_filters", "32",
             "--first_conv_kernel_size", "5",
             "--stride", "3"],
            cwd=wd)
        if rc != 0:
            sys.exit(f"model_train_eval falhou (rc={rc})")

    if not tflite.exists():
        sys.exit(f"tflite nao apareceu em {tflite}")
    out = wd / "hi_celer.tflite"
    shutil.copy(tflite, out)
    print(f"[treino] modelo: {out} ({out.stat().st_size} bytes)")
    print("[treino] metricas: ver o log acima (quantized streaming) — gate:")
    print("          recall(testing=vozes novas) >= 0.95 e ambient FA/h baixo")


if __name__ == "__main__":
    main()
