# Wake word "Hi Celer" — treino e modelo

Modelo proprio microWakeWord (Apache-2.0, treinado com
[kahrendt/microWakeWord](https://github.com/kahrendt/microWakeWord)) que
detecta **"hi celer"** no ESP32-S3 do cao — a palavra de ativacao dos
comandos de voz do Dog Face. O `.tflite` int8 (estado interno, streaming) e
embutido no firmware como header C (`tools/wake/tflite_to_header.py` ->
`main/Assets/Wake/HiCelerModel.h`) e roda no TFLite Micro com o frontend de
features C++ (`main/Hardware/Wake/frontend/`).

## Receita (na bancada, ~1 h contando downloads)

```bash
# 1. ambiente (python 3.12; tensorflow CPU basta — 16 nucleos)
uv venv --python 3.12 /tmp/mwwenv
uv pip install --python /tmp/mwwenv/bin/python \
    'git+https://github.com/whatsnowplaying/audio-metadata@d4ebb23' \
    tensorflow==2.19.0 'pymicro-features>=0.0.7' -e /tmp/mww \
    scipy pyyaml soundfile librosa matplotlib pandas mmap_ninja piper-tts
git clone --depth 1 https://github.com/kahrendt/microWakeWord /tmp/mww

# 2. dados (huggingface kahrendt/microwakeword — ~5.4 GB zipados)
mkdir -p /tmp/mwwrun/{negative_datasets,voices}
for f in dinner_party dinner_party_eval no_speech speech; do
  curl -L -o /tmp/mwwrun/negative_datasets/$f.zip \
    https://huggingface.co/datasets/kahrendt/microwakeword/resolve/main/$f.zip
done
# vozes piper (libritts_r tem 904 falantes; faber = sotaque pt_BR)
curl -L -o /tmp/mwwrun/voices/en_US-libritts_r-medium.onnx \
  https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts_r/medium/en_US-libritts_r-medium.onnx
# (+ o .onnx.json ao lado, e pt/pt_BR/faber/medium/pt_BR-faber-medium.{onnx,onnx.json})

# 3. positivos sinteticos (falantes >= 850 ficam DE FORA: sao o gate)
/tmp/mwwenv/bin/python tools/wake/generate_positives.py /tmp/mwwrun

# 4. treino + quantizacao + teste em streaming (gate de vozes novas)
/tmp/mwwenv/bin/python tools/wake/train_hi_celer.py /tmp/mwwrun --steps 12000

# 5. embutir no firmware e rebuildar o cao
python3 tools/wake/tflite_to_header.py /tmp/mwwrun/hi_celer.tflite
```

## Gate de qualidade

O teste do proprio treinador (quantizado, streaming) roda sobre
`positives/testing_unseen` — vozes **nunca vistas no treino** (falantes
850+ da libritts_r e leituras pt_BR diferentes) — mais o ambiente
`dinner_party_eval`. Aceitar o modelo exige recall >= 0.95 nas vozes novas
com falsos positivos/hora baixos; a palavra final e a sonda no proprio cao
(toques de "hi celer" pelo alto-falante dele, `main/Hardware/WakeWord.cpp`
loga `[wakeword]`).

## Artefatos versionados

- `tools/wake/generate_positives.py` / `train_hi_celer.py` /
  `tflite_to_header.py` — receita inteira
- `hi_celer.tflite` + `manifest.json` — modelo treinado (nome/autor/cutoff)
- `main/Assets/Wake/HiCelerModel.h` — gerado (nao editar)
