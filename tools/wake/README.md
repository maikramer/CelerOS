# Wake word "Hi Celer" — treino e modelo

Modelo proprio microWakeWord (Apache-2.0, treinado com
[OHF-Voice/micro-wake-word](https://github.com/OHF-Voice/micro-wake-word),
antigo kahrendt/microWakeWord) que detecta **"hi celer"** no ESP32-S3 do cao
— a palavra de ativacao dos comandos de voz do Dog Face. O `.tflite` int8
(estado interno, streaming) e embutido no firmware como header C
(`tflite_to_header.py` -> `main/Assets/Wake/HiCelerModel.h`) e roda no
TFLite Micro com o frontend de features C++ (`main/Hardware/Wake/frontend/`).

## Licao do v1 (nao repetir)

O v1 passou no proprio gate (falsa rejeicao 0.0) e nao funcionava com gente:
positivos 100% voz piper, nenhum negativo sintetico, **zero augmentacao** e
um gate feito so de vozes piper. O modelo aprendeu "soa como TTS": 27% de
falsos em frases sinteticas quaisquer, 0/25 da voz real do dono. O v2 ataca
as quatro causas — e o gate passou a ser a voz real pelo microfone do cao.

## Receita v2 (na bancada)

Workdir em DISCO (o v1 vivia em /tmp, que e tmpfs: some no reboot).

```bash
W=/caminho/em/disco/wake           # ~35 GB
# 1. ambiente (python 3.12; tensorflow CPU basta — 16 nucleos)
uv venv --python 3.12 $W/env
git clone --depth 1 https://github.com/OHF-Voice/micro-wake-word $W/mww
uv pip install --python $W/env/bin/python \
    'git+https://github.com/whatsnowplaying/audio-metadata@d4ebb23' \
    tensorflow==2.19.0 'pymicro-features>=0.0.7' -e $W/mww \
    scipy pyyaml soundfile librosa matplotlib pandas mmap_ninja piper-tts datasets

# 2. dados (em $W/mwwrun)
#    negative_datasets/{dinner_party,dinner_party_eval,no_speech,speech}.zip
#      de huggingface.co/datasets/kahrendt/microwakeword (~5.4 GB zipados)
#    voices/: piper en_US-libritts_r-medium (904 falantes) + pt_BR faber,
#      cadu, jeff, edresson + en_US-lessac, en_GB-alan (.onnx + .onnx.json)
#    rir/: davidscripka/MIT_environmental_impulse_responses (HF, 270 salas)
#    background/: ESC-50 (HF ashraq/esc50) convertido a 16 kHz (2000 sons)

# 3. gravacoes REAIS pelo microfone do cao (o dado que mais importa):
#    instale tools/wake/gravador no cao, /local/rec/plano.txt = "pos 25"
#    ("hi celer" a cada bipe) e depois "neg 25" (outras falas, palavras
#    parecidas, silencio/ruido); celerctl pull de /local/rec -> $W/real/
#    (pos_NN.wav, neg_NN.wav). Os 15 primeiros de cada treinam; o resto e o gate.

# 4. sinteticos: positivos e negativos ADVERSARIAIS com as mesmas vozes
$W/env/bin/python tools/wake/generate_positives.py $W/mwwrun
$W/env/bin/python tools/wake/generate_negatives.py $W/mwwrun

# 5. features com augmentacao + treino + quantizacao + gate
$W/env/bin/python tools/wake/train_hi_celer.py $W/mwwrun --real $W/real --steps 20000

# 6. embutir no firmware e rebuildar o cao (ajuste kProbCutoff pelo gate)
python3 tools/wake/tflite_to_header.py $W/mwwrun/hi_celer_v2.tflite
```

## Gate de qualidade

`eval_device.py` reproduz a cadeia do firmware (microfrontend -> int8 ->
invokes de 3 slices -> janela de 5 invokes >= cutoff) e o treino o roda no
fim sobre as gravacoes reais separadas + 100 adversariais de validacao, com
varredura de cutoff. Aceitar o modelo exige recall alto nas gravacoes reais
com zero falsos nos negativos reais e poucos nos adversariais — e a palavra
final e a bancada: dizer "hi celer" para o cao (o Dog Face mostra o
microfone na tela quando detecta).

```bash
$W/env/bin/python tools/wake/eval_device.py MODELO.tflite --pos $W/mwwrun/real_clean/pos_test \
    --neg $W/mwwrun/real_clean/neg_test -v
```

## Artefatos versionados

- `generate_positives.py` / `generate_negatives.py` / `train_hi_celer.py` /
  `eval_device.py` / `tflite_to_header.py` — receita inteira
- `gravador/` — app do cao que coleta as amostras reais
- `hi_celer.tflite` + `manifest.json` — modelo em uso (nome/autor/cutoff)
- `main/Assets/Wake/HiCelerModel.h` — gerado (nao editar)
