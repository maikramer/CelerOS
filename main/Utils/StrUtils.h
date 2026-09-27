#ifndef CELEROS_STR_UTILS_H
#define CELEROS_STR_UTILS_H

#include <string>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

// Helpers de string no espirito da API Arduino String, sobre std::string.
// Usados durante a migracao Arduino String -> std::string.
namespace kstr {

inline bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline long toInt(const std::string& s) { return strtol(s.c_str(), nullptr, 10); }
inline double toFloat(const std::string& s) { return strtod(s.c_str(), nullptr); }

// indexOf estilo Arduino: retorna -1 quando nao encontra
inline int indexOf(const std::string& s, const std::string& needle, int from = 0) {
    size_t p = s.find(needle, (size_t)(from < 0 ? 0 : from));
    return p == std::string::npos ? -1 : (int)p;
}
inline int indexOf(const std::string& s, char c, int from = 0) {
    size_t p = s.find(c, (size_t)(from < 0 ? 0 : from));
    return p == std::string::npos ? -1 : (int)p;
}
inline int lastIndexOf(const std::string& s, char c) {
    size_t p = s.rfind(c);
    return p == std::string::npos ? -1 : (int)p;
}

inline std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
    return s;
}

// Formatacao estilo sprintf -> std::string
inline std::string fmt(const char* format, ...) __attribute__((format(printf, 1, 2)));
inline std::string fmt(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list args2;
    va_copy(args2, args);
    int n = vsnprintf(nullptr, 0, format, args);
    va_end(args);
    if (n <= 0) { va_end(args2); return ""; }
    std::string out((size_t)n, '\0');
    vsnprintf(&out[0], (size_t)n + 1, format, args2);
    va_end(args2);
    return out;
}

} // namespace kstr

#endif // CELEROS_STR_UTILS_H
