# Frontend microfrontend (audio -> 40 features)

Copia fiel (nao editar) do microfrontend do TensorFlow — mesma linhagem que o
`micro_features/micro_speech` do TFLM e o `pymicro-features` usado para
TREINAR os modelos microWakeWord (as features precisam ser identicas no
treino e na inference). Fonte: rhasspy/pymicro-features
(tensorflow/lite/experimental/microfrontend/lib), Apache-2.0, copyright
The TensorFlow Authors.

So os 22 arquivos da pipeline runtime entram (fft/kissfft, window,
filterbank, noise_reduction, pcan_gain_control, log_scale, frontend);
os `*_util/*_io/*_main` do upstream (config via arquivo) ficam de fora —
a configuracao e em codigo no WakeWord.cpp (constantes de
`preprocessor_settings` do ESPHome, que batem com o treino do
microWakeWord: 40 features, janela 30 ms, passo 10 ms, PCAN on).
