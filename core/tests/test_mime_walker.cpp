// Tests for the MIME part walker (mail/mime_walker).

#include <cstddef>
#include <string>

#include "mail/mime_codec.hpp"
#include "mail/mime_walker.hpp"
#include "test_framework.hpp"

using namespace eudora;

namespace {

// Rewrite CRLF terminators to bare CR (the mailbox storage convention).
std::string crlf_to_cr(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') {
            out += '\r';
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

const std::string kBinary("\x00\x01\x02\xff\x10"
                          "ABC",
                          8);

std::string mixed_fixture() {
    return "From: a@example.com\r\n"
           "To: b@example.com\r\n"
           "Subject: mixed\r\n"
           "MIME-Version: 1.0\r\n"
           "Content-Type: multipart/mixed; boundary=\"outer42\"\r\n"
           "\r\n"
           "preamble to be ignored\r\n"
           "--outer42\r\n"
           "Content-Type: text/plain; charset=us-ascii\r\n"
           "\r\n"
           "hello body\r\n"
           "--outer42\r\n"
           "Content-Type: application/octet-stream; name=\"blob.bin\"\r\n"
           "Content-Transfer-Encoding: base64\r\n"
           "Content-Disposition: attachment; filename=\"blob.bin\"\r\n"
           "\r\n" +
           base64_encode(kBinary) +
           "\r\n"
           "--outer42--\r\n"
           "epilogue\r\n";
}

void check_mixed(const std::string &msg) {
    const auto parts = walk_mime(msg);
    CHECK_EQ(static_cast<int>(parts.size()), 2);
    if (parts.size() != 2)
        return;
    CHECK_EQ(parts[0].type, "text");
    CHECK_EQ(parts[0].subtype, "plain");
    CHECK(!parts[0].is_attachment);
    CHECK_EQ(parts[0].depth, 1);
    CHECK_EQ(parts[1].type, "application");
    CHECK_EQ(parts[1].subtype, "octet-stream");
    CHECK_EQ(parts[1].filename, "blob.bin");
    CHECK(parts[1].is_attachment);

    CHECK(decode_part(msg, parts[0]).find("hello body") != std::string::npos);
    CHECK(decode_part(msg, parts[1]) == kBinary); // exact binary round-trip
}

} // namespace

TEST_CASE("mime walker: multipart/mixed with CRLF line ends") {
    check_mixed(mixed_fixture());
}

TEST_CASE("mime walker: multipart/mixed with CR line ends (mailbox)") {
    check_mixed(crlf_to_cr(mixed_fixture()));
}

TEST_CASE("mime walker: nested multipart/alternative inside mixed") {
    const std::string msg =
        "Content-Type: multipart/mixed; boundary=\"out\"\r\n"
        "\r\n"
        "--out\r\n"
        "Content-Type: multipart/alternative; boundary=\"in\"\r\n"
        "\r\n"
        "--in\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "plain version\r\n"
        "--in\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<b>html</b>\r\n"
        "--in--\r\n"
        "--out\r\n"
        "Content-Type: image/png; name=\"p.png\"\r\n"
        "Content-Transfer-Encoding: base64\r\n"
        "\r\n"
        "iVBORw0KGgo=\r\n"
        "--out--\r\n";
    const auto parts = walk_mime(msg);
    CHECK_EQ(static_cast<int>(parts.size()), 3);
    if (parts.size() != 3)
        return;
    CHECK_EQ(parts[0].subtype, "plain");
    CHECK_EQ(parts[0].depth, 2);
    CHECK_EQ(parts[1].subtype, "html");
    CHECK(!parts[1].is_attachment);
    CHECK_EQ(parts[2].type, "image");
    CHECK_EQ(parts[2].filename, "p.png");
    CHECK(parts[2].is_attachment);
    CHECK(decode_part(msg, parts[0]).find("plain version") != std::string::npos);
}

TEST_CASE("mime walker: message/rfc822 recurses one level") {
    const std::string msg =
        "Content-Type: multipart/mixed; boundary=\"bb\"\r\n"
        "\r\n"
        "--bb\r\n"
        "Content-Type: message/rfc822\r\n"
        "\r\n"
        "From: inner@example.com\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "the enclosed message\r\n"
        "--bb--\r\n";
    const auto parts = walk_mime(msg);
    CHECK_EQ(static_cast<int>(parts.size()), 1);
    if (parts.empty())
        return;
    CHECK_EQ(parts[0].type, "text");
    CHECK_EQ(parts[0].depth, 2);
    CHECK(decode_part(msg, parts[0]).find("the enclosed") != std::string::npos);
}

TEST_CASE("mime walker: quoted-printable part decodes") {
    const std::string msg =
        "Content-Type: text/plain; charset=iso-8859-1\r\n"
        "Content-Transfer-Encoding: quoted-printable\r\n"
        "\r\n"
        "caf=E9 break=\r\nhere\r\n";
    const auto parts = walk_mime(msg);
    CHECK_EQ(static_cast<int>(parts.size()), 1);
    if (parts.empty())
        return;
    const std::string text = decode_part(msg, parts[0]);
    CHECK(text.find("caf\xE9") != std::string::npos);
    CHECK(text.find("breakhere") != std::string::npos); // soft break joins
}

TEST_CASE("mime walker: multipart without a matching boundary degrades") {
    const std::string msg =
        "Content-Type: multipart/mixed; boundary=\"nope\"\r\n"
        "\r\n"
        "no delimiters at all\r\n";
    const auto parts = walk_mime(msg);
    CHECK_EQ(static_cast<int>(parts.size()), 1);
    if (parts.empty())
        return;
    CHECK_EQ(parts[0].type, "multipart");
    CHECK_EQ(parts[0].depth, 0);
}

TEST_CASE("mime walker: plain message is one text part") {
    const std::string msg =
        "From: x@example.com\rSubject: hi\r\rjust text\r";
    const auto parts = walk_mime(msg);
    CHECK_EQ(static_cast<int>(parts.size()), 1);
    if (parts.empty())
        return;
    CHECK_EQ(parts[0].type, "text");
    CHECK_EQ(parts[0].subtype, "plain");
    CHECK(!parts[0].is_attachment);
    CHECK(decode_part(msg, parts[0]).find("just text") != std::string::npos);
}

EUTEST_MAIN
