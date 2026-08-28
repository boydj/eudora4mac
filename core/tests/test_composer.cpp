#include <cstring>
#include <filesystem>
#include <fstream>

#include "eudora/eudora_core.h"
#include "mail/composer.hpp"
#include "mail/header_parser.hpp"
#include "mail/mime_codec.hpp"
#include "mail/rfc2047.hpp"
#include "test_framework.hpp"

using namespace eudora;
namespace fs = std::filesystem;

TEST_CASE("rfc2047 encoding") {
    CHECK_EQ(encode_rfc2047("plain ascii"), "plain ascii");
    const std::string enc = encode_rfc2047("café");
    CHECK(enc.rfind("=?utf-8?B?", 0) == 0);
    CHECK_EQ(decode_rfc2047(enc), "café");

    // Long non-ASCII subject splits into multiple words that round trip.
    std::string many;
    for (int i = 0; i < 30; ++i)
        many += "héllo ";
    CHECK_EQ(decode_rfc2047(encode_rfc2047(many)), many);
}

TEST_CASE("rfc822 date formatting") {
    // 1989-06-14 17:36:18 UTC at -0500 = 12:36:18 local.
    CHECK_EQ(rfc822_date(613848978, -5 * 3600),
             "Wed, 14 Jun 1989 12:36:18 -0500");
    CHECK_EQ(rfc822_date(0, 0), "Thu, 1 Jan 1970 00:00:00 +0000");
}

TEST_CASE("composer builds a simple message") {
    MessageComposer c;
    c.from("Alice Wonder", "alice@example.com")
        .to("Bob <bob@example.org>, carol@example.net")
        .bcc("secret@example.io")
        .subject("lunch café")
        .body("Line one\nLine two\n")
        .priority(1);

    auto built = c.build();
    CHECK(built.has_value());
    if (!built)
        return;

    const auto parts = split_message(*built);
    const HeaderSet hs = HeaderSet::parse(parts.header_block);
    CHECK_EQ(hs.get_decoded("Subject"), "lunch café");
    CHECK_EQ(hs.get_decoded("From"), "Alice Wonder <alice@example.com>");
    CHECK(hs.get("Bcc") == std::nullopt); // never in headers
    CHECK(hs.get("Date").has_value());
    CHECK(hs.get("Message-Id").has_value());
    CHECK(std::string(*hs.get("X-Priority")) == "1");
    CHECK_EQ(hs.content_type(), "text");
    CHECK(hs.transfer_encoding() == TransferEncoding::QuotedPrintable ||
          hs.transfer_encoding() == TransferEncoding::SevenBit);

    // Envelope covers bcc.
    const auto rcpts = c.recipients();
    CHECK_EQ(rcpts.size(), 3u);
    CHECK_EQ(rcpts[2], "secret@example.io");
    CHECK_EQ(c.sender(), "alice@example.com");
}

TEST_CASE("composer builds multipart with attachment that round trips") {
    const fs::path dir = fs::temp_directory_path() / "eudora_composer_tests";
    fs::create_directories(dir);
    const fs::path att = dir / "data.bin";
    std::string payload;
    for (int i = 0; i < 300; ++i)
        payload += static_cast<char>(i & 0xFF);
    {
        std::ofstream f(att, std::ios::binary);
        f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    MessageComposer c;
    c.from("", "a@b.c").to("d@e.f").subject("with attachment").body("see file\n");
    c.attach({att, "", ""});
    auto built = c.build();
    CHECK(built.has_value());
    if (!built)
        return;

    const auto parts = split_message(*built);
    const HeaderSet hs = HeaderSet::parse(parts.header_block);
    CHECK_EQ(hs.content_type(), "multipart");
    CHECK_EQ(hs.content_subtype(), "mixed");
    const std::string boundary = hs.boundary();
    CHECK(!boundary.empty());

    // Locate the attachment part and decode it.
    const std::string marker = "--" + boundary;
    const auto p1 = built->find(marker);
    const auto p2 = built->find(marker, p1 + marker.size());
    CHECK(p2 != std::string::npos);
    const auto part_end = built->find(marker, p2 + marker.size());
    CHECK(part_end != std::string::npos);
    const std::string part =
        built->substr(p2 + marker.size() + 2, part_end - p2 - marker.size() - 2);
    const auto att_parts = split_message(part);
    const HeaderSet ah = HeaderSet::parse(att_parts.header_block);
    CHECK_EQ(ah.filename(), "data.bin");
    CHECK_EQ(ah.content_type(), "application");
    CHECK(ah.transfer_encoding() == TransferEncoding::Base64);
    std::string decoded;
    CHECK(base64_decode(att_parts.body, decoded));
    CHECK_EQ(decoded, payload);
}

TEST_CASE("C API composer") {
    eudora_composer *c = eudora_composer_new();
    CHECK(c != nullptr);
    if (!c)
        return;
    eudora_composer_from(c, "Alice", "alice@example.com");
    eudora_composer_to(c, "bob@example.org");
    eudora_composer_cc(c, "carol@example.net");
    eudora_composer_subject(c, "hello");
    eudora_composer_body(c, "world\n");
    eudora_composer_header(c, "X-Mailer", "EudoraCore 0.1");

    char *msg = eudora_composer_build(c);
    CHECK(msg != nullptr);
    if (msg) {
        CHECK(std::strstr(msg, "Subject: hello\r\n") != nullptr);
        CHECK(std::strstr(msg, "X-Mailer: EudoraCore 0.1\r\n") != nullptr);
        eudora_string_free(msg);
    }
    char *rcpts = eudora_composer_recipients(c);
    CHECK(rcpts != nullptr);
    if (rcpts) {
        CHECK_EQ(std::string(rcpts), "bob@example.org, carol@example.net");
        eudora_string_free(rcpts);
    }
    char *sender = eudora_composer_sender(c);
    CHECK(sender && std::string(sender) == "alice@example.com");
    eudora_string_free(sender);
    eudora_composer_free(c);
}

EUTEST_MAIN
