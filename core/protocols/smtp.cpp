#include "protocols/smtp.hpp"

#include <cctype>
#include <cstdlib>

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

} // namespace

int SmtpSession::send_command(const std::string &cmd) {
    if (transport_.send(cmd + "\r\n") != NetError::None)
        return last_code_ = 601; // TransErr
    return 0;
}

void SmtpSession::parse_ehlo_line(std::string_view line) {
    // EhloLine (sendmail.c:548): "250-DIRECTIVE value...".
    line = strip_cr(line);
    if (line.size() < 4)
        return;
    line.remove_prefix(4); // code + space/dash
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    std::size_t sp = 0;
    while (sp < line.size() && line[sp] != ' ' && line[sp] != '\t')
        ++sp;
    const std::string_view directive = line.substr(0, sp);
    std::string_view value = sp < line.size() ? line.substr(sp + 1) : "";

    // "AUTH=..." nonsense: treat like AUTH (sendmail.c:563-567).
    std::string_view fixed_directive = directive;
    if (directive.size() > 5 && iequals(directive.substr(0, 5), "AUTH=")) {
        fixed_directive = directive.substr(0, 4);
        value = directive.substr(5);
    }

    if (iequals(fixed_directive, "SIZE")) {
        ext_.max_size = 0x7FFFFFFF;
        if (!value.empty() && value.size() <= 9) {
            const long v = std::atol(std::string(value).c_str());
            if (v > 1024)
                ext_.max_size = v;
        }
    } else if (iequals(fixed_directive, "8BITMIME")) {
        ext_.mime8bit = true;
    } else if (iequals(fixed_directive, "PIPELINING")) {
        ext_.pipelining = true;
    } else if (iequals(fixed_directive, "STARTTLS")) {
        ext_.starttls = true;
    } else if (iequals(fixed_directive, "AUTH")) {
        // Space-separated mechanism list.
        std::size_t i = 0;
        while (i < value.size()) {
            while (i < value.size() && (value[i] == ' ' || value[i] == '\t'))
                ++i;
            std::size_t j = i;
            while (j < value.size() && value[j] != ' ' && value[j] != '\t')
                ++j;
            if (j > i)
                ext_.sasl = sasl_consider(ext_.sasl, value.substr(i, j - i));
            i = j;
        }
    }
}

int SmtpSession::get_reply(bool is_ehlo) {
    // GetReplyLo (sendmail.c:4278): loop over lines; skip leading
    // non-printable noise; a line whose first three printable chars are
    // digits and whose fourth is not '-' ends the reply.
    last_reply_.clear();
    for (;;) {
        std::string line;
        const NetError err = lines_.recv_line(line);
        if (err != NetError::None && line.empty())
            return last_code_ = 602; // RecvErr
        if (!last_reply_.empty())
            last_reply_ += '\n';
        last_reply_ += strip_cr(line);

        // skip leading non-printables
        std::size_t cp = 0;
        while (cp < line.size() &&
               (static_cast<unsigned char>(line[cp]) < ' ' || line[cp] > '~'))
            ++cp;
        const std::string_view body = std::string_view(line).substr(cp);

        if (is_ehlo && body.size() >= 3 && body.compare(0, 3, "250") == 0)
            parse_ehlo_line(body);

        const std::string_view stripped = strip_cr(body);
        if (stripped.size() >= 3 &&
            std::isdigit(static_cast<unsigned char>(stripped[0])) &&
            std::isdigit(static_cast<unsigned char>(stripped[1])) &&
            std::isdigit(static_cast<unsigned char>(stripped[2])) &&
            (stripped.size() == 3 || stripped[3] != '-')) {
            int code = std::atoi(std::string(stripped.substr(0, 3)).c_str());
            // 452 is really a permanent failure (sendmail.c:4374).
            if (code == 452)
                code = 552;
            return last_code_ = code;
        }
    }
}

bool SmtpSession::connect(const std::string &host, std::uint16_t port,
                          long timeout_seconds, const std::string &helo_name) {
    if (transport_.connect(host, port, timeout_seconds) != NetError::None) {
        last_code_ = 601;
        return false;
    }
    return begin_connected(helo_name);
}

bool SmtpSession::begin_connected(const std::string &helo_name) {
    connected_ = true;
    helo_name_ = helo_name.empty() ? transport_.local_host_name() : helo_name;

    if (get_reply(false) / 100 != 2)
        return false;
    return ehlo_again();
}

bool SmtpSession::ehlo_again() {
    ext_ = {};
    if (send_command("EHLO " + helo_name_))
        return false;
    int code = get_reply(true);
    if (code >= 400) {
        // Fall back to HELO (DoIntroductions).
        if (send_command("HELO " + helo_name_))
            return false;
        code = get_reply(false);
        if (code / 100 != 2)
            return false;
        ext_.esmtp = false;
        return true;
    }
    if (code / 100 != 2)
        return false;
    ext_.esmtp = true;
    return true;
}

bool SmtpSession::request_starttls() {
    if (send_command("STARTTLS"))
        return false;
    return get_reply(false) / 100 == 2;
}

bool SmtpSession::auth(const std::string &user, const std::string &password) {
    const SaslMechanism mech = ext_.sasl;
    if (mech == SaslMechanism::None)
        return false;

    // Initial command; PLAIN uses the initial-response shortcut, as the
    // original did ("some servers insist on it", sasl.c:305).
    std::string initial = "AUTH " + std::string(sasl_name(mech));
    if (mech == SaslMechanism::Plain)
        initial += " " + sasl_plain_response("", user, password);
    if (send_command(initial))
        return false;

    int round = 0;
    for (;;) {
        const int code = get_reply(false);
        if (code / 100 == 2)
            return true;
        if (code / 100 != 3)
            return false;
        ++round;
        // Challenge is the base64 text after the code.
        std::string_view challenge = last_reply_;
        const auto sp = challenge.find(' ');
        challenge = sp == std::string_view::npos ? "" : challenge.substr(sp + 1);

        std::string response;
        switch (mech) {
        case SaslMechanism::CramMD5:
            response = sasl_cram_md5_response(user, password, challenge);
            break;
        case SaslMechanism::Plain:
            // Server ignored the initial response; resend bare.
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
            response = "*"; // SASL_CANCEL
        if (send_command(response))
            return false;
    }
}

bool SmtpSession::mail_from(const std::string &address, long size_estimate) {
    std::string cmd = "MAIL FROM:<" + address + ">";
    if (ext_.max_size && size_estimate)
        cmd += " SIZE=" + std::to_string(size_estimate);
    if (ext_.mime8bit)
        cmd += " BODY=8BITMIME";
    if (send_command(cmd))
        return false;
    return get_reply(false) / 100 == 2;
}

bool SmtpSession::rcpt_to(const std::string &address) {
    if (send_command("RCPT TO:<" + address + ">"))
        return false;
    int code = get_reply(false);
    // Normalize 5xx to 550 for recipients (SendCmdGetReply, sendmail.c:539).
    if (code > 499 && code < 600) {
        last_code_ = 550;
        return false;
    }
    return code / 100 == 2;
}

bool SmtpSession::data(std::string_view message) {
    if (send_command("DATA"))
        return false;
    if (get_reply(false) != 354)
        return false;

    // Send line by line with dot stuffing; accept CR, LF, or CRLF input.
    std::string wire;
    wire.reserve(message.size() + message.size() / 32 + 8);
    std::size_t i = 0;
    while (i < message.size()) {
        std::size_t end = i;
        while (end < message.size() && message[end] != '\r' && message[end] != '\n')
            ++end;
        std::string_view line = message.substr(i, end - i);
        if (!line.empty() && line.front() == '.')
            wire += '.'; // transparency
        wire += line;
        wire += "\r\n";
        if (end < message.size()) {
            if (message[end] == '\r' && end + 1 < message.size() &&
                message[end + 1] == '\n')
                ++end;
            ++end;
        }
        i = end;
        if (wire.size() >= 16384) {
            if (transport_.send(wire) != NetError::None) {
                last_code_ = 601;
                return false;
            }
            wire.clear();
        }
    }
    wire += ".\r\n";
    if (transport_.send(wire) != NetError::None) {
        last_code_ = 601;
        return false;
    }
    return get_reply(false) / 100 == 2;
}

bool SmtpSession::rset() {
    if (send_command("RSET"))
        return false;
    return get_reply(false) / 100 == 2;
}

bool SmtpSession::quit() {
    if (!connected_)
        return true;
    bool ok = true;
    if (!send_command("QUIT"))
        ok = get_reply(false) / 100 == 2;
    transport_.disconnect();
    connected_ = false;
    return ok;
}

} // namespace eudora
