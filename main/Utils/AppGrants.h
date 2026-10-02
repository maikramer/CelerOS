#pragma once

/**
 * @file AppGrants.h
 * @brief Permissoes CONCEDIDAS pelo usuario a cada app (consentimento).
 *
 * O app.json DECLARA capabilities (AppPerms.h); ate aqui a declaracao bastava
 * — qualquer app podia pedir "system" e levar. Agora o runtime recebe
 * declaradas & concedidas: o que falta passa por um dialogo nativo no
 * launcher antes de abrir. Concessoes vivem no NVS (namespace "app_grants"),
 * fora do alcance do FS dos apps.
 *
 * Identidade = packageName (ou o caminho, para .js avulso) + hash do caminho
 * de instalacao: o mesmo pacote noutra pasta (copia no SD, sideload com nome
 * de app de sistema) nao herda a concessao.
 *
 * Migracao: no primeiro scan com este firmware, os apps JA instalados sao
 * concedidos com o que declaram (nada quebra para quem ja usava o aparelho).
 */

#include <cstdint>
#include <string>

namespace AppGrants {

// Mascara concedida; false = nunca concedido (ou concedido noutro caminho)
bool lookup(const std::string& pkg, const std::string& path, uint32_t* mask);

// Concede (substitui) a mascara para o app neste caminho
bool grant(const std::string& pkg, const std::string& path, uint32_t mask);

// Esquece o app (desinstalacao): a reinstalacao pede consentimento de novo
void revoke(const std::string& pkg);

// Migracao one-shot dos apps instalados antes do consentimento existir
bool migrated();
void setMigrated();

// "arquivos, rede, GPIO, sistema" (para o dialogo de consentimento)
std::string describe(uint32_t mask);

}  // namespace AppGrants
