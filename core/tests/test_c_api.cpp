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
#include <algorithm>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

// A miniature POP3 server on a loopback socket, driving the real
// PosixTransport end to end.  Serves a fixed number of sequential sessions;
// the message list may change between sessions (it is mutex-guarded).
struct MiniPop3Server {
    struct Msg {
        std::string uid;
        std::string text; // CRLF-terminated lines
    };

    std::vector<Msg> msgs;
    std::mutex mu;
    int listener = -1;
    uint16_t port = 0;
    std::thread thread;

    void add_message(std::string uid, std::string text) {
        std::lock_guard<std::mutex> lock(mu);
        msgs.push_back({std::move(uid), std::move(text)});
    }

    bool start(int connections) {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0)
            return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listener, reinterpret_cast<sockaddr *>(&addr),
                   sizeof(addr)) != 0 ||
            ::listen(listener, 1) != 0)
            return false;
        socklen_t alen = sizeof(addr);
        if (::getsockname(listener, reinterpret_cast<sockaddr *>(&addr),
                          &alen) != 0)
            return false;
        port = ntohs(addr.sin_port);
        thread = std::thread([this, connections] {
            for (int i = 0; i < connections; ++i) {
                const int c = ::accept(listener, nullptr, nullptr);
                if (c < 0)
                    return;
                serve(c);
            }
        });
        return true;
    }

    void finish() {
        if (thread.joinable())
            thread.join();
        if (listener >= 0) {
            ::close(listener);
            listener = -1;
        }
    }

    void serve(int c) {
        const auto say = [c](const std::string &s) {
            (void)!::write(c, s.data(), s.size());
        };
        const auto readline = [c]() -> std::string {
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
            std::lock_guard<std::mutex> lock(mu);
            if (cmd.rfind("CAPA", 0) == 0) {
                say("+OK\r\nUIDL\r\nTOP\r\n.\r\n");
            } else if (cmd.rfind("USER", 0) == 0 || cmd.rfind("PASS", 0) == 0) {
                say("+OK\r\n");
            } else if (cmd.rfind("STAT", 0) == 0) {
                std::size_t total = 0;
                for (const auto &m : msgs)
                    total += m.text.size();
                say("+OK " + std::to_string(msgs.size()) + " " +
                    std::to_string(total) + "\r\n");
            } else if (cmd.rfind("UIDL", 0) == 0) {
                std::string reply = "+OK\r\n";
                for (std::size_t i = 0; i < msgs.size(); ++i)
                    reply += std::to_string(i + 1) + " " + msgs[i].uid + "\r\n";
                say(reply + ".\r\n");
            } else if (cmd.rfind("LIST", 0) == 0) {
                std::string reply = "+OK\r\n";
                for (std::size_t i = 0; i < msgs.size(); ++i)
                    reply += std::to_string(i + 1) + " " +
                             std::to_string(msgs[i].text.size()) + "\r\n";
                say(reply + ".\r\n");
            } else if (cmd.rfind("RETR", 0) == 0) {
                const long n = std::strtol(cmd.c_str() + 5, nullptr, 10);
                if (n >= 1 && n <= static_cast<long>(msgs.size()))
                    say("+OK\r\n" + msgs[static_cast<std::size_t>(n) - 1].text +
                        ".\r\n");
                else
                    say("-ERR no such message\r\n");
            } else if (cmd.rfind("DELE", 0) == 0) {
                say("+OK\r\n");
            } else if (cmd.rfind("QUIT", 0) == 0) {
                say("+OK bye\r\n");
                break;
            } else {
                say("+OK\r\n");
            }
        }
        ::close(c);
    }
};

} // namespace

TEST_CASE("C API: POP3 fetch against a live localhost server") {
    MiniPop3Server server;
    server.add_message("uid-0001",
                       "From: remote@server.example\r\n"
                       "Subject: fetched\r\n"
                       "\r\n"
                       ".dotted first line\r\n"
                       "plain body\r\n");
    CHECK(server.start(1));

    const fs::path box = temp_dir() / "FetchedBox";
    std::error_code ec;
    fs::remove(box, ec);
    fs::remove(box.string() + ".toc", ec);

    const int32_t n = eudora_pop3_fetch("127.0.0.1", server.port,
                                        EUDORA_TLS_NONE, "user", "pass",
                                        box.string().c_str(),
                                        /*delete_from_server=*/0);
    server.finish();

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

namespace {

// Progress recorder for eudora_pop3_fetch_ex (must have C linkage-compatible
// shape: a capture-less callback plus a context struct).
struct FetchProgress {
    std::vector<std::string> stages;
    int32_t last_done = -1;
    int32_t last_total = -1;
    int cancel_at = -1; // cancel when a "retr" callback reports done >= this
};

int fetch_progress_cb(void *ctx, const char *stage, int32_t done,
                      int32_t total) {
    auto *p = static_cast<FetchProgress *>(ctx);
    p->stages.push_back(stage);
    if (std::string(stage) == "retr") {
        p->last_done = done;
        p->last_total = total;
        if (p->cancel_at >= 0 && done >= p->cancel_at)
            return 1;
    }
    return 0;
}

} // namespace

TEST_CASE("C API: POP3 big-message limit skips oversized mail") {
    MiniPop3Server server;
    server.add_message("uid-small",
                       "From: s@example.com\r\nSubject: small\r\n\r\ntiny\r\n");
    std::string big = "From: b@example.com\r\nSubject: big\r\n\r\n";
    big += std::string(4096, 'x');
    big += "\r\n";
    server.add_message("uid-big", std::move(big));
    CHECK(server.start(1));

    const fs::path box = temp_dir() / "BigLimitBox";
    std::error_code ec;
    fs::remove(box, ec);
    fs::remove(box.string() + ".toc", ec);

    eudora_pop3_options opts{};
    opts.max_message_k = 1; // skip anything over 1 KB
    const int32_t n = eudora_pop3_fetch_opts(
        "127.0.0.1", server.port, EUDORA_TLS_NONE, "user", "pass",
        box.string().c_str(), &opts, nullptr, nullptr);
    server.finish();

    CHECK_EQ(n, 1);
    eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (mb) {
        CHECK_EQ(eudora_mailbox_count(mb), 1);
        eudora_summary sum{};
        CHECK(eudora_mailbox_summary(mb, 0, &sum));
        CHECK_EQ(std::string(sum.subject), "small");
        eudora_mailbox_close(mb);
    }
}

TEST_CASE("C API: POP3 UIDL dedup, progress, and cancel") {
    MiniPop3Server server;
    server.add_message("uid-alpha",
                       "From: a@example.com\r\nSubject: alpha\r\n\r\nbody a\r\n");
    server.add_message("uid-beta",
                       "From: b@example.com\r\nSubject: beta\r\n\r\nbody b\r\n");
    CHECK(server.start(4));

    const fs::path box = temp_dir() / "DedupBox";
    std::error_code ec;
    fs::remove(box, ec);
    fs::remove(box.string() + ".toc", ec);

    // First fetch: both messages arrive, with progress through every stage.
    FetchProgress prog;
    int32_t n = eudora_pop3_fetch_ex("127.0.0.1", server.port, EUDORA_TLS_NONE,
                                     "user", "pass", box.string().c_str(), 0,
                                     fetch_progress_cb, &prog);
    CHECK_EQ(n, 2);
    for (const char *stage : {"connect", "auth", "list", "retr"})
        CHECK(std::find(prog.stages.begin(), prog.stages.end(), stage) !=
              prog.stages.end());
    CHECK_EQ(prog.last_done, 2);
    CHECK_EQ(prog.last_total, 2);

    // Second fetch (via the plain entry point): the UIDL hashes recorded in
    // the TOC dedup both messages — nothing is re-downloaded.
    n = eudora_pop3_fetch("127.0.0.1", server.port, EUDORA_TLS_NONE, "user",
                          "pass", box.string().c_str(), 0);
    CHECK_EQ(n, 0);
    {
        eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
        CHECK(mb != nullptr);
        if (mb) {
            CHECK_EQ(eudora_mailbox_count(mb), 2);
            eudora_mailbox_close(mb);
        }
    }

    // A message added on the server side: only the new one is fetched.
    server.add_message("uid-gamma",
                       "From: c@example.com\r\nSubject: gamma\r\n\r\nbody c\r\n");
    n = eudora_pop3_fetch("127.0.0.1", server.port, EUDORA_TLS_NONE, "user",
                          "pass", box.string().c_str(), 0);
    CHECK_EQ(n, 1);

    // Cancel from the callback before the first RETR: the new fourth message
    // stays on the server and the mailbox is untouched.
    server.add_message("uid-delta",
                       "From: d@example.com\r\nSubject: delta\r\n\r\nbody d\r\n");
    FetchProgress cancelProg;
    cancelProg.cancel_at = 0;
    n = eudora_pop3_fetch_ex("127.0.0.1", server.port, EUDORA_TLS_NONE, "user",
                             "pass", box.string().c_str(), 0,
                             fetch_progress_cb, &cancelProg);
    CHECK_EQ(n, 0);

    eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (mb) {
        CHECK_EQ(eudora_mailbox_count(mb), 3);
        eudora_summary sum{};
        CHECK(eudora_mailbox_summary(mb, 2, &sum));
        CHECK_EQ(std::string(sum.subject), "gamma");
        eudora_mailbox_close(mb);
    }
    server.finish();
}
#endif // !_WIN32

TEST_CASE("C API: MIME parts and binary-safe decode") {
    // Base64 of bytes {'A', 0x00, 'B', 0xFF} = "QQBC/w==".
    const std::string raw =
        "From: a@example.com\r"
        "Content-Type: multipart/mixed; boundary=\"zz\"\r"
        "\r"
        "--zz\r"
        "Content-Type: text/plain\r"
        "\r"
        "see attachment\r"
        "--zz\r"
        "Content-Type: application/octet-stream\r"
        "Content-Transfer-Encoding: base64\r"
        "Content-Disposition: attachment; filename=\"nul.bin\"\r"
        "\r"
        "QQBC/w==\r"
        "--zz--\r";
    eudora_message *msg = eudora_message_parse(raw.data(), raw.size());
    CHECK(msg != nullptr);
    if (!msg)
        return;
    CHECK_EQ(eudora_message_part_count(msg), 2);

    eudora_part_info info{};
    CHECK(eudora_message_part_info(msg, 1, &info));
    CHECK_EQ(std::string(info.filename), "nul.bin");
    CHECK_EQ(info.transfer_encoding, 2);
    CHECK(info.is_attachment);

    size_t len = 0;
    char *bytes = eudora_message_part_decode(msg, 1, &len);
    CHECK(bytes != nullptr);
    if (bytes) {
        // The decoded data contains a NUL: length, not strlen, is truth.
        CHECK_EQ(len, static_cast<size_t>(4));
        CHECK_EQ(bytes[0], 'A');
        CHECK_EQ(bytes[1], '\0');
        CHECK_EQ(bytes[2], 'B');
        CHECK_EQ(static_cast<unsigned char>(bytes[3]), 0xFFu);
        eudora_string_free(bytes);
    }
    CHECK(eudora_message_part_info(msg, 2, &info) == 0); // out of range
    eudora_message_free(msg);
}

TEST_CASE("C API: summary setters and find-by-serial") {
    const fs::path box = temp_dir() / "SetterBox";
    write_file(box, kMailbox + kMailbox);
    eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (!mb)
        return;

    eudora_summary sum{};
    CHECK(eudora_mailbox_summary(mb, 1, &sum));
    CHECK_EQ(eudora_mailbox_find_by_serial(mb, sum.serial_num), 1);
    CHECK_EQ(eudora_mailbox_find_by_serial(mb, 999999), -1);
    CHECK(sum.arrival_unix > 0);

    CHECK(eudora_mailbox_set_priority(mb, 0, 5));
    CHECK(eudora_mailbox_set_priority(mb, 1, 1));
    CHECK(!eudora_mailbox_set_priority(mb, 1, 6)); // out of the 1-5 scale
    CHECK(eudora_mailbox_set_spam_score(mb, 1, 200)); // clamps to 127
    CHECK(eudora_mailbox_set_subject(mb, 1, "renamed subject"));
    CHECK(eudora_mailbox_save(mb));
    eudora_mailbox_close(mb);

    mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (mb) {
        eudora_summary again{};
        CHECK(eudora_mailbox_summary(mb, 0, &again));
        CHECK_EQ(again.priority_display, 5);
        CHECK(eudora_mailbox_summary(mb, 1, &again));
        CHECK_EQ(again.priority_display, 1);
        CHECK_EQ(again.spam_score, 127);
        CHECK_EQ(std::string(again.subject), "renamed subject");
        eudora_mailbox_close(mb);
    }
}

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
