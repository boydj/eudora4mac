// End-to-end exercise of the C bridging interface, as a Swift frontend
// would drive it.

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "eudora/eudora_core.h"
#include "test_framework.hpp"

namespace fs = std::filesystem;

namespace {

fs::path temp_dir() {
    const fs::path d = fs::temp_directory_path() / "eudora_capi_tests";
    fs::create_directories(d);
    return d;
}

void write_file(const fs::path &p, const std::string &content) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

const std::string kMailbox =
    "From alice@example.com Wed Jun 14 12:36:18 1989\r"
    "Date: Wed, 14 Jun 1989 12:36:18 -0500\r"
    "From: Alice Wonder <alice@example.com>\r"
    "Subject: =?ISO-8859-1?Q?caf=E9?= plans\r"
    "Content-Type: text/plain; charset=\"us-ascii\"\r"
    "Message-Id: <99.zz@example.com>\r"
    "\r"
    "meet at nine\r";

} // namespace

TEST_CASE("C API: mailbox open, summaries, read, delete, compact") {
    const fs::path box = temp_dir() / "CBox";
    write_file(box, kMailbox + kMailbox); // two copies = two messages? no —
    // two concatenated messages, each starting with the From line.

    eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (!mb)
        return;
    CHECK_EQ(eudora_mailbox_count(mb), 2);

    eudora_summary sum{};
    CHECK(eudora_mailbox_summary(mb, 0, &sum));
    CHECK_EQ(std::string(sum.from), "Alice Wonder");
    CHECK_EQ(std::string(sum.subject), "café plans");
    CHECK_EQ(sum.orig_zone_minutes, -300);
    CHECK(sum.state == EUDORA_STATE_UNREAD);
    // 1989-06-14 17:36:18 UTC as Unix time.
    CHECK_EQ(sum.date_unix, 613848978);

    char *raw = eudora_mailbox_read_message(mb, 1);
    CHECK(raw != nullptr);
    if (raw) {
        CHECK(std::strstr(raw, "meet at nine") != nullptr);
        eudora_string_free(raw);
    }

    CHECK(eudora_mailbox_set_state(mb, 0, EUDORA_STATE_READ));
    CHECK(eudora_mailbox_save(mb));

    // Reopen: state survived via the TOC.
    eudora_mailbox_close(mb);
    mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (!mb)
        return;
    CHECK(eudora_mailbox_summary(mb, 0, &sum));
    CHECK(sum.state == EUDORA_STATE_READ);

    // Delete message 0 and compact; message 1 survives at offset 0.
    CHECK(eudora_mailbox_delete(mb, 0));
    CHECK(eudora_mailbox_compact(mb));
    CHECK_EQ(eudora_mailbox_count(mb), 1);
    CHECK(eudora_mailbox_summary(mb, 0, &sum));
    CHECK_EQ(sum.offset, 0);
    CHECK_EQ(static_cast<std::size_t>(sum.length), kMailbox.size());
    eudora_mailbox_close(mb);
}

TEST_CASE("C API: message parsing and decoding") {
    eudora_message *msg = eudora_message_parse(kMailbox.data(), kMailbox.size());
    CHECK(msg != nullptr);
    if (!msg)
        return;

    char *subj = eudora_message_header_decoded(msg, "Subject");
    CHECK_EQ(std::string(subj), "café plans");
    eudora_string_free(subj);

    char *missing = eudora_message_header(msg, "X-Nope");
    CHECK(missing == nullptr);

    CHECK_EQ(std::string(eudora_message_content_type(msg)), "text");
    CHECK_EQ(std::string(eudora_message_content_subtype(msg)), "plain");
    CHECK(std::strstr(eudora_message_body(msg), "meet at nine") != nullptr);
    eudora_message_free(msg);

    size_t out_len = 0;
    char *decoded = eudora_decode_body("Zm9vYmFy", 8, 2, &out_len);
    CHECK(decoded != nullptr);
    if (decoded) {
        CHECK_EQ(out_len, 6u);
        CHECK_EQ(std::string(decoded, out_len), "foobar");
        eudora_string_free(decoded);
    }

    char **addrs = eudora_parse_addresses("Alice <a@b.c>, d@e.f");
    CHECK(addrs != nullptr);
    if (addrs) {
        CHECK(addrs[0] && std::string(addrs[0]) == "a@b.c");
        CHECK(addrs[1] && std::string(addrs[1]) == "d@e.f");
        CHECK(addrs[2] == nullptr);
        eudora_addresses_free(addrs);
    }
}

TEST_CASE("C API: filters") {
    const std::string filterText =
        "rule Junk it\r"
        "incoming\r"
        "header subject:\r"
        "verb contains\r"
        "value plans\r"
        "junk 90\r"
        "stop\r";

    eudora_filters *f = eudora_filters_parse(filterText.c_str());
    CHECK(f != nullptr);
    if (!f)
        return;
    CHECK_EQ(eudora_filters_count(f), 1);

    int32_t count = 0;
    eudora_fired_action *fired = eudora_filters_run(
        f, EUDORA_FILTER_INCOMING, kMailbox.data(), kMailbox.size(), &count);
    CHECK_EQ(count, 2);
    CHECK(fired != nullptr);
    if (fired && count == 2) {
        CHECK_EQ(std::string(fired[0].keyword), "junk");
        CHECK_EQ(std::string(fired[0].value), "90");
        CHECK_EQ(std::string(fired[1].keyword), "stop");
        CHECK_EQ(std::string(fired[0].filter_name), "Junk it");
    }
    eudora_fired_actions_free(fired, count);

    const fs::path ff = temp_dir() / "Eudora Filters";
    CHECK(eudora_filters_save(f, ff.string().c_str()));
    eudora_filters_free(f);

    eudora_filters *again = eudora_filters_load(ff.string().c_str());
    CHECK(again != nullptr);
    if (again) {
        CHECK_EQ(eudora_filters_count(again), 1);
        eudora_filters_free(again);
    }
}

#if !defined(_WIN32)
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

TEST_CASE("C API: POP3 fetch against a live localhost server") {
    // A miniature POP3 server on a loopback socket, driving the real
    // PosixTransport end to end.
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    CHECK(::bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
    CHECK(::listen(listener, 1) == 0);
    socklen_t alen = sizeof(addr);
    CHECK(::getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &alen) == 0);
    const uint16_t port = ntohs(addr.sin_port);

    std::thread server([listener] {
        const int c = ::accept(listener, nullptr, nullptr);
        if (c < 0)
            return;
        const auto say = [c](const char *s) { (void)!::write(c, s, strlen(s)); };
        char buf[512];
        const auto readline = [&]() -> std::string {
            std::string line;
            char ch;
            while (::read(c, &ch, 1) == 1) {
                line += ch;
                if (ch == '\n')
                    break;
            }
            return line;
        };
        say("+OK mini POP3 ready\r\n");
        for (;;) {
            const std::string cmd = readline();
            if (cmd.empty())
                break;
            if (cmd.rfind("CAPA", 0) == 0)
                say("+OK\r\nUIDL\r\nTOP\r\n.\r\n");
            else if (cmd.rfind("USER", 0) == 0 || cmd.rfind("PASS", 0) == 0)
                say("+OK\r\n");
            else if (cmd.rfind("STAT", 0) == 0)
                say("+OK 1 90\r\n");
            else if (cmd.rfind("RETR", 0) == 0)
                say("+OK\r\n"
                    "From: remote@server.example\r\n"
                    "Subject: fetched\r\n"
                    "\r\n"
                    ".dotted first line\r\n"
                    "plain body\r\n"
                    ".\r\n");
            else if (cmd.rfind("QUIT", 0) == 0) {
                say("+OK bye\r\n");
                break;
            } else
                say("+OK\r\n");
        }
        ::close(c);
        (void)buf;
    });

    const fs::path box = temp_dir() / "FetchedBox";
    std::error_code ec;
    fs::remove(box, ec);
    fs::remove(box.string() + ".toc", ec);

    const int32_t n = eudora_pop3_fetch("127.0.0.1", port, EUDORA_TLS_NONE,
                                        "user", "pass", box.string().c_str(),
                                        /*delete_from_server=*/0);
    server.join();
    ::close(listener);

    CHECK_EQ(n, 1);
    eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (mb) {
        CHECK_EQ(eudora_mailbox_count(mb), 1);
        eudora_summary sum{};
        CHECK(eudora_mailbox_summary(mb, 0, &sum));
        CHECK_EQ(std::string(sum.subject), "fetched");
        CHECK_EQ(std::string(sum.from), "remote@server.example");
        char *raw = eudora_mailbox_read_message(mb, 0);
        CHECK(raw != nullptr);
        if (raw) {
            // Envelope added, transparency dot removed.
            CHECK(std::strncmp(raw, "From remote@server.example ", 27) == 0);
            CHECK(std::strstr(raw, "\r.dotted first line\r") != nullptr);
            eudora_string_free(raw);
        }
        eudora_mailbox_close(mb);
    }
}
#endif // !_WIN32

TEST_CASE("C API: mailbox append message") {
    const fs::path box = temp_dir() / "AppendBox";
    write_file(box, kMailbox);
    eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (!mb)
        return;
    CHECK_EQ(eudora_mailbox_count(mb), 1);

    // LF-terminated draft gets normalized, enveloped, and summarized.
    const std::string draft =
        "From: me@example.com\nTo: you@example.org\nSubject: queued draft\n"
        "\nsend me later\n";
    const int32_t idx = eudora_mailbox_append_message(
        mb, draft.data(), draft.size(), EUDORA_STATE_QUEUED);
    CHECK_EQ(idx, 1);
    eudora_summary sum{};
    CHECK(eudora_mailbox_summary(mb, 1, &sum));
    CHECK_EQ(std::string(sum.subject), "queued draft");
    CHECK(sum.state == EUDORA_STATE_QUEUED);
    CHECK(eudora_mailbox_save(mb));
    char *raw = eudora_mailbox_read_message(mb, 1);
    CHECK(raw != nullptr);
    if (raw) {
        CHECK(std::strncmp(raw, "From me@example.com ", 20) == 0);
        CHECK(std::strstr(raw, "send me later\r") != nullptr);
        eudora_string_free(raw);
    }

    CHECK(eudora_mailbox_set_label(mb, 1, 5));
    CHECK(eudora_mailbox_summary(mb, 1, &sum));
    CHECK_EQ((sum.flags >> 14) & 0xF, 5u);
    eudora_mailbox_close(mb);

    // Reopen: the TOC still matches the enlarged mailbox.
    mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (mb) {
        CHECK_EQ(eudora_mailbox_count(mb), 2);
        eudora_mailbox_close(mb);
    }
}

TEST_CASE("C API: filter editing surface") {
    eudora_filters *f = eudora_filters_new();
    CHECK(f != nullptr);
    if (!f)
        return;
    CHECK_EQ(eudora_filters_count(f), 0);

    const int32_t i = eudora_filters_add(f, "My rule");
    CHECK_EQ(i, 0);

    eudora_filter_info info{};
    CHECK(eudora_filters_get(f, i, &info));
    CHECK_EQ(std::string(info.name), "My rule");
    CHECK(info.incoming == 1);

    eudora_filter_info edit{};
    edit.name = "Spam rule";
    edit.incoming = 1;
    edit.manual = 1;
    edit.header1 = "subject:";
    edit.verb1 = "contains";
    edit.value1 = "viagra";
    edit.conjunction = "or";
    edit.header2 = "«any header»";
    edit.verb2 = "contains";
    edit.value2 = "lottery";
    CHECK(eudora_filters_set(f, i, &edit));

    CHECK(eudora_filter_action_add(f, i, "junk", "95"));
    CHECK(eudora_filter_action_add(f, i, "stop", ""));
    CHECK(!eudora_filter_action_add(f, i, "rule", "x")); // not an action
    CHECK_EQ(eudora_filter_action_count(f, i), 2);
    const char *kw = nullptr, *val = nullptr;
    CHECK(eudora_filter_action_get(f, i, 0, &kw, &val));
    CHECK_EQ(std::string(kw), "junk");
    CHECK_EQ(std::string(val), "95");

    // Serialized set round trips through the classic file format.
    const fs::path ff = temp_dir() / "EditedFilters";
    CHECK(eudora_filters_save(f, ff.string().c_str()));
    eudora_filters *again = eudora_filters_load(ff.string().c_str());
    CHECK(again != nullptr);
    if (again) {
        CHECK_EQ(eudora_filters_count(again), 1);
        eudora_filter_info back{};
        CHECK(eudora_filters_get(again, 0, &back));
        CHECK_EQ(std::string(back.name), "Spam rule");
        CHECK_EQ(std::string(back.conjunction), "or");
        CHECK_EQ(std::string(back.value2), "lottery");
        CHECK(back.manual == 1);
        eudora_filters_free(again);
    }

    // The edited filter actually fires.
    const std::string msg =
        "From x@y.z Wed Jun 14 12:36:18 1989\r"
        "Subject: get viagra here\r\r body\r";
    int32_t count = 0;
    eudora_fired_action *fired = eudora_filters_run(
        f, EUDORA_FILTER_INCOMING, msg.data(), msg.size(), &count);
    CHECK_EQ(count, 2);
    eudora_fired_actions_free(fired, count);

    // move/remove bookkeeping
    const int32_t j = eudora_filters_add(f, "Second");
    CHECK(eudora_filters_move(f, j, 0));
    eudora_filter_info first{};
    CHECK(eudora_filters_get(f, 0, &first));
    CHECK_EQ(std::string(first.name), "Second");
    CHECK(eudora_filters_remove(f, 0));
    CHECK_EQ(eudora_filters_count(f), 1);
    eudora_filters_free(f);
}

TEST_CASE("C API: version and errors") {
    CHECK_EQ(std::string(eudora_core_version()), "0.1.0");
    CHECK(eudora_mailbox_open("/nonexistent/path/Box") == nullptr);
    CHECK(std::string(eudora_last_error()).find("not found") !=
          std::string::npos);
}

EUTEST_MAIN
