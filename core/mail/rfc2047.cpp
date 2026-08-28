#include "mail/rfc2047.hpp"

#include <cctype>

#include "compat/charset.hpp"
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
