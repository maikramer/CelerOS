#ifndef CELEROS_BOARDS_CYD_VSPI_TRAITS_H
#define CELEROS_BOARDS_CYD_VSPI_TRAITS_H

// Trios de compilacao da CYD-VSPI (variante de pinout antiga, mesma UI da
// CYD classica: 240x320, telhado de fonte pequeno). Constantes constexpr:
// ramos mortos somem no link.
namespace BoardTraits {
constexpr bool largeUi = false;
}

#endif  // CELEROS_BOARDS_CYD_VSPI_TRAITS_H
