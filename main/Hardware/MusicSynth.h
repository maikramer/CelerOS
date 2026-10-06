#pragma once

// Cola ESP do System.playMusic (API 25): task dorminte que mistura as
// trilhas do MusicEngine e escreve no alto-falante I2S, com a MESMA guarda
// de posse exclusiva do playWav/tom/TTS (AudioPlayer::acquireOutput). O
// motor puro (compilacao e renderizacao) vive em MusicEngine.h — este
// header so existe para o runtime JS.
#include "MusicEngine.h"

namespace MusicSynth {

// Compila a musica e acorda a task (nao bloqueia). false = alto-falante
// ocupado (fala/tom/playWav em curso), sem audio na placa ou musica vazia.
bool play(const MusicEngine::Song& song);

// Corta no proximo bloco (~15 ms). Idempotente.
void stop();

// true enquanto a task toca (ate acabarem os loops ou chegar um stop()).
bool playing();

// Posicao em ms desde o inicio do audio, ou -1 se parado.
int32_t posMs();

}  // namespace MusicSynth
