#ifndef KRYONOS_PROJECT_CONFIG_H
#define KRYONOS_PROJECT_CONFIG_H

// Configuracoes globais do projeto KryonOS.
//
// Este componente existe porque os componentes internos (Storage, JsonModels,
// ...) incluem "projectConfig.h" — no satisfaction-hub ele vinha do pacote
// esp_components; aqui e do propio repo.
//
// O KryonOS nao usa o SdCard do componente Storage (o FileSystem do OS monta
// o SD com os pinos da placa, ver main/FileSystem); os defines abaixo ficam
// documentados caso o componente passe a ser usado.

// SmartDisplay ESP32-S3-4848S040: SD no FSPI (compartilhado com o init do
// painel): SCK 48, MISO 41, MOSI 47, CS 42.
#define SDCS0 ((gpio_num_t)42)
#define SD_SCLK 48
#define SD_MISO 41
#define SD_MOSI 47

#endif // KRYONOS_PROJECT_CONFIG_H
