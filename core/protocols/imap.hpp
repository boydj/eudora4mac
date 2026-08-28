// IMAP4rev1 client — the modern successor to Eudora's IMAP stack.
//
// The legacy build embedded the UW c-client (CrispinIMAP/imap4r1.c) behind
// the imapnetlib.c façade, wired to the TransVector via net_* glue.  This
// engine reimplements the protocol surface that façade actually used —
// capability/login/select/list/status/fetch/store/search/append/expunge —
// directly over the Transport abstraction, with full literal handling.
// Message contents are fetched whole and parsed by mail/header_parser, the
// same division of labor as the POP3 path.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "net/line_receiver.hpp"
#include "net/transport.hpp"
#include "protocols/sasl.hpp"

namespace eudora {

// A parsed IMAP data item: atom/NIL, quoted string or literal, or a
// parenthesized list.
struct ImapToken {
    enum class Kind { Atom, String, List, Nil };
    Kind kind = Kind::Atom;
    std::string text;              // Atom/String
    std::vector<ImapToken> items;  // List

    bool is_atom(std::string_view name) const; // case-insensitive
};

// One untagged response, tokenized: "* 12 FETCH (UID 5 ...)" becomes
// tokens [12, FETCH, (UID 5 ...)].
struct ImapUntagged {
    std::vector<ImapToken> tokens;
};

struct ImapMailboxInfo {
    std::string name;
    long exists = 0;
    long recent = 0;
    long unseen = 0;
    std::uint32_t uid_validity = 0;
    std::uint32_t uid_next = 0;
    std::vector<std::string> flags;
    std::vector<std::string> permanent_flags;
    bool read_only = false;
};

struct ImapListEntry {
    std::string name;
    std::vector<std::string> attributes; // \Noselect, \HasChildren, ...
    char delimiter = '/';
};

struct ImapFetchResult {
    long sequence = 0;
    std::uint32_t uid = 0;
    std::vector<std::string> flags;
    long rfc822_size = -1;
    std::string internal_date;
    std::string body;        // BODY[] / RFC822 literal, when requested
    std::string header_text; // BODY[HEADER] literal, when requested
};

class ImapSession {
public:
    explicit ImapSession(Transport &transport)
        : transport_(transport),
          lines_(transport, 8192, /*treat_bare_cr_as_newline=*/false) {}

    // Connect and read the greeting.  preauth() reports a PREAUTH greeting.
    bool connect(const std::string &host, std::uint16_t port,
                 long timeout_seconds = 45);
    bool begin_connected(); // externally connected/TLS-wrapped transports
    bool preauth() const { return preauth_; }

    // CAPABILITY (refreshed automatically after login/STARTTLS).
    const std::vector<std::string> &capabilities();
    bool has_capability(std::string_view name);

    // STARTTLS: on success caller performs the handshake, then MUST call
    // rescan_capabilities().
    bool request_starttls();
    void rescan_capabilities() { caps_valid_ = false; }

    // LOGIN or AUTHENTICATE (strongest offered SASL mechanism).
    bool login(const std::string &user, const std::string &password);
    bool logout();

    bool select(const std::string &mailbox, ImapMailboxInfo &info);
    bool examine(const std::string &mailbox, ImapMailboxInfo &info);
    bool close_mailbox();

    std::optional<std::vector<ImapListEntry>>
    list(const std::string &reference, const std::string &pattern);

    bool create_mailbox(const std::string &name);
    bool delete_mailbox(const std::string &name);
    bool rename_mailbox(const std::string &from, const std::string &to);

    // UID FETCH with the given item set (e.g. "(UID FLAGS RFC822.SIZE)" or
    // "(UID BODY.PEEK[])").  Results keyed by sequence order.
    std::optional<std::vector<ImapFetchResult>>
    uid_fetch(const std::string &uid_set, const std::string &items);

    // UID STORE: mode is "+FLAGS", "-FLAGS", or "FLAGS" (silent variants
    // are used internally).
    bool uid_store(const std::string &uid_set, const std::string &mode,
                   const std::string &flags);

    // UID SEARCH; criteria per RFC 3501 (e.g. "UNSEEN SINCE 1-Jan-2020").
    std::optional<std::vector<std::uint32_t>> uid_search(const std::string &criteria);

    // APPEND a complete message (client literal with continuation).
    bool append(const std::string &mailbox, const std::string &flags,
                const std::string &message);

    bool expunge();
    bool noop();

    const std::string &last_response() const { return last_tagged_; }
    const std::string &greeting() const { return greeting_; }

private:
    std::string next_tag();
    bool send_line(const std::string &line);
    // Run a command to its tagged completion; untagged responses are handed
    // to `sink` (may be nullptr).
    bool run_command(const std::string &args,
                     const std::function<void(const ImapUntagged &)> &sink,
                     const std::string *literal = nullptr);
    void handle_select_response(const ImapUntagged &u, ImapMailboxInfo &info);
    static std::string quote_string(const std::string &s);

    Transport &transport_;
    LineReceiver lines_;
    unsigned tag_counter_ = 0;
    std::string current_tag_;
    std::string greeting_;
    std::string last_tagged_;
    bool preauth_ = false;
    bool connected_ = false;
    bool caps_valid_ = false;
    std::vector<std::string> caps_;
};

// Map IMAP flags to Eudora summary state (imapdownload.c's conversion).
// \Seen -> Read, \Answered -> Replied, else Unread.
std::uint8_t imap_flags_to_state(const std::vector<std::string> &flags);

} // namespace eudora
