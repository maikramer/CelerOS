#pragma once

#include <string>

// Configuracoes do sistema em NVS (namespace "settings", F3). Antes eram 6+
// arquivos .txt soltos no LittleFS — sem tipo, sem CRC, quebraveis por
// qualquer app com acesso ao FS. A migracao (migrateLegacy) importa os
// arquivos classicos UMA vez e os apaga: consumers continuam vendo o mesmo
// valor.
//
// Flags (web_on, nowifi, install_sd) sao "1"/"0"; valores sao strings
// curtas (brightness, ota_url).
namespace CelerSettings {

// Valor da key ("" se ausente). def volta quando ausente.
std::string get(const char* key, const char* def = "");

// Grava (ate 63 chars). value vazio apaga a key.
bool set(const char* key, const char* value);

bool erase(const char* key);

// Apaga todas as keys (factoryReset "configs")
bool eraseAll();

// Importacao one-shot dos .txt classicos: se a key NVS nao existe e o
// arquivo sim, importa (flags = arquivo existe; strings = conteudo) e
// APAGA o arquivo. Chamado no boot (WebManager::init/startAsync).
void migrateLegacy();

}  // namespace CelerSettings
