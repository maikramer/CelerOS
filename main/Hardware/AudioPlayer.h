#pragma once

// Player de arquivos WAV (System.playWav, API 13).
//
// PCM 16-bit mono/stereo de 8 a 48 kHz, lido do FS em streaming (chunks de
// 1 KB — o arquivo NAO carrega inteiro na RAM). Reusa o mesmo ciclo de
// canal do BoardIO::toneI2s: I2S_NUM_0 (o I2S1 e do microfone), codec
// ES8311/PA do perfil acordados em volta da reproducao, volume pela
// escala digital (System.setVolume). Bloqueante: o watchdog e alimentado
// por chunk. Placa sem I2S de audio (CYD): NoAudio de imediato.
namespace AudioPlayer {

enum class WavError {
    None = 0,
    NoAudio,      // placa sem alto-falante I2S
    OpenFailed,   // arquivo nao abriu
    BadHeader,    // nao e WAV ou fmt inesperado (codec, bits, canais, rate)
};

// Toca o arquivo inteiro e devolve. Caminho ja validado pelo chamador.
WavError playWav(const char* path);

}  // namespace AudioPlayer
