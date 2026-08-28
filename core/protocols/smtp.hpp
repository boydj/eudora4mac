// SMTP client — the modern protocol half of sendmail.c.
//
// StartSMTP / DoIntroductions / EhloLine / DoSMTPAuth / SendCmdGetReply /
// GetReplyLo, over the Transport abstraction.  The MIME composer half of
// sendmail.c (attachments, PICT/QuickTime, styled text) was message
// generation, not protocol, and is not part of this class.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/line_receiver.hpp"
#include "net/transport.hpp"
#include "protocols/sasl.hpp"

namespace eudora {

// EhloStuff (sendmail.c EhloLine).
struct SmtpExtensions {
    bool esmtp = false; // EHLO accepted
    long max_size = 0;  // SIZE (0 = unadvertised)
    bool mime8bit = false;
    bool pipelining = false;
    bool starttls = false;
    SaslMechanism sasl = SaslMechanism::None;
};

class SmtpSession {
public:
    explicit SmtpSession(Transport &transport)
        : transport_(transport), lines_(transport) {}

    // Connect, read greeting, EHLO (falling back to HELO on 4xx/5xx, as
    // DoIntroductions did).  helo_name defaults to the transport's idea of
    // our host name (WhoAmI).
    bool connect(const std::string &host, std::uint16_t port,
                 long timeout_seconds = 45, const std::string &helo_name = "");

    const SmtpExtensions &extensions() const { return ext_; }

    // STARTTLS: issue the command; on 2xx the caller runs the handshake and
    // MUST call ehlo_again() (RFC 3207 requires a fresh EHLO).
    bool request_starttls();
    bool ehlo_again();

    // AUTH via the strongest advertised mechanism (DoSMTPAuth).
    bool auth(const std::string &user, const std::string &password);

    // Envelope + data.  `data` must be CRLF-or-CR line oriented; dot
    // stuffing and the final ".\r\n" are applied here.
    bool mail_from(const std::string &address, long size_estimate = 0);
    bool rcpt_to(const std::string &address);
    bool data(std::string_view message);
    bool rset();
    bool quit();

    // Last reply code (SMErrEnum-style; 601 transmission error, 602 receive
    // error, matching the original's synthetic codes).
    int last_code() const { return last_code_; }
    const std::string &last_reply() const { return last_reply_; }

private:
    int send_command(const std::string &cmd);
    // GetReplyLo: skip non-numeric noise, follow "250-" continuations,
    // parse EHLO extension lines, map 452 to 552.
    int get_reply(bool is_ehlo);
    void parse_ehlo_line(std::string_view line);

    Transport &transport_;
    LineReceiver lines_;
    SmtpExtensions ext_;
    std::string helo_name_;
    int last_code_ = 0;
    std::string last_reply_;
    bool connected_ = false;
};

} // namespace eudora
