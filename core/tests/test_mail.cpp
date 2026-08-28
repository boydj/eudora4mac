#include "compat/hashes.hpp"
#include "mail/address_parser.hpp"
#include "mail/header_parser.hpp"
#include "mail/lex822.hpp"
#include "mail/mime_codec.hpp"
#include "mail/rfc2047.hpp"
#include "test_framework.hpp"

using namespace eudora;

TEST_CASE("base64 round trip") {
    const std::string data = "Man is distinguished, not only by his reason.";
    const std::string enc = base64_encode(data);
    std::string dec;
    CHECK(base64_decode(enc, dec));
    CHECK_EQ(dec, data);

    // RFC 4648 vectors.
    CHECK_EQ(base64_encode(""), "");
    CHECK_EQ(base64_encode("f"), "Zg==");
    CHECK_EQ(base64_encode("fo"), "Zm8=");
    CHECK_EQ(base64_encode("foo"), "Zm9v");
    CHECK_EQ(base64_encode("foob"), "Zm9vYg==");
    CHECK_EQ(base64_encode("fooba"), "Zm9vYmE=");
    CHECK_EQ(base64_encode("foobar"), "Zm9vYmFy");

    std::string out;
    CHECK(base64_decode("Zm9vYmE=", out));
    CHECK_EQ(out, "fooba");
    CHECK(base64_decode("Zm9v YmE=", out)); // whitespace skipped
    CHECK_EQ(out, "fooba");
    CHECK(!base64_decode("Zm9v!YmE=", out)); // invalid char counted

    // Binary safety.
    std::string bin;
    for (int i = 0; i < 256; ++i)
        bin += static_cast<char>(i);
    CHECK(base64_decode(base64_encode(bin), out));
    CHECK_EQ(out, bin);

    // Line wrapping at 68 chars.
    const std::string wrapped = base64_encode(std::string(100, 'x'), "\r");
    CHECK(wrapped.find('\r') != std::string::npos);
    std::string unwrapped;
    CHECK(base64_decode(wrapped, unwrapped));
    CHECK_EQ(unwrapped, std::string(100, 'x'));
}

TEST_CASE("base64 text mode normalizes newlines") {
    // "a\r\nb" encoded, decoded in text mode -> "a\rb" (CRLF -> CR).
    std::string dec;
    CHECK(base64_decode(base64_encode("a\r\nb"), dec, true));
    CHECK_EQ(dec, "a\rb");
    CHECK(base64_decode(base64_encode("a\nb"), dec, true));
    CHECK_EQ(dec, "a\rb"); // LF -> CR
}

TEST_CASE("quoted-printable round trip") {
    std::string out;
    CHECK(qp_decode("Caf=E9 =3D fun", out));
    CHECK_EQ(out, "Caf\xE9 = fun");

    // Soft line breaks, CR and CRLF flavors.
    CHECK(qp_decode("foo=\rbar", out));
    CHECK_EQ(out, "foobar");
    CHECK(qp_decode("foo=\r\nbar", out));
    CHECK_EQ(out, "foobar");

    // Encoder: '=' and control chars escaped, "From " protected.
    const std::string enc = qp_encode("x = y\r");
    CHECK_EQ(enc, "x =3D y\r");
    const std::string enc2 = qp_encode("From here\r");
    CHECK_EQ(enc2.substr(0, 3), "=46"); // 'F' at line start encoded
    CHECK(qp_decode(enc2, out));
    CHECK_EQ(out, "From here\r");

    // Long lines get soft-wrapped under 76 columns.
    const std::string long_line = std::string(200, 'a') + "\r";
    const std::string enc3 = qp_encode(long_line);
    CHECK(enc3.find("=\r") != std::string::npos);
    CHECK(qp_decode(enc3, out));
    CHECK_EQ(out, long_line);
}

TEST_CASE("rfc2047 decoding") {
    CHECK_EQ(decode_rfc2047("=?ISO-8859-1?Q?Andr=E9?= Pirard"), "André Pirard");
    CHECK_EQ(decode_rfc2047("=?ISO-8859-1?B?SWYgeW91IGNhbiByZWFkIHRoaXM=?="),
             "If you can read this");
    // Adjacent encoded words: intervening whitespace removed.
    CHECK_EQ(decode_rfc2047("=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?="), "ab");
    // Unknown charset left intact.
    CHECK_EQ(decode_rfc2047("=?x-klingon?Q?ab?="), "=?x-klingon?Q?ab?=");
    // Underscore means space in Q encoding.
    CHECK_EQ(decode_rfc2047("=?utf-8?Q?a_b?="), "a b");
    // Plain text untouched.
    CHECK_EQ(decode_rfc2047("just plain text"), "just plain text");
    // MacRoman charset converts.
    CHECK_EQ(decode_rfc2047("=?macintosh?Q?caf=8E?="), "café");
}

TEST_CASE("address parser") {
    auto a = parse_addresses("Alice Wonder <alice@example.com>, bob@example.org");
    CHECK(a.has_value());
    if (a) {
        CHECK_EQ(a->size(), 2u);
        CHECK_EQ((*a)[0], "alice@example.com");
        CHECK_EQ((*a)[1], "bob@example.org");
    }

    // Comments stripped; quoted commas protected.
    auto b = parse_addresses("carol@example.net (Carol), \"Last, First\" <d@e.f>");
    CHECK(b.has_value());
    if (b) {
        CHECK_EQ(b->size(), 2u);
        CHECK_EQ((*b)[0], "carol@example.net");
        CHECK_EQ((*b)[1], "d@e.f");
    }

    // Group syntax: like the original, the group name ("friends:") and the
    // terminating ";" come back as their own tokens for callers to filter.
    auto c = parse_addresses("friends: x@y.z, w@v.u;");
    CHECK(c.has_value());
    if (c) {
        CHECK_EQ(c->size(), 4u);
        CHECK_EQ((*c)[0], "friends:");
        CHECK_EQ((*c)[1], "x@y.z");
        CHECK_EQ((*c)[2], "w@v.u");
        CHECK_EQ((*c)[3], ";");
    }

    // Domain literals survive.
    auto d = parse_addresses("root@[10.0.0.1]");
    CHECK(d.has_value());
    if (d && !d->empty())
        CHECK_EQ((*d)[0], "root@[10.0.0.1]");

    // Unbalanced angle bracket is an error.
    CHECK(!parse_addresses("oops <unclosed@example.com").has_value() ||
          parse_addresses("oops <unclosed@example.com")->empty());

    CHECK_EQ(short_address("Alice <alice@example.com>"), "alice@example.com");
    CHECK(same_address("Alice <ALICE@example.com>", "alice@example.com"));
    CHECK(!same_address("a@b.c", "d@e.f"));
}

TEST_CASE("lex822 tokenizer") {
    const auto t = lex822_tokenize("text/plain; charset=\"us-ascii\" (note)");
    CHECK(t.size() >= 8);
    CHECK(t[0].kind == Token822::Atom);
    CHECK_EQ(t[0].text, "text");
    CHECK(t[1].kind == Token822::Special);
    CHECK_EQ(t[1].text, "/");
    CHECK(t[2].kind == Token822::Atom);
    CHECK_EQ(t[2].text, "plain");
    bool saw_quoted = false, saw_comment = false;
    for (const auto &tok : t) {
        if (tok.kind == Token822::QText && tok.text == "us-ascii")
            saw_quoted = true;
        if (tok.kind == Token822::Comment && tok.text == "note")
            saw_comment = true;
    }
    CHECK(saw_quoted);
    CHECK(saw_comment);

    CHECK_EQ(quote822("plain", true), "plain");
    CHECK_EQ(quote822("has space", true), "\"has space\"");
    CHECK_EQ(quote822("semi;colon", false), "\"semi;colon\"");
}

TEST_CASE("header parser") {
    const std::string msg =
        "From: =?ISO-8859-1?Q?Andr=E9?= <andre@example.fr>\r"
        "To: someone@example.com\r"
        "Subject: greetings\r"
        " and salutations\r"
        "Message-Id: <42@example.fr>\r"
        "Content-Type: multipart/Mixed; boundary=\"=====42=====\"\r"
        "Content-Transfer-Encoding: 7bit\r"
        "\r"
        "body starts here\r";

    const auto parts = split_message(msg);
    CHECK_EQ(parts.body, "body starts here\r");

    const auto hs = HeaderSet::parse(parts.header_block);
    CHECK_EQ(hs.fields().size(), 6u);
    CHECK(hs.get("subject").has_value());
    CHECK_EQ(std::string(*hs.get("Subject")), "greetings and salutations");
    CHECK_EQ(hs.get_decoded("From"), "André <andre@example.fr>");
    CHECK_EQ(hs.content_type(), "multipart");
    CHECK_EQ(hs.content_subtype(), "mixed");
    CHECK_EQ(hs.boundary(), "=====42=====");
    CHECK(hs.transfer_encoding() == TransferEncoding::SevenBit);
    CHECK_EQ(hs.message_id_hash(), kr_hash("42@example.fr"));

    // LF-terminated messages parse identically.
    std::string lf = msg;
    for (auto &ch : lf)
        if (ch == '\r')
            ch = '\n';
    const auto hs2 = HeaderSet::parse(split_message(lf).header_block);
    CHECK_EQ(hs2.fields().size(), 6u);
    CHECK_EQ(std::string(*hs2.get("Subject")), "greetings and salutations");
}

TEST_CASE("header parser attachment facts") {
    const std::string hdr =
        "Content-Type: application/octet-stream; name=\"report.pdf\"\r"
        "Content-Disposition: attachment; filename=\"report.pdf\"\r"
        "Content-Transfer-Encoding: base64\r";
    const auto hs = HeaderSet::parse(hdr);
    CHECK_EQ(hs.content_type(), "application");
    CHECK_EQ(hs.content_subtype(), "octet-stream");
    CHECK_EQ(hs.filename(), "report.pdf");
    CHECK(hs.transfer_encoding() == TransferEncoding::Base64);
}

EUTEST_MAIN
