#include "compat/macroman.hpp"

#include <array>
#include <cstdint>

namespace eudora {

// Unicode code points for MacRoman 0x80-0xFF (Apple's canonical table).
static constexpr std::array<char32_t, 128> kMacRomanHigh = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, // 80-87
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8, // 88-8F
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, // 90-97
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, // 98-9F
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, // A0-A7
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, // A8-AF
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, // B0-B7
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, // B8-BF
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, // C0-C7
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, // C8-CF
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, // D0-D7
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, // D8-DF
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, // E0-E7
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, // E8-EF
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, // F0-F7
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7, // F8-FF
};

static void append_utf8(std::string &out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string macroman_to_utf8(std::string_view macroman) {
    std::string out;
    out.reserve(macroman.size());
    for (unsigned char c : macroman) {
        if (c < 0x80)
            out += static_cast<char>(c);
        else
            append_utf8(out, kMacRomanHigh[c - 0x80]);
    }
    return out;
}

// Decode one UTF-8 code point; advances i past it.  Invalid sequences decode
// as U+FFFD one byte at a time.
static char32_t next_cp(std::string_view s, std::size_t &i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t need = 0;
    char32_t cp = 0;
    if (c < 0x80) {
        ++i;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        need = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        need = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        need = 3;
        cp = c & 0x07;
    } else {
        ++i;
        return 0xFFFD;
    }
    if (i + need >= s.size()) {
        ++i;
        return 0xFFFD;
    }
    for (std::size_t k = 1; k <= need; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += need + 1;
    return cp;
}

bool utf8_to_macroman(std::string_view utf8, std::string &out) {
    out.clear();
    out.reserve(utf8.size());
    bool clean = true;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const char32_t cp = next_cp(utf8, i);
        if (cp < 0x80) {
            out += static_cast<char>(cp);
            continue;
        }
        bool mapped = false;
        for (std::size_t k = 0; k < kMacRomanHigh.size(); ++k) {
            if (kMacRomanHigh[k] == cp) {
                out += static_cast<char>(0x80 + k);
                mapped = true;
                break;
            }
        }
        if (!mapped) {
            out += '?';
            clean = false;
        }
    }
    return clean;
}

bool is_ascii(std::string_view s) {
    for (unsigned char c : s)
        if (c >= 0x80)
            return false;
    return true;
}

} // namespace eudora
