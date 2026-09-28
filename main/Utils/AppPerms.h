#pragma once

/**
 * @file AppPerms.h
 * @brief Capabilities declaradas no app.json (F4), header-only e testável
 * no host (F5). Extraído do JSBindings.h para o CI não puxar headers do
 * Duktape.
 */

#include <string>
#include <cstdint>

namespace celer {

enum AppPerm : uint32_t {
    PERM_FS     = 1u << 0,
    PERM_NET    = 1u << 1,
    PERM_GPIO   = 1u << 2,
    PERM_SYSTEM = 1u << 3,   // restart/factoryReset/OTA/web/wifiConnect
    PERM_ALL    = 0xFFFFFFFFu,
};

// "permissions": ["fs","net","gpio","system"] — campo ausente (ou array
// vazio/inválido) = PERM_ALL (compat com apps existentes: não tranca app
// por engano de formatação).
inline uint32_t parsePermissions(const std::string& appJson) {
    size_t key = appJson.find("\"permissions\"");
    if (key == std::string::npos) return PERM_ALL;
    size_t open = appJson.find('[', key);
    size_t close = appJson.find(']', key);
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return PERM_ALL;
    }
    std::string arr = appJson.substr(open, close - open);
    uint32_t m = 0;
    if (arr.find("\"fs\"") != std::string::npos) m |= PERM_FS;
    if (arr.find("\"net\"") != std::string::npos) m |= PERM_NET;
    if (arr.find("\"gpio\"") != std::string::npos) m |= PERM_GPIO;
    if (arr.find("\"system\"") != std::string::npos) m |= PERM_SYSTEM;
    return m == 0 ? PERM_ALL : m;
}

// true se o app.json DECLARA a capability "net" (campo presente + net na
// lista). Apps sem campo sao tratados como basicos: em placas sem PSRAM o
// radio WiFi e desligado durante a execucao (devolve ~35KB de heap — sem
// isso nenhum app JS abre na RAM interna). Um app que use Net sem declarar
// recebe "WiFi is not connected" (nao rodava mesmo com o radio ligado).
inline bool declaresNet(const std::string& appJson) {
    size_t key = appJson.find("\"permissions\"");
    if (key == std::string::npos) return false;
    size_t open = appJson.find('[', key);
    size_t close = appJson.find(']', key);
    if (open == std::string::npos || close == std::string::npos || close < open) return false;
    return appJson.substr(open, close - open).find("\"net\"") != std::string::npos;
}

}  // namespace celer