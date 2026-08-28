#include "addressbook/nicknames.hpp"
#include "eudora/eudora_core.h"
#include "test_framework.hpp"

#include <cstring>

using namespace eudora;

namespace {

const std::string kBook =
    "alias alice alice@example.com\r"
    "alias \"the team\" alice, bob@example.org, carol@example.net\r"
    "note \"the team\" <fax:555-1234>weekly sync group\r"
    "alias wrapped one@x.y, \\\r"
    "two@x.y\r"
    "# a comment line to ignore\r"
    "junk that is not a command\r";

} // namespace

TEST_CASE("address book parses aliases, quotes, continuations") {
    const AddressBook book = AddressBook::parse(kBook);
    CHECK_EQ(book.nicknames().size(), 3u);

    const Nickname *alice = book.find("ALICE"); // case-insensitive
    CHECK(alice != nullptr);
    if (alice) {
        CHECK_EQ(alice->addresses, "alice@example.com");
        CHECK(!alice->group);
    }

    const Nickname *team = book.find("the team");
    CHECK(team != nullptr);
    if (team) {
        CHECK(team->group);
        CHECK_EQ(team->notes, "<fax:555-1234>weekly sync group");
        auto fax = AddressBook::note_field(*team, "fax");
        CHECK(fax.has_value());
        if (fax)
            CHECK_EQ(*fax, "555-1234");
    }

    // Escaped-newline continuation joined the two physical lines.
    const Nickname *wrapped = book.find("wrapped");
    CHECK(wrapped != nullptr);
    if (wrapped)
        CHECK_EQ(wrapped->addresses, "one@x.y, two@x.y");
}

TEST_CASE("address book expansion is recursive with cycle protection") {
    AddressBook book;
    book.set({"inner", "deep@example.com", "", false});
    book.set({"outer", "inner, direct@example.com", "", true});
    book.set({"loopy", "loopy, safe@example.com", "", true});

    const auto expanded = book.expand("outer, extra@x.y");
    CHECK_EQ(expanded.size(), 3u);
    if (expanded.size() == 3) {
        CHECK_EQ(expanded[0], "deep@example.com");
        CHECK_EQ(expanded[1], "direct@example.com");
        CHECK_EQ(expanded[2], "extra@x.y");
    }

    // Self-reference terminates and keeps the literal token.
    const auto loop = book.expand("loopy");
    CHECK_EQ(loop.size(), 2u);
    if (loop.size() == 2) {
        CHECK_EQ(loop[0], "loopy");
        CHECK_EQ(loop[1], "safe@example.com");
    }
}

TEST_CASE("address book round trip and membership") {
    const AddressBook book = AddressBook::parse(kBook);
    const std::string text = book.serialize();
    const AddressBook again = AddressBook::parse(text);
    CHECK_EQ(again.nicknames().size(), book.nicknames().size());
    CHECK(again.find("the team") != nullptr);

    CHECK(book.contains_address("bob@example.org"));
    CHECK(book.contains_address("Bob Person <bob@example.org>")); // short form
    CHECK(!book.contains_address("stranger@nowhere.example"));
}

TEST_CASE("C API address book + filters intersectsFile") {
    eudora_addressbook *ab = eudora_addressbook_parse(kBook.c_str());
    CHECK(ab != nullptr);
    if (!ab)
        return;
    CHECK_EQ(eudora_addressbook_count(ab), 3);
    CHECK(eudora_addressbook_contains(ab, "carol@example.net"));

    // Multi-word nicknames are quoted in recipient lists, as in real Eudora
    // (the address parser squeezes spaces from unquoted tokens).
    char **exp = eudora_addressbook_expand(ab, "\"the team\"");
    CHECK(exp != nullptr);
    if (exp) {
        int n = 0;
        while (exp[n])
            ++n;
        CHECK_EQ(n, 3);
        eudora_addresses_free(exp);
    }

    CHECK(eudora_addressbook_set(ab, "dave", "dave@example.io", nullptr));
    CHECK_EQ(eudora_addressbook_count(ab), 4);
    CHECK(eudora_addressbook_remove(ab, "dave"));

    // Filter with intersectsFile fires only with the book attached.
    const std::string filters =
        "rule Friends\r"
        "incoming\r"
        "header from:\r"
        "verb intersectsFile\r"
        "value \r"
        "label 2\r";
    const std::string msg =
        "From alice@example.com Wed Jun 14 12:36:18 1989\r"
        "From: Alice <alice@example.com>\r"
        "Subject: hi\r"
        "\r"
        "hello\r";

    eudora_filters *f = eudora_filters_parse(filters.c_str());
    CHECK(f != nullptr);
    if (f) {
        int32_t count = -1;
        eudora_fired_action *fired = eudora_filters_run(
            f, EUDORA_FILTER_INCOMING, msg.data(), msg.size(), &count);
        CHECK_EQ(count, 0); // no book: verb can't match
        eudora_fired_actions_free(fired, count);

        fired = eudora_filters_run_with_book(f, EUDORA_FILTER_INCOMING,
                                             msg.data(), msg.size(), ab, &count);
        CHECK_EQ(count, 1);
        if (fired && count == 1)
            CHECK_EQ(std::string(fired[0].keyword), "label");
        eudora_fired_actions_free(fired, count);
        eudora_filters_free(f);
    }
    eudora_addressbook_free(ab);
}

EUTEST_MAIN
