#ifndef KRYONOS_SETTINGS_SCREENS_H
#define KRYONOS_SETTINGS_SCREENS_H

// ============================================================================
// SettingsScreens — port W7c-2 do SettingsUI para o Kui.
//
// Telas: menu principal + Wi-Fi + Apps + Hora/Fuso + Ajuste manual + Seguranca
// (PIN) + Tela (brilho) + Atualizacao (OTA) + Sobre. O gate de PIN virou uma
// PinPadScreen (nao-bloqueante): o loop da UI segue vivo durante o desbloqueio.
//
// Entrada: SettingsScreens::open() — resolve o gate de PIN e empurra o menu.
// ============================================================================

namespace SettingsScreens {
void open();
}

#endif  // KRYONOS_SETTINGS_SCREENS_H
