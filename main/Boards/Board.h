#ifndef CELER_BOARDS_BOARD_H
#define CELER_BOARDS_BOARD_H

#include <stdint.h>
#include "../Display/Display.h"
#include "BoardDisplay.h"  // resolve para main/Boards/<placa>/ (include path do CMake)

// ============================================================================
// HAL de placas do CelerOS.
//
// O build seleciona exatamente uma placa (-DCELEROS_BOARD=<smartdisplay|cyd>);
// o CMake compila Boards/<placa>/Board.cpp e coloca o diretorio no include
// path. Nenhum outro arquivo do firmware pode conter #ifdef de placa —
// caracteristicas diferentes vivem no BoardProfile abaixo.
// ============================================================================

struct SdConfig {
    int cs, sck, miso, mosi;
    int freqKhz;
};

struct BoardProfile {
    const char* id;          // "smartdisplay_4in" / "cyd"
    const char* otaChannel;  // canal de updates (updates/<canal>/)
    const char* name;        // nome amigavel (shell/info)
    SdConfig sd;
    bool hasPsram;
    bool backlightPwm;       // brilho controlavel
    bool capacitiveTouch;    // sem calibracao interativa
};

// Display concreto da placa (BoardDisplay, de Boards/<placa>/BoardDisplay.h).
using CelerDisplay = BoardDisplay;

namespace Board {

// Inicializa o display da placa (chamar uma vez no boot, antes da UI).
void init();

// Display global (valido apos init(); a instancia vive no Board.cpp da placa)
CelerDisplay& display();

const BoardProfile& profile();

}  // namespace Board

#endif  // CELER_BOARDS_BOARD_H
