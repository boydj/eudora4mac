// Live-socket tests for eudora_imap_fetch_ex against a miniature IMAP
// server, mirroring the POP3 mini-server tests in test_c_api.cpp.

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "eudora/eudora_core.h"
#include "test_framework.hpp"

#if !defined(_WIN32)

#include <algorithm>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path temp_dir() {
    const fs::path d = fs::temp_directory_path() / "eudora_imap_tests";
    fs::create_directories(d);
    return d;
}

struct MiniImapServer {
    struct Msg {
        unsigned long uid;
        std::string flags; // e.g. "\\Seen", "" for none
        std::string text;  // CRLF lines
        bool deleted = false;
    };

    std::vector<Msg> msgs;
    unsigned long uidvalidity = 7;
    int stores = 0;
    int expunges = 0;
    std::vector<std::string> flag_stores; // "<uid> <flags>" recorded per STORE
    std::vector<std::string> folders = {"INBOX", "Archive", "Sent Items"};
    std::mutex mu;
    int listener = -1;
    uint16_t port = 0;
    std::thread thread;

    void add_message(unsigned long uid, std::string flags, std::string text) {
        std::lock_guard<std::mutex> lock(mu);
        msgs.push_back({uid, std::move(flags), std::move(text), false});
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
        say("* OK mini IMAP ready\r\n");
        for (;;) {
            std::string line = readline();
            if (line.empty())
                break;
            while (!line.empty() &&
                   (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            const auto sp = line.find(' ');
            if (sp == std::string::npos)
                continue;
            const std::string tag = line.substr(0, sp);
            const std::string cmd = line.substr(sp + 1);
            std::lock_guard<std::mutex> lock(mu);
            if (cmd.rfind("CAPABILITY", 0) == 0) {
                say("* CAPABILITY IMAP4rev1\r\n" + tag + " OK done\r\n");
            } else if (cmd.rfind("LOGIN", 0) == 0) {
                say(tag + " OK logged in\r\n");
            } else if (cmd.rfind("SELECT", 0) == 0) {
                std::size_t live = 0;
                for (const auto &m : msgs)
                    if (!m.deleted)
                        ++live;
                say("* " + std::to_string(live) + " EXISTS\r\n" +
                    "* OK [UIDVALIDITY " + std::to_string(uidvalidity) +
                    "] ok\r\n" + tag + " OK [READ-WRITE] selected\r\n");
            } else if (cmd.rfind("UID FETCH ", 0) == 0) {
                const bool want_body =
                    cmd.find("BODY.PEEK") != std::string::npos;
                const std::string spec =
                    cmd.substr(10, cmd.find(' ', 10) - 10);
                long seq = 0;
                for (const auto &m : msgs) {
                    if (m.deleted)
                        continue;
                    ++seq;
                    if (spec != "1:*" &&
                        m.uid != std::strtoul(spec.c_str(), nullptr, 10))
                        continue;
                    std::string r = "* " + std::to_string(seq) +
                                    " FETCH (UID " + std::to_string(m.uid) +
                                    " FLAGS (" + m.flags + ")";
                    if (want_body)
                        r += " BODY[] {" + std::to_string(m.text.size()) +
                             "}\r\n" + m.text;
                    r += ")\r\n";
                    say(r);
                }
                say(tag + " OK fetched\r\n");
            } else if (cmd.rfind("LIST ", 0) == 0) {
                for (const auto &f : folders)
                    say("* LIST () \"/\" \"" + f + "\"\r\n");
                say(tag + " OK listed\r\n");
            } else if (cmd.rfind("UID STORE ", 0) == 0) {
                ++stores;
                std::string args = cmd.substr(10);
                std::string set = args.substr(0, args.find(' '));
                flag_stores.push_back(set + " " + args);
                const bool deleting = args.find("\\Deleted") != std::string::npos;
                std::size_t pos = 0;
                for (;;) {
                    const unsigned long uid =
                        std::strtoul(set.c_str() + pos, nullptr, 10);
                    if (deleting)
                        for (auto &m : msgs)
                            if (m.uid == uid)
                                m.deleted = true;
                    const std::size_t comma = set.find(',', pos);
                    if (comma == std::string::npos)
                        break;
                    pos = comma + 1;
                }
                say(tag + " OK stored\r\n");
            } else if (cmd.rfind("EXPUNGE", 0) == 0) {
                ++expunges;
                say(tag + " OK expunged\r\n");
            } else if (cmd.rfind("LOGOUT", 0) == 0) {
                say("* BYE\r\n" + tag + " OK bye\r\n");
                break;
            } else {
                say(tag + " OK whatever\r\n");
            }
        }
        ::close(c);
    }
};

struct FetchProgress {
    std::vector<std::string> stages;
    int32_t last_done = -1;
    int32_t last_total = -1;
    int cancel_at = -1;
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

int32_t fetch(const MiniImapServer &server, const fs::path &box,
              int delete_from_server = 0, FetchProgress *prog = nullptr) {
    return eudora_imap_fetch_ex("127.0.0.1", server.port, EUDORA_TLS_NONE,
                                "user", "pass", nullptr,
                                box.string().c_str(), delete_from_server,
                                prog ? fetch_progress_cb : nullptr, prog);
}

} // namespace

TEST_CASE("C API: IMAP fetch, state mapping, dedup, cancel") {
    MiniImapServer server;
    server.add_message(101, "\\Seen",
                       "From: a@example.com\r\nSubject: seen one\r\n\r\nalpha\r\n");
    server.add_message(102, "",
                       "From: b@example.com\r\nSubject: fresh two\r\n\r\nbeta\r\n");
    CHECK(server.start(4));

    const fs::path box = temp_dir() / "ImapBox";
    std::error_code ec;
    fs::remove(box, ec);
    fs::remove(box.string() + ".toc", ec);

    // Initial fetch: both messages arrive, server flags choose the states.
    FetchProgress prog;
    int32_t n = fetch(server, box, 0, &prog);
    CHECK_EQ(n, 2);
    for (const char *stage : {"connect", "auth", "list", "retr"})
        CHECK(std::find(prog.stages.begin(), prog.stages.end(), stage) !=
              prog.stages.end());
    CHECK_EQ(prog.last_done, 2);
    CHECK_EQ(prog.last_total, 2);
    {
        eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
        CHECK(mb != nullptr);
        if (mb) {
            CHECK_EQ(eudora_mailbox_count(mb), 2);
            eudora_summary sum{};
            CHECK(eudora_mailbox_summary(mb, 0, &sum));
            CHECK(sum.state == EUDORA_STATE_READ); // \Seen
            CHECK_EQ(std::string(sum.subject), "seen one");
            CHECK(eudora_mailbox_summary(mb, 1, &sum));
            CHECK(sum.state == EUDORA_STATE_UNREAD);
            eudora_mailbox_close(mb);
        }
    }

    // Second session: the UIDVALIDITY/UID hashes dedup everything.
    n = fetch(server, box);
    CHECK_EQ(n, 0);

    // A new message on the server: only it is fetched.
    server.add_message(103, "",
                       "From: c@example.com\r\nSubject: three\r\n\r\ngamma\r\n");
    n = fetch(server, box);
    CHECK_EQ(n, 1);

    // Cancel before the first body fetch: the fourth message stays put.
    server.add_message(104, "",
                       "From: d@example.com\r\nSubject: four\r\n\r\ndelta\r\n");
    FetchProgress cancelProg;
    cancelProg.cancel_at = 0;
    n = fetch(server, box, 0, &cancelProg);
    CHECK_EQ(n, 0);

    eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
    CHECK(mb != nullptr);
    if (mb) {
        CHECK_EQ(eudora_mailbox_count(mb), 3);
        eudora_summary sum{};
        CHECK(eudora_mailbox_summary(mb, 2, &sum));
        CHECK_EQ(std::string(sum.subject), "three");
        eudora_mailbox_close(mb);
    }
    server.finish();
}

TEST_CASE("C API: IMAP delete-from-server stores \\Deleted and expunges") {
    MiniImapServer server;
    server.add_message(7, "",
                       "From: d@example.com\r\nSubject: del\r\n\r\nbye\r\n");
    CHECK(server.start(1));

    const fs::path box = temp_dir() / "ImapDelBox";
    std::error_code ec;
    fs::remove(box, ec);
    fs::remove(box.string() + ".toc", ec);

    const int32_t n = fetch(server, box, /*delete_from_server=*/1);
    server.finish();

    CHECK_EQ(n, 1);
    CHECK_EQ(server.stores, 1);
    CHECK_EQ(server.expunges, 1);
    CHECK(server.msgs[0].deleted);
}

TEST_CASE("C API: IMAP folder listing skips \\Noselect") {
    MiniImapServer server;
    server.folders = {"INBOX", "Work", "Sent"};
    CHECK(server.start(1));
    char **folders = eudora_imap_list_folders("127.0.0.1", server.port,
                                              EUDORA_TLS_NONE, "user", "pass");
    server.finish();
    CHECK(folders != nullptr);
    if (folders) {
        int count = 0;
        for (char **p = folders; *p; ++p)
            ++count;
        CHECK_EQ(count, 3);
        CHECK_EQ(std::string(folders[0]), "INBOX");
        CHECK_EQ(std::string(folders[2]), "Sent");
        eudora_addresses_free(folders);
    }
}

TEST_CASE("C API: IMAP flag write-back stores \\Seen for read messages") {
    MiniImapServer server;
    server.uidvalidity = 42;
    server.add_message(1001, "",
                       "From: a@example.com\r\nSubject: one\r\n\r\nbody a\r\n");
    server.add_message(1002, "",
                       "From: b@example.com\r\nSubject: two\r\n\r\nbody b\r\n");
    CHECK(server.start(2)); // fetch session + sync session

    const fs::path box = temp_dir() / "ImapSyncBox";
    std::error_code ec;
    fs::remove(box, ec);
    fs::remove(box.string() + ".toc", ec);

    int32_t n = fetch(server, box);
    CHECK_EQ(n, 2);

    // Mark the first message read locally, then sync.
    {
        eudora_mailbox *mb = eudora_mailbox_open(box.string().c_str());
        CHECK(mb != nullptr);
        if (mb) {
            eudora_mailbox_set_state(mb, 0, EUDORA_STATE_READ);
            eudora_mailbox_save(mb);
            eudora_mailbox_close(mb);
        }
    }
    const int32_t synced = eudora_imap_sync_flags(
        "127.0.0.1", server.port, EUDORA_TLS_NONE, "user", "pass", nullptr,
        box.string().c_str());
    server.finish();

    CHECK_EQ(synced, 1);
    bool saw_seen = false;
    for (const auto &s : server.flag_stores)
        if (s.find("1001") != std::string::npos &&
            s.find("\\Seen") != std::string::npos)
            saw_seen = true;
    CHECK(saw_seen);
}

#endif // !_WIN32

EUTEST_MAIN
