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
// startMs pula para o meio da musica (handoff da matilha: retoma de onde
// parou no vizinho; alem do total a task encerra na hora, posMs final).
bool play(const MusicEngine::Song& song, uint32_t startMs = 0);

// Efeito curto (System.sfx, API 32) misturado por cima da trilha — nao
// bloqueia; sem trilha a task abre uma sessao so para ele. Substitui o
// efeito em curso. Devolve os tons aceitos (0 = alto-falante ocupado por
// fala/tom/playWav, sem audio na placa ou lista vazia).
int sfx(const MusicEngine::SfxTone* tones, int n);

// Corta a trilha no proximo bloco (~6 ms); um efeito em curso termina.
// Idempotente.
void stop();

// true enquanto a trilha toca (ate acabarem os loops ou chegar um stop()).
bool playing();

// Posicao em ms desde o inicio do audio, ou -1 se parado.
int32_t posMs();

// Copia a musica em curso (handoff da matilha). false = nada tocando.
bool currentSong(MusicEngine::Song* out);

}  // namespace MusicSynth
