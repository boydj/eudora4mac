// Classic attachment decoders and charset conversion.

#include <cstdint>
#include <string>

#include "compat/charset.hpp"
#include "mail/attachment_decode.hpp"
#include "mail/mime_walker.hpp"
#include "test_framework.hpp"

using namespace eudora;

namespace {

std::string be32(std::uint32_t v) {
    return {static_cast<char>((v >> 24) & 0xFF), static_cast<char>((v >> 16) & 0xFF),
            static_cast<char>((v >> 8) & 0xFF), static_cast<char>(v & 0xFF)};
}

} // namespace

TEST_CASE("charset: single-byte tables convert to UTF-8") {
    std::string out;
    // koi8-r "Привет"
    CHECK(charset_to_utf8("koi8-r",
                          std::string("\xF0\xD2\xC9\xD7\xC5\xD4"), out));
    CHECK_EQ(out, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    // iso-8859-15 0xA4 is the euro sign
    CHECK(charset_to_utf8("iso-8859-15", std::string("\xA4"), out));
    CHECK_EQ(out, "\xE2\x82\xAC");
    // windows-1251 0xC0 -> CYRILLIC А (U+0410)
    CHECK(charset_to_utf8("windows-1251", std::string("\xC0"), out));
    CHECK_EQ(out, "\xD0\x90");
    // utf-16be with a BOM
    CHECK(charset_to_utf8("utf-16", std::string("\xFE\xFF\x00\x41", 4), out));
    CHECK_EQ(out, "A");
    // A charset no table and no platform converter knows is rejected so the
    // caller keeps the bytes.  (iso-2022-jp etc. resolve on macOS through
    // the CoreFoundation fallback, so use a name nothing recognizes.)
    CHECK(!charset_to_utf8("x-not-a-real-charset-zzz", "whatever", out));
}

TEST_CASE("attachments: uuencode round-trips") {
    const std::string uutext =
        R"(begin 644 hello.txt
M2&5L;&\L(&%T=&%C:&UE;G0@=V]R;&0A"DAE;&QO+"!A='1A8VAM96YT('=O
><FQD(0I(96QL;RP@871T86-H;65N="!W;W)L9"$*

end
)";
    const auto a = decode_uuencode(uutext);
    CHECK(a.ok);
    CHECK_EQ(a.filename, "hello.txt");
    const std::string expect =
        "Hello, attachment world!\nHello, attachment world!\n"
        "Hello, attachment world!\n";
    CHECK_EQ(a.data, expect);
}

TEST_CASE("attachments: BinHex 4.0 recovers the data fork") {
    const std::string bh =
        "(This file must be converted with BinHex 4.0)\n\n"
        ":\"h\"TBbjNBA3!9%9B9'9038-!N!8b!*!'3NP15%9B4%&838**6NK&@%4\"9%&#"
        "58j)49K%394\"3NP15%9B4%&838**6NK&@%4\"9%%!!!:\n";
    const auto a = decode_binhex(bh);
    CHECK(a.ok);
    CHECK_EQ(a.filename, "pic.dat");
    CHECK_EQ(a.data, std::string("BINHEXDATA""BINHEXDATA""BINHEXDATA"
                                 "BINHEXDATA""BINHEXDATA"));
}

TEST_CASE("attachments: AppleSingle data fork extraction") {
    // magic + version + 16-byte filler + entry count + one data-fork entry.
    const std::string payload = "the data fork";
    const std::string name = "myfile";
    std::string f = be32(0x00051600); // AppleSingle
    f += be32(0x00020000);            // version
    f += std::string(16, '\0');       // filler
    f += std::string("\x00\x02", 2);  // 2 entries
    // entry table starts at offset 26; two 12-byte entries -> data at 50.
    const std::uint32_t name_off = 26 + 24;
    const std::uint32_t data_off = name_off + static_cast<std::uint32_t>(name.size());
    f += be32(3) + be32(name_off) + be32(static_cast<std::uint32_t>(name.size()));
    f += be32(1) + be32(data_off) + be32(static_cast<std::uint32_t>(payload.size()));
    f += name;
    f += payload;

    const auto a = decode_applefile(f);
    CHECK(a.ok);
    CHECK_EQ(a.filename, "myfile");
    CHECK_EQ(a.data, payload);
    // Dispatch by magic also works.
    CHECK(decode_classic_attachment(f).ok);
}

TEST_CASE("mime: a non-UTF8 text part is converted for display") {
    const std::string msg =
        "Content-Type: text/plain; charset=iso-8859-15\r\n"
        "\r\n"
        "cost: \xA4""5\r\n"; // 0xA4 is the euro sign in latin-9
    const auto parts = walk_mime(msg);
    CHECK_EQ(static_cast<int>(parts.size()), 1);
    if (parts.empty())
        return;
    CHECK_EQ(parts[0].charset, "iso-8859-15");
    const std::string text = decode_text_part(msg, parts[0]);
    CHECK(text.find("\xE2\x82\xAC") != std::string::npos); // euro in UTF-8
}

EUTEST_MAIN
