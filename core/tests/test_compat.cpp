#include "compat/endian.hpp"
#include "compat/pstring.hpp"
#include "test_framework.hpp"

using namespace eudora;

TEST_CASE("be16/be32 round trip") {
    std::uint8_t buf[4];
    store_be16(buf, 0xBEEF);
    CHECK_EQ(buf[0], 0xBE);
    CHECK_EQ(buf[1], 0xEF);
    CHECK_EQ(load_be16(buf), 0xBEEF);

    store_be32(buf, 0xDEADBEEFu);
    CHECK_EQ(buf[0], 0xDE);
    CHECK_EQ(buf[3], 0xEF);
    CHECK_EQ(load_be32(buf), 0xDEADBEEFu);
}

TEST_CASE("reader/writer offsets") {
    BigEndianWriter w(12);
    w.u16(0, 1);
    w.i32(2, -42);
    w.u32(6, 0x01020304u);
    w.u8(10, 0x7F);

    BigEndianReader r(w.data());
    CHECK_EQ(r.u16(0), 1);
    CHECK_EQ(r.i32(2), -42);
    CHECK_EQ(r.u32(6), 0x01020304u);
    CHECK_EQ(r.u8(10), 0x7F);
    CHECK(r.has(0, 12));
    CHECK(!r.has(10, 4));
}

TEST_CASE("CodeWarrior MSB-first bitfields") {
    // Layout: uLong a:14; uLong b:1; uLong c:17;
    // a occupies bits 31..18, b bit 17, c bits 16..0.
    std::uint32_t word = 0;
    word = cw_set_bits(word, 0, 14, 0x2AAA);
    word = cw_set_bits(word, 14, 1, 1);
    word = cw_set_bits(word, 15, 17, 0x1F0F1);
    CHECK_EQ(cw_bits(word, 0, 14), 0x2AAAu);
    CHECK_EQ(cw_bits(word, 14, 1), 1u);
    CHECK_EQ(cw_bits(word, 15, 17), 0x1F0F1u);

    // Signed 8-bit field: -5 encodes as 0xFB.
    CHECK_EQ(sign_extend(0xFBu, 8), -5);
    CHECK_EQ(sign_extend(0x05u, 8), 5);
}

TEST_CASE("pascal strings") {
    std::uint8_t buf[8];
    string_to_pascal("hello world", buf); // truncates to 7 chars
    CHECK_EQ(buf[0], 7);
    CHECK_EQ(pascal_to_string(buf), "hello w");

    string_to_pascal("hi", buf);
    CHECK_EQ(pascal_to_string(buf), "hi");
    CHECK_EQ(buf[3], 0); // zero fill

    // Corrupt length byte gets clamped, mirroring CheckStringLen.
    std::uint8_t bad[4] = {200, 'a', 'b', 'c'};
    CHECK_EQ(pascal_to_string(bad), "abc");
}

EUTEST_MAIN
