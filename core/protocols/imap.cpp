#include "protocols/imap.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "mailstore/summary.hpp"

namespace eudora {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string_view strip_cr(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

// ---- response tokenizer ----------------------------------------------------
//
// A logical response is the raw line text with literals spliced in as
// out-of-band segments: the reader replaces each {n}CRLF + n bytes with a
// placeholder \x01 and stores the bytes in `literals` in order.

struct Cursor {
    std::string_view text;
    const std::vector<std::string> *literals;
    std::size_t pos = 0;
    std::size_t literal_index = 0;

    bool at_end() const { return pos >= text.size(); }
    char peek() const { return text[pos]; }

    void skip_space() {
        while (pos < text.size() && text[pos] == ' ')
            ++pos;
    }
};

bool is_atom_char(char c) {
    switch (c) {
    case '(': case ')': case '{': case ' ': case '"': case '\x01':
        return false;
    default:
        return static_cast<unsigned char>(c) > 0x1F;
    }
    // ']' is legal inside atoms here so BODY[HEADER] parses as one atom.
}

ImapToken parse_token(Cursor &cur);

ImapToken parse_list(Cursor &cur) {
    ImapToken list;
    list.kind = ImapToken::Kind::List;
    ++cur.pos; // '('
    for (;;) {
        cur.skip_space();
        if (cur.at_end())
            break;
        if (cur.peek() == ')') {
            ++cur.pos;
            break;
        }
        list.items.push_back(parse_token(cur));
    }
    return list;
}

ImapToken parse_token(Cursor &cur) {
    ImapToken tok;
    cur.skip_space();
    if (cur.at_end())
        return tok;
    const char c = cur.peek();
    if (c == '(')
        return parse_list(cur);
    if (c == '"') {
        tok.kind = ImapToken::Kind::String;
        ++cur.pos;
        while (!cur.at_end() && cur.peek() != '"') {
            if (cur.peek() == '\\' && cur.pos + 1 < cur.text.size())
                ++cur.pos;
            tok.text += cur.text[cur.pos++];
        }
        if (!cur.at_end())
            ++cur.pos;
        return tok;
    }
    if (c == '\x01') {
        // literal placeholder
        tok.kind = ImapToken::Kind::String;
        if (cur.literals && cur.literal_index < cur.literals->size())
            tok.text = (*cur.literals)[cur.literal_index++];
        ++cur.pos;
        return tok;
    }
    // atom (or NIL / number)
    while (!cur.at_end() && is_atom_char(cur.peek()))
        tok.text += cur.text[cur.pos++];
    // A stray non-atom byte (e.g. a bare '{' or '}' from a malicious or
    // broken server) matches no case above and is not an atom char, so the
    // loop consumes nothing.  Skip one byte to guarantee forward progress —
    // otherwise tokenize()/parse_list() would spin forever appending empty
    // tokens until memory is exhausted.
    if (tok.text.empty() && !cur.at_end())
        ++cur.pos;
    if (iequals(tok.text, "NIL"))
        tok.kind = ImapToken::Kind::Nil;
    return tok;
}

std::vector<ImapToken> tokenize(std::string_view line,
                                const std::vector<std::string> &literals) {
    Cursor cur{line, &literals};
    std::vector<ImapToken> out;
    for (;;) {
        cur.skip_space();
        if (cur.at_end())
            break;
        out.push_back(parse_token(cur));
    }
    return out;
}

// A malicious server must not be able to make us pre-allocate arbitrary
// memory from an announced literal size; cap it well above any real
// message part (matching the spirit of the big-message limit).
constexpr long kMaxImapLiteral = 128L * 1024 * 1024; // 128 MiB

std::optional<long> literal_size(std::string_view line) {
    // A line ending "{123}" announces a literal.
    line = strip_cr(line);
    if (line.size() < 3 || line.back() != '}')
        return std::nullopt;
    const auto open = line.find_last_of('{');
    if (open == std::string_view::npos)
        return std::nullopt;
    std::string_view digits = line.substr(open + 1, line.size() - open - 2);
    if (digits.empty())
        return std::nullopt;
    // Ignore a LITERAL+ "+" suffix.
    if (digits.back() == '+')
        digits.remove_suffix(1);
    if (digits.empty() || digits.size() > 18) // 18 digits fit in a long
        return std::nullopt;
    for (char c : digits)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return std::nullopt;
    const long size = std::atol(std::string(digits).c_str());
    if (size < 0 || size > kMaxImapLiteral)
        return std::nullopt; // reject: run_command treats this as a bad line
    return size;
}

} // namespace

bool ImapToken::is_atom(std::string_view name) const {
    return kind == Kind::Atom && iequals(text, name);
}

std::string ImapSession::next_tag() {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "A%04u", ++tag_counter_);
    return buf;
}

bool ImapSession::send_line(const std::string &line) {
    return transport_.send(line + "\r\n") == NetError::None;
}

bool ImapSession::run_command(
    const std::string &args,
    const std::function<void(const ImapUntagged &)> &sink,
    const std::string *literal) {
    current_tag_ = next_tag();
    if (!send_line(current_tag_ + " " + args))
        return false;

    bool literal_pending = literal != nullptr;
    for (;;) {
        ImapUntagged untagged;
        // Continuations ("+ ...") never carry literals from the server.
        std::string line;
        const NetError err = lines_.recv_line(line, 64 * 1024);
        if (err != NetError::None && line.empty())
            return false;

        const std::string_view body = strip_cr(line);
        if (!body.empty() && body.front() == '+') {
            if (literal_pending) {
                if (transport_.send(*literal) != NetError::None)
                    return false;
                if (!send_line(""))
                    return false;
                literal_pending = false;
            }
            continue;
        }

        // Not a continuation: it may announce a literal; finish reading it.
        std::vector<std::string> literals;
        std::string logical(body);
        std::string cur_line = line;
        while (auto lit = literal_size(cur_line)) {
            const auto open = logical.find_last_of('{');
            logical.resize(open);
            logical += '\x01';
            std::string data;
            if (lines_.recv_bytes(data, static_cast<std::size_t>(*lit)) !=
                NetError::None)
                return false;
            literals.push_back(std::move(data));
            if (lines_.recv_line(cur_line, 64 * 1024) != NetError::None &&
                cur_line.empty())
                return false;
            logical += strip_cr(cur_line);
        }

        if (logical.rfind(current_tag_ + " ", 0) == 0) {
            last_tagged_ = logical.substr(current_tag_.size() + 1);
            std::string_view status = last_tagged_;
            const auto sp = status.find(' ');
            const std::string_view verdict =
                sp == std::string_view::npos ? status : status.substr(0, sp);
            return iequals(verdict, "OK");
        }

        if (!logical.empty() && logical.front() == '*') {
            untagged.tokens = tokenize(
                std::string_view(logical).substr(1), literals);
            if (sink)
                sink(untagged);
        }
    }
}

bool ImapSession::connect(const std::string &host, std::uint16_t port,
                          long timeout_seconds) {
    if (transport_.connect(host, port, timeout_seconds) != NetError::None)
        return false;
    return begin_connected();
}

bool ImapSession::begin_connected() {
    std::string line;
    if (lines_.recv_line(line, 8192) != NetError::None)
        return false;
    greeting_ = std::string(strip_cr(line));
    if (greeting_.rfind("* PREAUTH", 0) == 0)
        preauth_ = true;
    else if (greeting_.rfind("* OK", 0) != 0)
        return false;
    connected_ = true;
    caps_valid_ = false;
    return true;
}

const std::vector<std::string> &ImapSession::capabilities() {
    if (caps_valid_)
        return caps_;
    caps_.clear();
    run_command("CAPABILITY", [this](const ImapUntagged &u) {
        if (!u.tokens.empty() && u.tokens[0].is_atom("CAPABILITY"))
            for (std::size_t i = 1; i < u.tokens.size(); ++i)
                caps_.push_back(u.tokens[i].text);
    });
    caps_valid_ = true;
    return caps_;
}

bool ImapSession::has_capability(std::string_view name) {
    for (const auto &c : capabilities())
        if (iequals(c, name))
            return true;
    return false;
}

bool ImapSession::request_starttls() { return run_command("STARTTLS", nullptr); }

std::string ImapSession::quote_string(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

bool ImapSession::login(const std::string &user, const std::string &password) {
    if (preauth_)
        return true;

    // Prefer AUTHENTICATE with the strongest offered mechanism.
    SaslMechanism mech = SaslMechanism::None;
    for (const auto &c : capabilities())
        if (c.size() > 5 && iequals(std::string_view(c).substr(0, 5), "AUTH="))
            mech = sasl_consider(mech, std::string_view(c).substr(5));

    if (mech != SaslMechanism::None) {
        current_tag_ = next_tag();
        if (!send_line(current_tag_ + " AUTHENTICATE " +
                       std::string(sasl_name(mech))))
            return false;
        int round = 0;
        for (;;) {
            std::string line;
            if (lines_.recv_line(line, 8192) != NetError::None && line.empty())
                return false;
            const std::string_view body = strip_cr(line);
            if (!body.empty() && body.front() == '+') {
                ++round;
                const std::string_view challenge =
                    body.size() > 2 ? body.substr(2) : "";
                std::string response;
                switch (mech) {
                case SaslMechanism::CramMD5:
                    response = sasl_cram_md5_response(user, password, challenge);
                    break;
                case SaslMechanism::Plain:
                    response = sasl_plain_response("", user, password);
                    break;
                case SaslMechanism::Login:
                    response = round == 1 ? sasl_login_user(user)
                                          : sasl_login_password(password);
                    break;
                default:
                    break;
                }
                if (response.empty())
                    response = "*";
                if (!send_line(response))
                    return false;
                continue;
            }
            if (body.rfind(current_tag_ + " ", 0) == 0) {
                last_tagged_ = std::string(body.substr(current_tag_.size() + 1));
                if (last_tagged_.rfind("OK", 0) == 0) {
                    caps_valid_ = false;
                    return true;
                }
                break; // fall through to LOGIN
            }
            // untagged noise during auth: ignore
        }
    }

    const bool ok = run_command(
        "LOGIN " + quote_string(user) + " " + quote_string(password), nullptr);
    if (ok)
        caps_valid_ = false;
    return ok;
}

bool ImapSession::logout() {
    const bool ok = run_command("LOGOUT", nullptr);
    transport_.disconnect();
    connected_ = false;
    return ok;
}

void ImapSession::handle_select_response(const ImapUntagged &u,
                                         ImapMailboxInfo &info) {
    const auto &t = u.tokens;
    if (t.size() >= 2 && t[1].is_atom("EXISTS")) {
        info.exists = std::atol(t[0].text.c_str());
    } else if (t.size() >= 2 && t[1].is_atom("RECENT")) {
        info.recent = std::atol(t[0].text.c_str());
    } else if (!t.empty() && t[0].is_atom("FLAGS") && t.size() >= 2 &&
               t[1].kind == ImapToken::Kind::List) {
        info.flags.clear();
        for (const auto &f : t[1].items)
            info.flags.push_back(f.text);
    } else if (!t.empty() && t[0].is_atom("OK") && t.size() >= 2 &&
               t[1].kind == ImapToken::Kind::List && !t[1].items.empty()) {
        const auto &code = t[1].items;
        if (code[0].is_atom("UIDVALIDITY") && code.size() >= 2)
            info.uid_validity =
                static_cast<std::uint32_t>(std::strtoul(code[1].text.c_str(), nullptr, 10));
        else if (code[0].is_atom("UIDNEXT") && code.size() >= 2)
            info.uid_next =
                static_cast<std::uint32_t>(std::strtoul(code[1].text.c_str(), nullptr, 10));
        else if (code[0].is_atom("UNSEEN") && code.size() >= 2)
            info.unseen = std::atol(code[1].text.c_str());
        else if (code[0].is_atom("PERMANENTFLAGS") && code.size() >= 2 &&
                 code[1].kind == ImapToken::Kind::List) {
            for (const auto &f : code[1].items)
                info.permanent_flags.push_back(f.text);
        }
    }
}

bool ImapSession::select(const std::string &mailbox, ImapMailboxInfo &info) {
    info = ImapMailboxInfo{};
    info.name = mailbox;
    const bool ok = run_command(
        "SELECT " + quote_string(mailbox),
        [&](const ImapUntagged &u) { handle_select_response(u, info); });
    if (ok && last_tagged_.find("READ-ONLY") != std::string::npos)
        info.read_only = true;
    return ok;
}

bool ImapSession::examine(const std::string &mailbox, ImapMailboxInfo &info) {
    info = ImapMailboxInfo{};
    info.name = mailbox;
    info.read_only = true;
    return run_command(
        "EXAMINE " + quote_string(mailbox),
        [&](const ImapUntagged &u) { handle_select_response(u, info); });
}

bool ImapSession::close_mailbox() { return run_command("CLOSE", nullptr); }

std::optional<std::vector<ImapListEntry>>
ImapSession::list(const std::string &reference, const std::string &pattern) {
    std::vector<ImapListEntry> entries;
    const bool ok = run_command(
        "LIST " + quote_string(reference) + " " + quote_string(pattern),
        [&](const ImapUntagged &u) {
            const auto &t = u.tokens;
            if (t.size() >= 4 && t[0].is_atom("LIST") &&
                t[1].kind == ImapToken::Kind::List) {
                ImapListEntry e;
                for (const auto &a : t[1].items)
                    e.attributes.push_back(a.text);
                e.delimiter = t[2].kind == ImapToken::Kind::Nil || t[2].text.empty()
                                  ? '\0'
                                  : t[2].text[0];
                e.name = t[3].text;
                entries.push_back(std::move(e));
            }
        });
    if (!ok)
        return std::nullopt;
    return entries;
}

bool ImapSession::create_mailbox(const std::string &name) {
    return run_command("CREATE " + quote_string(name), nullptr);
}
bool ImapSession::delete_mailbox(const std::string &name) {
    return run_command("DELETE " + quote_string(name), nullptr);
}
bool ImapSession::rename_mailbox(const std::string &from, const std::string &to) {
    return run_command("RENAME " + quote_string(from) + " " + quote_string(to),
                       nullptr);
}

std::optional<std::vector<ImapFetchResult>>
ImapSession::uid_fetch(const std::string &uid_set, const std::string &items) {
    std::vector<ImapFetchResult> results;
    const bool ok = run_command(
        "UID FETCH " + uid_set + " " + items, [&](const ImapUntagged &u) {
            const auto &t = u.tokens;
            if (t.size() < 3 || !t[1].is_atom("FETCH") ||
                t[2].kind != ImapToken::Kind::List)
                return;
            ImapFetchResult r;
            r.sequence = std::atol(t[0].text.c_str());
            const auto &kv = t[2].items;
            for (std::size_t i = 0; i + 1 < kv.size(); i += 2) {
                const ImapToken &key = kv[i];
                const ImapToken &val = kv[i + 1];
                if (key.is_atom("UID")) {
                    r.uid = static_cast<std::uint32_t>(
                        std::strtoul(val.text.c_str(), nullptr, 10));
                } else if (key.is_atom("FLAGS") &&
                           val.kind == ImapToken::Kind::List) {
                    for (const auto &f : val.items)
                        r.flags.push_back(f.text);
                } else if (key.is_atom("RFC822.SIZE")) {
                    r.rfc822_size = std::atol(val.text.c_str());
                } else if (key.is_atom("INTERNALDATE")) {
                    r.internal_date = val.text;
                } else if (key.kind == ImapToken::Kind::Atom &&
                           (iequals(key.text, "BODY[]") ||
                            iequals(key.text, "RFC822") ||
                            iequals(key.text, "BODY[TEXT]"))) {
                    r.body = val.text;
                } else if (key.kind == ImapToken::Kind::Atom &&
                           (iequals(key.text, "BODY[HEADER]") ||
                            iequals(key.text, "RFC822.HEADER"))) {
                    r.header_text = val.text;
                }
            }
            results.push_back(std::move(r));
        });
    if (!ok)
        return std::nullopt;
    return results;
}

bool ImapSession::uid_store(const std::string &uid_set, const std::string &mode,
                            const std::string &flags) {
    return run_command("UID STORE " + uid_set + " " + mode + " (" + flags + ")",
                       nullptr);
}

std::optional<std::vector<std::uint32_t>>
ImapSession::uid_search(const std::string &criteria) {
    std::vector<std::uint32_t> uids;
    const bool ok = run_command(
        "UID SEARCH " + criteria, [&](const ImapUntagged &u) {
            if (!u.tokens.empty() && u.tokens[0].is_atom("SEARCH"))
                for (std::size_t i = 1; i < u.tokens.size(); ++i)
                    uids.push_back(static_cast<std::uint32_t>(
                        std::strtoul(u.tokens[i].text.c_str(), nullptr, 10)));
        });
    if (!ok)
        return std::nullopt;
    return uids;
}

bool ImapSession::append(const std::string &mailbox, const std::string &flags,
                         const std::string &message) {
    std::string args = "APPEND " + quote_string(mailbox);
    if (!flags.empty())
        args += " (" + flags + ")";
    args += " {" + std::to_string(message.size()) + "}";
    return run_command(args, nullptr, &message);
}

bool ImapSession::expunge() { return run_command("EXPUNGE", nullptr); }
bool ImapSession::noop() { return run_command("NOOP", nullptr); }

std::uint8_t imap_flags_to_state(const std::vector<std::string> &flags) {
    bool seen = false, answered = false;
    for (const auto &f : flags) {
        if (iequals(f, "\\Seen"))
            seen = true;
        else if (iequals(f, "\\Answered"))
            answered = true;
    }
    if (answered)
        return static_cast<std::uint8_t>(MessageState::Replied);
    if (seen)
        return static_cast<std::uint8_t>(MessageState::Read);
    return static_cast<std::uint8_t>(MessageState::Unread);
}

} // namespace eudora
