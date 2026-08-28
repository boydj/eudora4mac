#include "protocols/pop3.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

#include "mail/mime_codec.hpp"
#include "mailstore/mbox_parser.hpp" // is_from_line

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

std::vector<std::string_view> split_ws(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        std::size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t')
            ++j;
        if (j > i)
            out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

} // namespace

bool Pop3Session::send_command(const std::string &line) {
    return transport_.send(line + "\r\n") == NetError::None;
}

bool Pop3Session::command(const std::string &line) {
    if (!line.empty() && !send_command(line))
        return false;
    // POPGetReplyLo: skip echoes/blank lines until a +/- line (errChar).
    last_response_.clear();
    for (;;) {
        std::string reply;
        const NetError err = lines_.recv_line(reply);
        if (err != NetError::None && reply.empty())
            return false;
        if (reply.empty())
            return false;
        if (reply.front() == '+' || reply.front() == '-') {
            last_response_ = std::string(strip_cr(reply));
            return last_response_.front() == '+';
        }
    }
}

bool Pop3Session::connect(const std::string &host, std::uint16_t port,
                          long timeout_seconds) {
    if (transport_.connect(host, port, timeout_seconds) != NetError::None)
        return false;
    return begin_connected();
}

bool Pop3Session::begin_connected() {
    // Read the banner; loop past any junk before +/- (POPIntroductions).
    if (!command(""))
        return false;
    greeting_ = last_response_;
    connected_ = true;
    return true;
}

const Pop3Capabilities &Pop3Session::query_capabilities() {
    caps_ = {};
    caps_valid_ = true;
    if (!command("CAPA"))
        return caps_;
    caps_.capa = true;
    // Multi-line list terminated by ".".
    for (;;) {
        std::string line;
        if (lines_.recv_line(line) != NetError::None)
            break;
        const std::string_view body = strip_cr(line);
        if (body == ".")
            break;
        const auto tokens = split_ws(body);
        if (tokens.empty())
            continue;
        const auto tag = tokens[0];
        if (iequals(tag, "TOP"))
            caps_.top = true;
        else if (iequals(tag, "PIPELINING"))
            caps_.pipelining = true;
        else if (iequals(tag, "USER"))
            caps_.user = true;
        else if (iequals(tag, "EXPIRE"))
            caps_.expire = true;
        else if (iequals(tag, "UIDL"))
            caps_.uidl = true;
        else if (iequals(tag, "STLS"))
            caps_.stls = true;
        else if (iequals(tag, "AUTH-RESP-CODE"))
            caps_.auth_resp_code = true;
        else if (iequals(tag, "SASL"))
            for (std::size_t i = 1; i < tokens.size(); ++i)
                caps_.sasl = sasl_consider(caps_.sasl, tokens[i]);
    }
    return caps_;
}

bool Pop3Session::request_stls() {
    // Just issue the command.  A -ERR here (STLS not offered) is NOT the
    // Cyrus case and must not flush — legacy only flushed after a +OK whose
    // TLS handshake then failed to engage (pop.c:514-532), which is the
    // caller's job once start_tls() fails.
    return command("STLS");
}

void Pop3Session::flush_after_failed_tls() {
    // Cyrus leaves a bogus reply queued when TLS did not engage after a
    // successful STLS; drain it (pop.c:527-533).
    transport_.flush_input(2);
}

bool Pop3Session::login(const std::string &user, const std::string &password,
                        Pop3Auth method) {
    if (!caps_valid_)
        query_capabilities();

    if (method == Pop3Auth::Automatic)
        method = caps_.sasl != SaslMechanism::None ? Pop3Auth::Sasl
                                                   : Pop3Auth::UserPass;

    switch (method) {
    case Pop3Auth::Apop: {
        const std::string digest = apop_digest(greeting_, password);
        if (digest.empty())
            return false; // no timestamp in banner
        return command("APOP " + user + " " + digest);
    }
    case Pop3Auth::Sasl: {
        const SaslMechanism mech = caps_.sasl;
        if (mech == SaslMechanism::None)
            return false;
        // AUTH <mech>, then challenge rounds ("+ <base64>").
        if (!send_command("AUTH " + std::string(sasl_name(mech))))
            return false;
        int round = 0;
        for (;;) {
            std::string reply;
            if (lines_.recv_line(reply) != NetError::None)
                return false;
            const std::string_view body = strip_cr(reply);
            if (body.empty())
                continue;
            if (body.front() == '+' && (body.size() == 1 || body[1] == ' ')) {
                // challenge
                ++round;
                std::string_view challenge = body.size() > 2 ? body.substr(2) : "";
                std::string response;
                if (mech == SaslMechanism::CramMD5) {
                    response = sasl_cram_md5_response(user, password, challenge);
                } else if (mech == SaslMechanism::Plain) {
                    response = sasl_plain_response("", user, password);
                } else if (mech == SaslMechanism::Login) {
                    response = round == 1 ? sasl_login_user(user)
                                          : sasl_login_password(password);
                }
                if (response.empty()) {
                    send_command("*"); // SASL_CANCEL
                    continue;
                }
                if (!send_command(response))
                    return false;
                continue;
            }
            last_response_ = std::string(body);
            return body.front() == '+';
        }
    }
    case Pop3Auth::UserPass:
    case Pop3Auth::Automatic:
        if (!command("USER " + user))
            return false;
        return command("PASS " + password);
    }
    return false;
}

bool Pop3Session::stat(long &count, long &total_size) {
    if (!command("STAT"))
        return false;
    // "+OK count size"
    const auto tokens = split_ws(last_response_);
    if (tokens.size() < 3)
        return false;
    count = std::atol(std::string(tokens[1]).c_str());
    total_size = std::atol(std::string(tokens[2]).c_str());
    return true;
}

bool Pop3Session::list(std::map<long, long> &sizes) {
    if (!command("LIST"))
        return false;
    for (;;) {
        std::string line;
        if (lines_.recv_line(line) != NetError::None)
            return false;
        const std::string_view body = strip_cr(line);
        if (body == ".")
            break;
        const auto tokens = split_ws(body);
        if (tokens.size() >= 2)
            sizes[std::atol(std::string(tokens[0]).c_str())] =
                std::atol(std::string(tokens[1]).c_str());
    }
    return true;
}

bool Pop3Session::uidl(std::map<long, std::string> &uids) {
    if (!command("UIDL"))
        return false;
    for (;;) {
        std::string line;
        if (lines_.recv_line(line) != NetError::None)
            return false;
        const std::string_view body = strip_cr(line);
        if (body == ".")
            break;
        const auto tokens = split_ws(body);
        if (tokens.size() >= 2)
            uids[std::atol(std::string(tokens[0]).c_str())] =
                std::string(tokens[1]);
    }
    return true;
}

std::optional<long> Pop3Session::last() {
    if (!command("LAST"))
        return std::nullopt;
    const auto tokens = split_ws(last_response_);
    if (tokens.size() < 2)
        return std::nullopt;
    return std::atol(std::string(tokens[1]).c_str());
}

bool Pop3Session::read_message_body(
    const std::function<void(std::string_view)> &sink) {
    // ReadPOPLine semantics (pop.c:2189-2263): the transparency dot is
    // removed, ".\r" ends the message, and sendmail "From " envelopes are
    // escaped with '>'.  Partial (over-long) lines pass through unmangled.
    bool was_nl = true;
    for (;;) {
        std::string line;
        const NetError err = lines_.recv_line(line);
        if (err != NetError::None && line.empty())
            return false;
        if (was_nl) {
            if (!line.empty() && line.front() == '.') {
                if (line.size() > 1 && line[1] == '.') {
                    line.erase(0, 1); // doubled dot: data
                } else if (line.size() >= 1 &&
                           (line.size() == 1 || line[1] == '\r')) {
                    return true; // end of message
                }
            } else if (is_from_line(line)) {
                line.insert(line.begin(), '>');
            }
        }
        was_nl = line.empty() || line.back() == '\r';
        sink(line);
    }
}

bool Pop3Session::retrieve(long message,
                           const std::function<void(std::string_view)> &sink) {
    if (!command("RETR " + std::to_string(message)))
        return false;
    return read_message_body(sink);
}

bool Pop3Session::top(long message, long body_lines,
                      const std::function<void(std::string_view)> &sink) {
    if (!command("TOP " + std::to_string(message) + " " +
                 std::to_string(body_lines)))
        return false;
    return read_message_body(sink);
}

bool Pop3Session::dele(long message) {
    return command("DELE " + std::to_string(message));
}

bool Pop3Session::rset() { return command("RSET"); }

bool Pop3Session::quit() {
    if (!connected_)
        return true;
    const bool ok = command("QUIT");
    transport_.disconnect();
    connected_ = false;
    return ok;
}

} // namespace eudora
