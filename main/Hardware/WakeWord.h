#pragma once

// Wake word on-device "Hi ESP" (WakeNet wn10_hiesp do esp-sr, API 20).
// Task propria le o canal I2S do microfone (o mesmo do gravador: lock por
// chunk em BoardIO::micChanLock) e roda a inference a cada chunk do modelo.
// Enquanto uma gravacao esta em curso (micRecActive) a task descansa — o
// comando falado apos o wake e capturado pelo Mic.*, o detector so reativa
// quando o canal volta a ficar livre.
//
// So existe no build com CONFIG_CELEROS_WAKE_WORD (hoje o cao).

namespace WakeWord {

// Sobe o modelo e a task (idempotente). Devolve false sem microfone, sem
// RAM p/ o modelo ou se o esp-sr nao achar o wn10_hiesp.
bool start();
// Encerra a task e destroi o modelo (join: <= ~200 ms).
void stop();
bool running();
// true = "Hi ESP" detectado (consome o evento; deteccoes multiplas entre
// polls colapsam numa so — o app polha a cada volta do loop).
bool poll();
// RMS 0..100 do ultimo chunk lido (mesma escala do micLevel); -1 parado.
int level();

}  // namespace WakeWord
