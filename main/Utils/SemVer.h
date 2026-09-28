#pragma once

/**
 * @file SemVer.h
 * @brief Comparador de versões simples (x.y.z), header-only e testável no
 * host (F5): extraído do OtaManager.cpp para poder rodar no CI sem ESP-IDF.
 */

#include <string>
#include <cstdlib>

namespace celer {

inline bool versionGreater(const std::string& newVer, const std::string& oldVer) {
    int newParts[3] = {0, 0, 0}, oldParts[3] = {0, 0, 0};
    auto parseV = [](const std::string& v, int* p) {
        int pt = 0, st = 0;
        while (pt < 3 && st < (int)v.length()) {
            int d = (int)v.find('.', st);
            if (d == -1 || d == (int)std::string::npos) {
                p[pt] = atoi(v.substr(st).c_str());
                break;
            }
            p[pt] = atoi(v.substr(st, d - st).c_str());
            st = d + 1;
            pt++;
        }
    };
    parseV(newVer, newParts);
    parseV(oldVer, oldParts);
    if (newParts[0] > oldParts[0]) return true;
    if (newParts[0] < oldParts[0]) return false;
    if (newParts[1] > oldParts[1]) return true;
    if (newParts[1] < oldParts[1]) return false;
    return newParts[2] > oldParts[2];
}

}  // namespace celer
