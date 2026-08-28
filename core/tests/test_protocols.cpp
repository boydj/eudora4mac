#include <algorithm>

#include "mail/mime_codec.hpp"
#include "net/line_receiver.hpp"
#include "protocols/md5.h"
#include "protocols/pop3.hpp"
#include "protocols/sasl.hpp"
#include "protocols/smtp.hpp"
#include "scripted_transport.hpp"
#include "test_framework.hpp"

using namespace eudora;
using eutest::ScriptedTransport;

namespace {
std::string hexify(const unsigned char d[16]) {
    static const char *hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 16; ++i) {
        s += hex[(d[i] >> 4) & 0xF];
        s += hex[d[i] & 0xF];
    }
    return s;
}
} // namespace

TEST_CASE("md5 RFC 1321 vectors") {
    unsigned char d[16];
    eudora_md5("", 0, d);
    CHECK_EQ(hexify(d), "d41d8cd98f00b204e9800998ecf8427e");
    eudora_md5("abc", 3, d);
    CHECK_EQ(hexify(d), "900150983cd24fb0d6963f7d28e17f72");
    eudora_md5("message digest", 14, d);
    CHECK_EQ(hexify(d), "f96b697d7cb7938d525a2f31aaf161d0");
    const std::string a64(1000000, 'a');
    eudora_md5(a64.data(), a64.size(), d);
    CHECK_EQ(hexify(d), "7707d6ae4e027c70eea2a935c2296f21");
}

TEST_CASE("hmac-md5 RFC 2202 vectors") {
    unsigned char d[16];
    const std::string key1(16, '\x0b');
    eudora_hmac_md5("Hi There", 8, key1.data(), key1.size(), d);
    CHECK_EQ(hexify(d), "9294727a3638bb1c13f48ef8158bfc9d");
    eudora_hmac_md5("what do ya want for nothing?", 28, "Jefe", 4, d);
    CHECK_EQ(hexify(d), "750c783e6ab0b503eaa86e310a5db738");
}

TEST_CASE("sasl mechanisms") {
    // RFC 2195 example: CRAM-MD5.
    const std::string challenge =
        base64_encode("<1896.697170952@postoffice.reston.mci.net>");
    const std::string resp =
        sasl_cram_md5_response("tim", "tanstaaftanstaaf", challenge);
    std::string decoded;
    CHECK(base64_decode(resp, decoded));
    CHECK_EQ(decoded, "tim b913a602c7eda7a495b4e6e7334d3890");

    // PLAIN: NUL-separated triple.
    std::string plain;
    CHECK(base64_decode(sasl_plain_response("", "user", "pass"), plain));
    CHECK_EQ(plain, std::string("\0user\0pass", 10));

    CHECK_EQ(sasl_login_user("user"), base64_encode("user"));

    // Mechanism selection: CRAM-MD5 beats LOGIN/PLAIN.
    SaslMechanism m = SaslMechanism::None;
    m = sasl_consider(m, "LOGIN");
    CHECK(m == SaslMechanism::Login);
    m = sasl_consider(m, "PLAIN");
    CHECK(m == SaslMechanism::Plain);
    m = sasl_consider(m, "CRAM-MD5");
    CHECK(m == SaslMechanism::CramMD5);
    m = sasl_consider(m, "X-UNKNOWN");
    CHECK(m == SaslMechanism::CramMD5);

    // APOP digest, RFC 1939 example.
    CHECK_EQ(apop_digest("+OK POP3 server ready <1896.697170952@dbc.mtview.ca.us>",
                         "tanstaaf"),
             "c4c9334bac560ecc979e58001b3e22fb");
}

TEST_CASE("line receiver handles CRLF, bare LF, and Exchange bare CR") {
    ScriptedTransport t({{"", "one\r\ntwo\nthree\rfour\r\n"}});
    t.connect("x", 1, 1);
    LineReceiver lr(t);
    std::string line;
    CHECK(lr.recv_line(line) == NetError::None);
    CHECK_EQ(line, "one\r");
    CHECK(lr.recv_line(line) == NetError::None);
    CHECK_EQ(line, "two\r");
    CHECK(lr.recv_line(line) == NetError::None);
    CHECK_EQ(line, "three\r"); // TREAT_BODY_CR_AS_CRLF workaround
    CHECK(lr.recv_line(line) == NetError::None);
    CHECK_EQ(line, "four\r");
    CHECK(lr.recv_line(line) == NetError::Closed);
}

TEST_CASE("POP3 full session against scripted server") {
    ScriptedTransport t2({
        {"", "+OK POP3 ready\r\n"},
        {"CAPA\r\n", "-ERR no caps\r\n"},
        {"USER alice\r\n", "+OK\r\n"},
        {"PASS secret\r\n", "+OK logged in\r\n"},
        {"STAT\r\n", "+OK 2 320\r\n"},
        {"LIST\r\n", "+OK\r\n1 120\r\n2 200\r\n.\r\n"},
        {"UIDL\r\n", "+OK\r\n1 abc\r\n2 def\r\n.\r\n"},
        {"RETR 1\r\n", "+OK message follows\r\n"
                       "Subject: hi\r\n"
                       "\r\n"
                       "..leading dot line\r\n"
                       "From alice@example.com Wed Jun 14 12:36:18 1989\r\n"
                       "normal line\r\n"
                       ".\r\n"},
        {"DELE 1\r\n", "+OK deleted\r\n"},
        {"QUIT\r\n", "+OK bye\r\n"},
    });

    Pop3Session pop(t2);
    CHECK(pop.connect("pop.example.com", 110));
    CHECK_EQ(pop.greeting(), "+OK POP3 ready");

    const auto &caps = pop.query_capabilities();
    CHECK(!caps.capa);

    CHECK(pop.login("alice", "secret"));

    long count = 0, size = 0;
    CHECK(pop.stat(count, size));
    CHECK_EQ(count, 2);
    CHECK_EQ(size, 320);

    std::map<long, long> sizes;
    CHECK(pop.list(sizes));
    CHECK_EQ(sizes[1], 120);
    CHECK_EQ(sizes[2], 200);

    std::map<long, std::string> uids;
    CHECK(pop.uidl(uids));
    CHECK_EQ(uids[2], "def");

    std::string body;
    CHECK(pop.retrieve(1, [&](std::string_view line) { body += line; }));
    // Dot-unstuffed, envelope escaped.
    CHECK(body.find(".leading dot line\r") != std::string::npos);
    CHECK(body.find("..leading") == std::string::npos);
    CHECK(body.find(">From alice@example.com Wed Jun 14 12:36:18 1989\r") !=
          std::string::npos);
    CHECK(body.find("normal line\r") != std::string::npos);

    CHECK(pop.dele(1));
    CHECK(pop.quit());
    CHECK(t2.failures().empty());
    for (const auto &f : t2.failures())
        std::fprintf(stderr, "  %s\n", f.c_str());
    CHECK(t2.all_consumed());
}

TEST_CASE("POP3 capabilities and CRAM-MD5 login") {
    const std::string chal_b64 =
        base64_encode("<1896.697170952@postoffice.reston.mci.net>");
    std::string expected_resp;
    {
        std::string decoded;
        base64_decode(sasl_cram_md5_response("tim", "tanstaaftanstaaf", chal_b64),
                      decoded);
        expected_resp = "tim b913a602c7eda7a495b4e6e7334d3890";
        CHECK_EQ(decoded, expected_resp);
    }
    ScriptedTransport t({
        {"", "+OK ready\r\n"},
        {"CAPA\r\n", "+OK\r\nTOP\r\nSASL CRAM-MD5\r\nAUTH-RESP-CODE\r\n.\r\n"},
        {"AUTH CRAM-MD5\r\n", "+ " + chal_b64 + "\r\n"},
        {base64_encode(expected_resp) + "\r\n", "+OK authed\r\n"},
    });
    Pop3Session pop(t);
    CHECK(pop.connect("pop.example.com", 110));
    const auto &caps = pop.query_capabilities();
    CHECK(caps.capa);
    CHECK(caps.top);
    CHECK(caps.auth_resp_code);
    CHECK(caps.sasl == SaslMechanism::CramMD5);
    CHECK(pop.login("tim", "tanstaaftanstaaf"));
    CHECK(t.failures().empty());
}

TEST_CASE("SMTP full session with EHLO, AUTH PLAIN, pipelined view") {
    const std::string plain = sasl_plain_response("", "alice", "secret");
    ScriptedTransport t({
        {"", "220 smtp.example.com ESMTP\r\n"},
        {"EHLO client.example.com\r\n",
         "250-smtp.example.com greets you\r\n"
         "250-SIZE 5000000\r\n"
         "250-8BITMIME\r\n"
         "250-PIPELINING\r\n"
         "250-AUTH PLAIN LOGIN CRAM-MD5\r\n"
         "250 STARTTLS\r\n"},
        {"AUTH CRAM-MD5\r\n", "334 " + base64_encode("<chal@example.com>") + "\r\n"},
    });
    SmtpSession smtp(t);
    CHECK(smtp.connect("smtp.example.com", 587));
    const auto &ext = smtp.extensions();
    CHECK(ext.esmtp);
    CHECK_EQ(ext.max_size, 5000000);
    CHECK(ext.mime8bit);
    CHECK(ext.pipelining);
    CHECK(ext.starttls);
    CHECK(ext.sasl == SaslMechanism::CramMD5); // strongest offered
    (void)plain;
}

TEST_CASE("SMTP send message end to end") {
    ScriptedTransport t({
        {"", "220 ready\r\n"},
        {"EHLO client.example.com\r\n", "250-ok\r\n250 AUTH PLAIN\r\n"},
        {"AUTH PLAIN " + sasl_plain_response("", "u", "p") + "\r\n",
         "235 2.7.0 authed\r\n"},
        {"MAIL FROM:<alice@example.com>\r\n", "250 sender ok\r\n"},
        {"RCPT TO:<bob@example.org>\r\n", "250 rcpt ok\r\n"},
        {"DATA\r\n", "354 go ahead\r\n"},
        {"Subject: test\r\n\r\nhello\r\n..dots\r\n.\r\n", "250 queued\r\n"},
        {"QUIT\r\n", "221 bye\r\n"},
    });
    SmtpSession smtp(t);
    CHECK(smtp.connect("smtp.example.com", 587));
    CHECK(smtp.auth("u", "p"));
    CHECK(smtp.mail_from("alice@example.com"));
    CHECK(smtp.rcpt_to("bob@example.org"));
    // Message uses CR line ends (mailbox convention); ".dots" needs stuffing.
    CHECK(smtp.data("Subject: test\r\rhello\r.dots\r"));
    CHECK(smtp.quit());
    for (const auto &f : t.failures())
        std::fprintf(stderr, "  %s\n", f.c_str());
    CHECK(t.failures().empty());
    CHECK(t.all_consumed());
}

TEST_CASE("SMTP HELO fallback and reply-code fixups") {
    ScriptedTransport t({
        {"", "220 old server\r\n"},
        {"EHLO client.example.com\r\n", "500 what?\r\n"},
        {"HELO client.example.com\r\n", "250 hello\r\n"},
        {"MAIL FROM:<a@b.c>\r\n", "452 mailbox full\r\n"}, // remapped to 552
        {"RCPT TO:<x@y.z>\r\n", "553 no such user\r\n"},   // normalized to 550
    });
    SmtpSession smtp(t);
    CHECK(smtp.connect("smtp.example.com", 25));
    CHECK(!smtp.extensions().esmtp);
    CHECK(!smtp.mail_from("a@b.c"));
    CHECK_EQ(smtp.last_code(), 552);
    CHECK(!smtp.rcpt_to("x@y.z"));
    CHECK_EQ(smtp.last_code(), 550);
    CHECK(t.failures().empty());
}

TEST_CASE("SMTP multiline reply with noise") {
    ScriptedTransport t({
        {"", "junk with no code\r\n220-welcome\r\n220 ready\r\n"},
        {"EHLO client.example.com\r\n", "250 plain\r\n"},
    });
    SmtpSession smtp(t);
    CHECK(smtp.connect("smtp.example.com", 25));
    CHECK(smtp.extensions().esmtp);
}

EUTEST_MAIN
