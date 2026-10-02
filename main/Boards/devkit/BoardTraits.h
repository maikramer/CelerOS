#ifndef CELEROS_BOARDS_DEVKIT_TRAITS_H
#define CELEROS_BOARDS_DEVKIT_TRAITS_H

// Tracos de compilacao do devkit (espaco virtual 240x320, stub de painel).
// Constantes constexpr: o codigo comum escolhe por elas com ramos que o
// compilador descarta — as fontes da tela grande nem entram no binario.
namespace BoardTraits {
constexpr bool largeUi = false;
}

#endif  // CELEROS_BOARDS_DEVKIT_TRAITS_H
