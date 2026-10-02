#pragma once

// Protocolo Bangle.js do Gadgetbridge (Android) — parte pura, sem BLE nem
// JSON: montagem de linhas a partir das escritas do NUS, classificacao da
// linha e conversao de fuso. Header-only, testado no host
// (test/cpp/run_tests.cpp). O JSON do GB({...}) e lido no firmware
// (Bluetooth/PhoneLink.cpp, ArduinoJson).
//
// O celular manda codigo "Espruino" em linhas terminadas em \n, as vezes
// prefixadas por \x10 (DLE = sem eco) e quebradas em varios writes do ATT:
//   GB({"t":"notify","id":17,"src":"WhatsApp","title":"Ana","body":"oi"})
//   setTime(1790935200);E.setTimeZone(-3.0);(s=>{...})(...)
// O relogio responde com JSON por linha: {"t":"status","bat":85,"chg":0}

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace celer {
namespace gb {

// Junta os pedacos do RX em linhas. Linha maior que maxLen e descartada
// inteira (ate o proximo \n) — protege a RAM de um peer abusivo.
class LineAssembler {
public:
    explicit LineAssembler(size_t maxLen = 1024) : m_max(maxLen) {}

    // Alimenta bytes; onLine(const std::string&) a cada linha completa.
    template <typename F>
    void feed(const uint8_t* data, size_t len, F onLine) {
        for (size_t i = 0; i < len; i++) {
            const char c = (char)data[i];
            if (c == '\n') {
                if (!m_overflow && !m_buf.empty()) onLine(m_buf);
                m_buf.clear();
                m_overflow = false;
                continue;
            }
            if (c == '\r' || c == '\x10') continue;  // DLE: "sem eco" do Espruino
            if (m_overflow) continue;
            if (m_buf.size() >= m_max) {
                m_overflow = true;
                m_buf.clear();
                continue;
            }
            m_buf += c;
        }
    }
    void reset() {
        m_buf.clear();
        m_overflow = false;
    }

private:
    std::string m_buf;
    size_t m_max;
    bool m_overflow = false;
};

enum class Kind { Unknown, Gb, SetTime };

struct Line {
    Kind kind = Kind::Unknown;
    std::string json;      // Gb: o objeto entre GB( e o ultimo )
    int64_t epoch = 0;     // SetTime
    bool hasTz = false;
    float tzHours = 0;     // E.setTimeZone(h)
};

inline Line classify(const std::string& raw) {
    Line out;
    size_t s = 0;
    while (s < raw.size() && (raw[s] == ' ' || raw[s] == '\t')) s++;
    if (raw.compare(s, 3, "GB(") == 0) {
        size_t a = s + 3;
        size_t b = raw.rfind(')');
        if (b != std::string::npos && b > a && raw[a] == '{') {
            out.kind = Kind::Gb;
            out.json = raw.substr(a, b - a);
        }
        return out;
    }
    size_t t = raw.find("setTime(");
    if (t != std::string::npos) {
        char* end = nullptr;
        long long e = strtoll(raw.c_str() + t + 8, &end, 10);
        if (end != nullptr && *end == ')' && e > 1500000000LL) {
            out.kind = Kind::SetTime;
            out.epoch = e;
            size_t z = raw.find("E.setTimeZone(");
            if (z != std::string::npos) {
                char* zend = nullptr;
                double h = strtod(raw.c_str() + z + 14, &zend);
                if (zend != nullptr && *zend == ')' && h >= -14 && h <= 14) {
                    out.hasTz = true;
                    out.tzHours = (float)h;
                }
            }
        }
    }
    return out;
}

// Fuso em horas (leste positivo, como o Espruino) -> TZ POSIX. POSIX inverte
// o sinal: UTC-3 vira "<-03>3"; UTC+5:30 vira "<+0530>-5:30".
inline std::string tzPosix(float hours) {
    int mins = (int)(hours * 60.0f + (hours >= 0 ? 0.5f : -0.5f));
    if (mins == 0) return "<+00>0";
    const char sign = mins < 0 ? '-' : '+';
    int am = mins < 0 ? -mins : mins;
    int h = am / 60, m = am % 60;
    char name[16], off[16];
    if (m) {
        snprintf(name, sizeof(name), "<%c%02d%02d>", sign, h, m);
        snprintf(off, sizeof(off), "%s%d:%02d", mins < 0 ? "" : "-", h, m);
    } else {
        snprintf(name, sizeof(name), "<%c%02d>", sign, h);
        snprintf(off, sizeof(off), "%s%d", mins < 0 ? "" : "-", h);
    }
    return std::string(name) + off;
}

// Texto do Android (UTF-8 qualquer) -> UTF-8 so com U+0020..U+00FF, que e o
// que as fontes do CelerOS desenham. Pontuacao tipografica vira ASCII,
// emoji/CJK somem, quebras viram espaco e o corte em maxBytes nunca parte
// uma sequencia ao meio.
inline std::string latin1Safe(const std::string& in, size_t maxBytes) {
    std::string out;
    size_t i = 0;
    auto put = [&](const char* s) {
        size_t n = strlen(s);
        if (out.size() + n > maxBytes) return false;
        out.append(s, n);
        return true;
    };
    while (i < in.size()) {
        unsigned char c = (unsigned char)in[i];
        uint32_t cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { i++; continue; }  // byte de continuacao solto
        if (i + len > in.size()) break;
        bool bad = false;
        for (size_t k = 1; k < len; k++) {
            unsigned char cc = (unsigned char)in[i + k];
            if ((cc & 0xC0) != 0x80) { bad = true; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (bad) { i++; continue; }
        i += len;
        bool ok = true;
        if (cp == '\n' || cp == '\r' || cp == '\t') ok = put(" ");
        else if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) continue;
        else if (cp < 0x80) { char b[2] = {(char)cp, 0}; ok = put(b); }
        else if (cp <= 0xFF) {
            char b[3] = {(char)(0xC0 | (cp >> 6)), (char)(0x80 | (cp & 0x3F)), 0};
            ok = put(b);
        }
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201B) ok = put("'");
        else if (cp == 0x201C || cp == 0x201D) ok = put("\"");
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) ok = put("-");
        else if (cp == 0x2026) ok = put("...");
        else if (cp == 0x2022) ok = put("*");
        else if (cp == 0x00A0 || cp == 0x2009 || cp == 0x200A || cp == 0x202F) ok = put(" ");
        else continue;  // emoji, CJK, seletores de variacao: some
        if (!ok) break;
    }
    // espacos repetidos (emoji removido entre palavras) e nas pontas
    std::string tidy;
    for (char ch : out) {
        if (ch == ' ' && (tidy.empty() || tidy.back() == ' ')) continue;
        tidy += ch;
    }
    while (!tidy.empty() && tidy.back() == ' ') tidy.pop_back();
    return tidy;
}

// String JSON segura (aspas, barra, controle) para as respostas.
inline std::string jsonStr(const std::string& in) {
    std::string out = "\"";
    for (char c : in) {
        unsigned char u = (unsigned char)c;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", u);
            out += b;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

}  // namespace gb
}  // namespace celer
