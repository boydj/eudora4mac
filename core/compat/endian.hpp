// Big-endian serialization helpers for Eudora's legacy on-disk formats.
//
// The classic Mac builds (68K and PowerPC) persisted raw struct images:
// every multi-byte scalar is big-endian, structs use mac68k (2-byte)
// alignment, and CodeWarrior packed bitfields MSB-first within their
// storage unit.  The original sources contain no byte-swapping at all
// (they never ran on a little-endian machine), so all decoding here is
// explicit and offset-based — structs are never memcpy'd from disk.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace eudora {

// ---- scalar conversions ---------------------------------------------------

constexpr std::uint16_t load_be16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

constexpr std::uint32_t load_be32(const std::uint8_t *p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

constexpr void store_be16(std::uint8_t *p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}

constexpr void store_be32(std::uint8_t *p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// ---- CodeWarrior bitfield extraction --------------------------------------
//
// CodeWarrior for PPC/68K allocates bitfields starting at the most
// significant bit of the storage unit.  For a declaration
//     uLong a:14; uLong b:1; ... ;
// field `a` occupies bits 31..18 of the big-endian 32-bit word, `b` bit 17,
// and so on.  `first_bit` counts from the MSB (0 == bit 31).

constexpr std::uint32_t cw_bits(std::uint32_t word, unsigned first_bit, unsigned width) {
    const unsigned shift = 32u - first_bit - width;
    const std::uint32_t mask = width >= 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return (word >> shift) & mask;
}

constexpr std::uint32_t cw_set_bits(std::uint32_t word, unsigned first_bit, unsigned width,
                                    std::uint32_t value) {
    const unsigned shift = 32u - first_bit - width;
    const std::uint32_t mask = (width >= 32 ? 0xFFFFFFFFu : ((1u << width) - 1u)) << shift;
    return (word & ~mask) | ((value << shift) & mask);
}

// Sign-extend an n-bit two's-complement value (for signed bitfields such as
// MSumType.spamScore, declared `long spamScore:8`).
constexpr std::int32_t sign_extend(std::uint32_t value, unsigned width) {
    const std::uint32_t sign = 1u << (width - 1);
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

// ---- offset-based reader / writer -----------------------------------------

class BigEndianReader {
public:
    explicit BigEndianReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::size_t size() const { return data_.size(); }
    bool has(std::size_t offset, std::size_t len) const {
        return offset <= data_.size() && len <= data_.size() - offset;
    }

    std::uint8_t u8(std::size_t off) const { return data_[off]; }
    std::uint16_t u16(std::size_t off) const { return load_be16(&data_[off]); }
    std::uint32_t u32(std::size_t off) const { return load_be32(&data_[off]); }
    std::int16_t i16(std::size_t off) const { return static_cast<std::int16_t>(u16(off)); }
    std::int32_t i32(std::size_t off) const { return static_cast<std::int32_t>(u32(off)); }

    std::span<const std::uint8_t> bytes(std::size_t off, std::size_t len) const {
        return data_.subspan(off, len);
    }

private:
    std::span<const std::uint8_t> data_;
};

class BigEndianWriter {
public:
    explicit BigEndianWriter(std::size_t size) : data_(size, 0) {}

    std::vector<std::uint8_t> &data() { return data_; }
    const std::vector<std::uint8_t> &data() const { return data_; }

    void u8(std::size_t off, std::uint8_t v) { data_[off] = v; }
    void u16(std::size_t off, std::uint16_t v) { store_be16(&data_[off], v); }
    void u32(std::size_t off, std::uint32_t v) { store_be32(&data_[off], v); }
    void i16(std::size_t off, std::int16_t v) { u16(off, static_cast<std::uint16_t>(v)); }
    void i32(std::size_t off, std::int32_t v) { u32(off, static_cast<std::uint32_t>(v)); }

    void bytes(std::size_t off, std::span<const std::uint8_t> src) {
        std::copy(src.begin(), src.end(), data_.begin() + static_cast<std::ptrdiff_t>(off));
    }

private:
    std::vector<std::uint8_t> data_;
};

} // namespace eudora
