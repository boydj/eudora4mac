#include "mail/attachment_decode.hpp"

#include <array>
#include <cstdint>

#include "compat/endian.hpp"

namespace eudora {

namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
           c == '\v';
}

} // namespace

// ---- uuencode --------------------------------------------------------------

DecodedAttachment decode_uuencode(std::string_view text) {
    DecodedAttachment out;
    const auto begin = text.find("begin ");
    if (begin == std::string_view::npos)
        return out;
    // "begin <mode> <name>\n"
    std::size_t p = begin + 6;
    while (p < text.size() && text[p] != ' ')
        ++p; // skip mode
    while (p < text.size() && text[p] == ' ')
        ++p;
    std::size_t name_start = p;
    while (p < text.size() && text[p] != '\r' && text[p] != '\n')
        ++p;
    out.filename.assign(text.substr(name_start, p - name_start));
    // trim trailing spaces from the name
    while (!out.filename.empty() && is_space(out.filename.back()))
        out.filename.pop_back();

    auto dec = [](char c) -> int { return (c - 0x20) & 0x3F; };
    std::string data;
    // Advance to the next line.
    while (p < text.size() && (text[p] == '\r' || text[p] == '\n'))
        ++p;
    while (p < text.size()) {
        std::size_t eol = p;
        while (eol < text.size() && text[eol] != '\r' && text[eol] != '\n')
            ++eol;
        std::string_view line = text.substr(p, eol - p);
        p = eol;
        while (p < text.size() && (text[p] == '\r' || text[p] == '\n'))
            ++p;
        if (line.empty())
            continue;
        if (line[0] == '`' || line == "end" || line.substr(0, 3) == "end")
            break;
        int n = dec(line[0]);
        if (n <= 0)
            continue;
        std::size_t i = 1;
        while (n > 0 && i + 1 < line.size()) {
            const int c0 = dec(line[i]);
            const int c1 = i + 1 < line.size() ? dec(line[i + 1]) : 0;
            const int c2 = i + 2 < line.size() ? dec(line[i + 2]) : 0;
            const int c3 = i + 3 < line.size() ? dec(line[i + 3]) : 0;
            if (n-- > 0)
                data += static_cast<char>((c0 << 2) | (c1 >> 4));
            if (n-- > 0)
                data += static_cast<char>((c1 << 4) | (c2 >> 2));
            if (n-- > 0)
                data += static_cast<char>((c2 << 6) | c3);
            i += 4;
        }
    }
    out.data = std::move(data);
    out.ok = !out.data.empty();
    return out;
}

// ---- BinHex 4.0 ------------------------------------------------------------

namespace {

// The BinHex 4.0 6-bit alphabet.
constexpr std::string_view kBinHexAlphabet =
    "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";

std::array<int, 256> binhex_reverse() {
    std::array<int, 256> t{};
    t.fill(-1);
    for (int i = 0; i < static_cast<int>(kBinHexAlphabet.size()); ++i)
        t[static_cast<unsigned char>(kBinHexAlphabet[i])] = i;
    return t;
}

} // namespace

DecodedAttachment decode_binhex(std::string_view text) {
    DecodedAttachment out;
    // The payload is between the first ':' after the comment line and the
    // trailing ':'.
    const auto start = text.find(':');
    if (start == std::string_view::npos)
        return out;
    const auto end = text.find(':', start + 1);
    if (end == std::string_view::npos)
        return out;
    const std::string_view payload = text.substr(start + 1, end - start - 1);

    static const auto rev = binhex_reverse();

    // 6-bit decode, skipping whitespace.
    std::string sixbit;
    int acc = 0, bits = 0;
    for (char ch : payload) {
        if (is_space(ch))
            continue;
        const int v = rev[static_cast<unsigned char>(ch)];
        if (v < 0)
            continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            sixbit += static_cast<char>((acc >> bits) & 0xFF);
        }
    }

    // RLE90 decompress: <byte> 0x90 <count> => byte * count; count 0 => 0x90.
    std::string raw;
    for (std::size_t i = 0; i < sixbit.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(sixbit[i]);
        if (c == 0x90 && i + 1 < sixbit.size()) {
            const unsigned char count =
                static_cast<unsigned char>(sixbit[i + 1]);
            ++i;
            if (count == 0) {
                raw += static_cast<char>(0x90);
            } else if (!raw.empty()) {
                const char last = raw.back();
                for (int k = 1; k < count; ++k)
                    raw += last;
            }
        } else {
            raw += static_cast<char>(c);
        }
    }

    // Header: 1 byte namelen, name, 1 version, 4 type, 4 creator, 2 flags,
    // 4 dataLen, 4 rsrcLen, 2 header CRC, then the data fork.
    if (raw.empty())
        return out;
    const std::size_t namelen = static_cast<unsigned char>(raw[0]);
    std::size_t pos = 1;
    if (pos + namelen + 1 + 4 + 4 + 2 + 4 + 4 + 2 > raw.size())
        return out;
    out.filename = raw.substr(pos, namelen);
    pos += namelen;
    pos += 1;         // version
    pos += 4 + 4 + 2; // type, creator, flags
    const std::uint32_t data_len =
        load_be32(reinterpret_cast<const std::uint8_t *>(raw.data() + pos));
    pos += 4;
    pos += 4; // resource fork length
    pos += 2; // header CRC
    if (pos + data_len > raw.size())
        return out;
    out.data = raw.substr(pos, data_len);
    out.ok = true;
    return out;
}

// ---- AppleSingle / AppleDouble --------------------------------------------

DecodedAttachment decode_applefile(std::string_view bytes) {
    DecodedAttachment out;
    if (bytes.size() < 26)
        return out;
    const auto *b = reinterpret_cast<const std::uint8_t *>(bytes.data());
    const std::uint32_t magic = load_be32(b);
    if (magic != 0x00051600u && magic != 0x00051607u) // AppleSingle/Double
        return out;
    const std::uint16_t entries = load_be16(b + 24);
    std::size_t p = 26;
    std::string data_fork, real_name;
    for (std::uint16_t i = 0; i < entries; ++i) {
        if (p + 12 > bytes.size())
            break;
        const std::uint32_t id = load_be32(b + p);
        const std::uint32_t off = load_be32(b + p + 4);
        const std::uint32_t len = load_be32(b + p + 8);
        p += 12;
        if (static_cast<std::size_t>(off) + len > bytes.size())
            continue;
        const std::string_view slice = bytes.substr(off, len);
        if (id == 1) // data fork
            data_fork.assign(slice);
        else if (id == 3) // real name
            real_name.assign(slice);
    }
    out.filename = std::move(real_name);
    out.data = std::move(data_fork);
    out.ok = !out.data.empty();
    return out;
}

// ---- dispatch --------------------------------------------------------------

DecodedAttachment decode_classic_attachment(std::string_view data) {
    if (data.size() >= 4) {
        const auto *b = reinterpret_cast<const std::uint8_t *>(data.data());
        const std::uint32_t magic = load_be32(b);
        if (magic == 0x00051600u || magic == 0x00051607u)
            return decode_applefile(data);
    }
    if (data.find("(This file must be converted with BinHex") !=
            std::string_view::npos ||
        (data.find(':') != std::string_view::npos &&
         data.find("BinHex") != std::string_view::npos)) {
        auto r = decode_binhex(data);
        if (r.ok)
            return r;
    }
    if (data.find("begin ") != std::string_view::npos) {
        auto r = decode_uuencode(data);
        if (r.ok)
            return r;
    }
    return {};
}

} // namespace eudora
