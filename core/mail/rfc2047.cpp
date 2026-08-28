#include "mail/rfc2047.hpp"

#include <array>
#include <cctype>

#include "compat/macroman.hpp"
#include "mail/mime_codec.hpp"

namespace eudora {

namespace {

int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string latin1_to_utf8(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// Windows-1252 differs from Latin-1 only in 0x80-0x9F.
std::string cp1252_to_utf8(std::string_view s) {
    static constexpr std::array<char32_t, 32> kHigh = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    std::string out;
    for (unsigned char c : s) {
        char32_t cp = c;
        if (c >= 0x80 && c <= 0x9F)
            cp = kHigh[c - 0x80];
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// Convert decoded bytes in `charset` to UTF-8; false if charset unknown.
bool charset_to_utf8(std::string_view charset, std::string_view bytes,
                     std::string &out) {
    // Strip RFC 2231 language suffix ("utf-8*en").
    const auto star = charset.find('*');
    if (star != std::string_view::npos)
        charset = charset.substr(0, star);

    if (iequals(charset, "utf-8") || iequals(charset, "utf8") ||
        iequals(charset, "us-ascii") || iequals(charset, "ascii")) {
        out.assign(bytes);
        return true;
    }
    if (iequals(charset, "iso-8859-1") || iequals(charset, "latin1")) {
        out = latin1_to_utf8(bytes);
        return true;
    }
    if (iequals(charset, "windows-1252") || iequals(charset, "cp1252")) {
        out = cp1252_to_utf8(bytes);
        return true;
    }
    if (iequals(charset, "macintosh") || iequals(charset, "x-mac-roman") ||
        iequals(charset, "mac")) {
        out = macroman_to_utf8(bytes);
        return true;
    }
    return false;
}

} // namespace

std::string decode_q_encoding(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '_') {
            out += ' ';
        } else if (c == '=' && i + 2 < text.size()) {
            const int hi = hex_digit(static_cast<unsigned char>(text[i + 1]));
            const int lo = hex_digit(static_cast<unsigned char>(text[i + 2]));
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::string encode_rfc2047(std::string_view text) {
    bool ascii = true;
    for (unsigned char c : text)
        if (c >= 0x80 || c == '\r' || c == '\n') {
            ascii = false;
            break;
        }
    if (ascii)
        return std::string(text);

    // Chunk at UTF-8 boundaries; 45 raw bytes -> 60 base64 chars, keeping
    // each "=?utf-8?B?...?=" word under the 75-character limit.
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t take = std::min<std::size_t>(45, text.size() - i);
        // Don't split inside a UTF-8 sequence.
        while (take > 1 && i + take < text.size() &&
               (static_cast<unsigned char>(text[i + take]) & 0xC0) == 0x80)
            --take;
        if (!out.empty())
            out += ' '; // whitespace between encoded words decodes to nothing
        out += "=?utf-8?B?";
        out += base64_encode(text.substr(i, take));
        out += "?=";
        i += take;
    }
    return out;
}

std::string decode_rfc2047(std::string_view header) {
    std::string out;
    std::size_t i = 0;
    bool last_was_encoded = false;
    std::size_t pending_ws_start = std::string::npos;

    while (i < header.size()) {
        // Look for "=?charset?E?text?=".
        if (header[i] == '=' && i + 1 < header.size() && header[i + 1] == '?') {
            const std::size_t q0 = i + 1;
            const std::size_t q1 = header.find('?', q0 + 1);
            if (q1 != std::string_view::npos && q1 + 2 < header.size() &&
                header[q1 + 2] == '?') {
                const std::size_t q2 = q1 + 2;
                const std::size_t q3 = header.find('?', q2 + 1);
                if (q3 != std::string_view::npos && q3 + 1 < header.size() &&
                    header[q3 + 1] == '=') {
                    const std::string_view charset =
                        header.substr(q0 + 1, q1 - q0 - 1);
                    const char enc = header[q1 + 1];
                    const std::string_view text =
                        header.substr(q2 + 1, q3 - q2 - 1);

                    std::string bytes;
                    bool ok = true;
                    if (enc == 'Q' || enc == 'q') {
                        bytes = decode_q_encoding(text);
                    } else if (enc == 'B' || enc == 'b') {
                        ok = base64_decode(text, bytes);
                    } else {
                        ok = false;
                    }
                    std::string decoded;
                    if (ok)
                        ok = charset_to_utf8(charset, bytes, decoded);
                    if (ok) {
                        // Whitespace between adjacent encoded words vanishes.
                        if (last_was_encoded && pending_ws_start != std::string::npos)
                            out.resize(pending_ws_start);
                        out += decoded;
                        i = q3 + 2;
                        last_was_encoded = true;
                        pending_ws_start = std::string::npos;
                        continue;
                    }
                }
            }
        }

        const char c = header[i];
        if (c == ' ' || c == '\t') {
            if (pending_ws_start == std::string::npos)
                pending_ws_start = out.size();
        } else {
            pending_ws_start = std::string::npos;
            last_was_encoded = false;
        }
        out += c;
        ++i;
    }
    return out;
}

} // namespace eudora
