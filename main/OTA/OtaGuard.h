#ifndef OTA_GUARD_H
#define OTA_GUARD_H

// Exclusao mutua entre os escritores da particao OTA (celerctl push serial/
// bridge, upload web /update, hub): tres caminhos chamam esp_ota_begin na
// mesma particao e o IDF nao serializa — o guard recusa o 2o escritor em vez
// de deixar dois streams pisarem um no outro.
//
// Header-only: inline variable do C++17 tem vinculagem unica (um estado por
// programa, nao um por TU).

#include <atomic>

namespace OtaGuard {

inline std::atomic<bool> s_held{false};

inline bool acquire() {
    bool expected = false;
    return s_held.compare_exchange_strong(expected, true);
}

inline void release() { s_held.store(false); }

inline bool held() { return s_held.load(); }

}  // namespace OtaGuard

#endif  // OTA_GUARD_H
