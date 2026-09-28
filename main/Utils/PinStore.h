#pragma once

// PIN do Settings com salt. O formato legado (settings_pin.txt = MD5 puro de
// 4-6 digitos) era quebravel offline em segundos e ainda era bypassavel
// apagando o arquivo. Formato novo: /local/settings_pin2.bin = "CP2" +
// salt(16) + SHA-256(salt + pin). A flag "pin_set" no NVS (namespace
// "celer") espelha a existencia do PIN: flag setada sem arquivo = estado
// corrompido — o Settings pede redefinicao em vez de abrir destravado.
// Login com PIN legado valido regrava no formato novo (migracao
// transparente) e apaga o .txt antigo.
namespace PinStore {

// Grava/redefine o PIN (4-6 digitos). Retorna false se fora do formato.
bool set(const char* pin);

// Confere o PIN contra o formato novo; na ausencia dele, contra o MD5
// legado (fazendo o upgrade para o formato novo em caso de sucesso).
bool verify(const char* pin);

// Remove o PIN (arquivo novo, legado e flag do NVS).
bool clear();

// 0 = sem PIN, 1 = ativo, 2 = corrompido (flag NVS sem arquivo)
int state();

}  // namespace PinStore
