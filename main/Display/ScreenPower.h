#ifndef CELER_DISPLAY_SCREENPOWER_H
#define CELER_DISPLAY_SCREENPOWER_H

// Estados de tela do watch (F3): pleno -> dim (8 s) -> AOD (15 s, so com o
// app Watchface aberto) -> apagado de verdade (timeout do Backlight em
// minutos + painel em SLPIN). Raise do IMU acende um "glance" (AOD por
// glance_sec e volta a dormir). Porte da maquina de estados do firmware
// Rust (main.rs), adaptado ao CelerOS:
//
//   - o Backlight continua dono do OFF logico (timeout + wake-consumido no
//     kui::readTouch — API 12); ScreenPower adiciona os estagios DIM e AOD
//     por cima (Backlight::dim/undim) e o sono REAL do painel (hooks
//     screenSleep/screenWake do BoardProfile — SLPIN/SLPOUT do AMOLED);
//   - atividade = hub do Backlight (toques reais/injetados + botoes);
//   - em AOD/off o quadro dos apps nao vai ao vidro (present() consulta
//     suppressAppFrame): o AOD e desenhado pelo proprio ScreenPower, 1x por
//     minuto, com deslocamento anti burn-in.
//
// Ativo apenas em placas com hook screenSleep no perfil (watch). Nos demais
// tudo abaixo e no-op.

#include <stdint.h>

namespace ScreenPower {

void init();               // celerSetup (le config; liga o timeout do Backlight)
void tick(bool inApp);     // celerLoop -> false; present() dos apps -> true
bool suppressAppFrame();   // present() consulta: true em AOD/off
void keepAwake(bool on);   // apps de jogo seguram a tela acesa (F4: System)

// Deep sleep de verdade (reboot ao acordar): painel em SLPIN, ritual da
// placa (sleepPrep), radio fora, wake por EXT1 nos botoes do perfil.
// Botao PWR segurado ~2 s chama; o System.deepSleep dos apps continua
// sendo o caminho com timer.
void deepSleepNow();

}  // namespace ScreenPower

#endif  // CELER_DISPLAY_SCREENPOWER_H
