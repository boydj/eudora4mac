#include <cstdio>

#include "mail/mime_codec.hpp"
#include "protocols/imap.hpp"
#include "scripted_transport.hpp"
#include "test_framework.hpp"

using namespace eudora;
using eutest::ScriptedTransport;

TEST_CASE("IMAP login, select, list") {
    ScriptedTransport t({
        {"", "* OK IMAP4rev1 server ready\r\n"},
        {"A0001 CAPABILITY\r\n",
         "* CAPABILITY IMAP4rev1 STARTTLS AUTH=CRAM-MD5 AUTH=PLAIN\r\n"
         "A0001 OK done\r\n"},
        // AUTHENTICATE CRAM-MD5 with the RFC 2195 vector.
        {"A0002 AUTHENTICATE CRAM-MD5\r\n",
         "+ " + base64_encode("<1896.697170952@postoffice.reston.mci.net>") +
             "\r\n"},
        {base64_encode("tim b913a602c7eda7a495b4e6e7334d3890") + "\r\n",
         "A0002 OK authenticated\r\n"},
        {"A0003 SELECT \"INBOX\"\r\n",
         "* 3 EXISTS\r\n"
         "* 1 RECENT\r\n"
         "* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)\r\n"
         "* OK (UIDVALIDITY 857529045) UIDs valid\r\n"
         "* OK (UIDNEXT 4392) Predicted next UID\r\n"
         "* OK (UNSEEN 2) Message 2 is first unseen\r\n"
         "A0003 OK (READ-WRITE) SELECT completed\r\n"},
        {"A0004 LIST \"\" \"*\"\r\n",
         "* LIST (\\HasNoChildren) \"/\" \"INBOX\"\r\n"
         "* LIST (\\Noselect \\HasChildren) \"/\" \"Archive\"\r\n"
         "* LIST (\\HasNoChildren) \"/\" {13}\r\nArchive/2003a\r\n"
         "A0004 OK LIST completed\r\n"},
    });

    ImapSession imap(t);
    CHECK(imap.connect("imap.example.com", 143));
    CHECK(!imap.preauth());
    CHECK(imap.has_capability("IMAP4rev1"));
    CHECK(imap.has_capability("starttls"));

    CHECK(imap.login("tim", "tanstaaftanstaaf"));

    ImapMailboxInfo info;
    CHECK(imap.select("INBOX", info));
    CHECK_EQ(info.exists, 3);
    CHECK_EQ(info.recent, 1);
    CHECK_EQ(info.unseen, 2);
    CHECK_EQ(info.uid_validity, 857529045u);
    CHECK_EQ(info.uid_next, 4392u);
    CHECK(!info.read_only);
    CHECK_EQ(info.flags.size(), 5u);

    auto listing = imap.list("", "*");
    CHECK(listing.has_value());
    if (listing) {
        CHECK_EQ(listing->size(), 3u);
        CHECK_EQ((*listing)[0].name, "INBOX");
        CHECK_EQ((*listing)[1].attributes[0], "\\Noselect");
        // Literal mailbox name spliced back in.
        CHECK_EQ((*listing)[2].name, "Archive/2003a");
    }
    for (const auto &f : t.failures())
        std::fprintf(stderr, "  %s\n", f.c_str());
    CHECK(t.failures().empty());
}

TEST_CASE("IMAP fetch with literals, store, search, append, expunge") {
    const std::string message =
        "From: a@b.c\r\nSubject: via imap\r\n\r\nbody here\r\n";

    ScriptedTransport t({
        {"", "* PREAUTH welcome\r\n"},
        {"A0001 UID FETCH 1:* (UID FLAGS RFC822.SIZE)\r\n",
         "* 1 FETCH (UID 101 FLAGS (\\Seen) RFC822.SIZE 48)\r\n"
         "* 2 FETCH (UID 102 FLAGS () RFC822.SIZE 99)\r\n"
         "A0001 OK FETCH completed\r\n"},
        {"A0002 UID FETCH 102 (UID BODY.PEEK[])\r\n",
         "* 2 FETCH (UID 102 BODY[] {" + std::to_string(message.size()) +
             "}\r\n" + message + ")\r\n"
             "A0002 OK FETCH completed\r\n"},
        {"A0003 UID STORE 102 +FLAGS (\\Seen)\r\n",
         "* 2 FETCH (UID 102 FLAGS (\\Seen))\r\n"
         "A0003 OK STORE completed\r\n"},
        {"A0004 UID SEARCH UNSEEN\r\n",
         "* SEARCH 103 104\r\n"
         "A0004 OK SEARCH completed\r\n"},
        {"A0005 APPEND \"Sent\" (\\Seen) {" + std::to_string(message.size()) +
             "}\r\n",
         "+ Ready for literal data\r\n"},
        {message + "\r\n", "A0005 OK APPEND completed\r\n"},
        {"A0006 EXPUNGE\r\n", "* 1 EXPUNGE\r\nA0006 OK EXPUNGE completed\r\n"},
        {"A0007 LOGOUT\r\n", "* BYE\r\nA0007 OK bye\r\n"},
    });

    ImapSession imap(t);
    CHECK(imap.connect("imap.example.com", 143));
    CHECK(imap.preauth());
    CHECK(imap.login("ignored", "ignored")); // PREAUTH: no-op

    auto flags = imap.uid_fetch("1:*", "(UID FLAGS RFC822.SIZE)");
    CHECK(flags.has_value());
    if (flags) {
        CHECK_EQ(flags->size(), 2u);
        CHECK_EQ((*flags)[0].uid, 101u);
        CHECK_EQ((*flags)[0].rfc822_size, 48);
        CHECK_EQ(imap_flags_to_state((*flags)[0].flags), 2); // Read
        CHECK_EQ(imap_flags_to_state((*flags)[1].flags), 1); // Unread
    }

    auto body = imap.uid_fetch("102", "(UID BODY.PEEK[])");
    CHECK(body.has_value());
    if (body && !body->empty()) {
        CHECK_EQ((*body)[0].uid, 102u);
        CHECK_EQ((*body)[0].body, message); // literal round-tripped exactly
    }

    CHECK(imap.uid_store("102", "+FLAGS", "\\Seen"));

    auto unseen = imap.uid_search("UNSEEN");
    CHECK(unseen.has_value());
    if (unseen) {
        CHECK_EQ(unseen->size(), 2u);
        CHECK_EQ((*unseen)[0], 103u);
    }

    CHECK(imap.append("Sent", "\\Seen", message));
    CHECK(imap.expunge());
    CHECK(imap.logout());

    for (const auto &f : t.failures())
        std::fprintf(stderr, "  %s\n", f.c_str());
    CHECK(t.failures().empty());
    CHECK(t.all_consumed());
}

TEST_CASE("IMAP NO/BAD handling and quoting") {
    ScriptedTransport t({
        {"", "* OK ready\r\n"},
        {"A0001 CAPABILITY\r\n", "* CAPABILITY IMAP4rev1\r\nA0001 OK\r\n"},
        {"A0002 LOGIN \"user\" \"pa\\\"ss\"\r\n", "A0002 NO [AUTHENTICATIONFAILED] nope\r\n"},
    });
    ImapSession imap(t);
    CHECK(imap.connect("imap.example.com", 143));
    CHECK(!imap.login("user", "pa\"ss"));
    CHECK(imap.last_response().find("NO") == 0);
}

EUTEST_MAIN
