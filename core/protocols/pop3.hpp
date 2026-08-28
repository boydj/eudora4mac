// POP3 client — the modern protocol half of pop.c.
//
// The RFC 1939 state machine (StartPOP / POPIntroductions / PopCapabilities
// / POPCmdGetReply / ReadPOPLine / FillSizesWithList / FillWithUidl /
// POPByeBye), driven over the Transport abstraction.  The message *decoder*
// half of pop.c (attachment files, resource forks) was storage code, not
// protocol, and is not part of this class.

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

struct Pop3Capabilities {
    bool capa = false; // server answered CAPA at all
    bool top = false;
    bool pipelining = false;
    bool user = false;
    bool expire = false;
    bool uidl = false;
    bool stls = false;
    bool auth_resp_code = false;
    SaslMechanism sasl = SaslMechanism::None;
};

enum class Pop3Auth {
    Automatic, // SASL if offered, else USER/PASS
    UserPass,
    Apop,
    Sasl,
};

class Pop3Session {
public:
    explicit Pop3Session(Transport &transport)
        : transport_(transport), lines_(transport) {}

    // Connect and read the greeting banner (StartPOP + the banner loop of
    // POPIntroductions).  Returns false on failure; last_response() has the
    // server text.
    bool connect(const std::string &host, std::uint16_t port,
                 long timeout_seconds = 45);

    // For transports connected (and possibly TLS-wrapped) externally:
    // just read the greeting banner.
    bool begin_connected();

    // CAPA (PopCapabilities).
    const Pop3Capabilities &query_capabilities();

    // STLS: issue the command; on +OK the caller runs the TLS handshake on
    // the transport and then MUST call rescan_capabilities().  On failure
    // the input is flushed (the Cyrus workaround, pop.c:532).
    bool request_stls();
    // Drain the bogus reply Cyrus queues when TLS fails to engage after a
    // successful STLS (call only in that case, pop.c:527-533).
    void flush_after_failed_tls();
    void rescan_capabilities() { query_capabilities(); }

    // Log in (POPIntroductions' auth ladder).
    bool login(const std::string &user, const std::string &password,
               Pop3Auth method = Pop3Auth::Automatic);

    // STAT: message count and total size.
    bool stat(long &count, long &total_size);
    // LIST (FillSizesWithList): message number -> size.
    bool list(std::map<long, long> &sizes);
    // UIDL (FillWithUidl): message number -> unique id.
    bool uidl(std::map<long, std::string> &uids);
    // LAST (POPLast).
    std::optional<long> last();

    // RETR/TOP.  Each line of the message is delivered to `sink` with the
    // POP transparency dot removed and envelope "From " lines escaped to
    // ">From ", exactly as ReadPOPLine did; lines end in '\r'.
    bool retrieve(long message, const std::function<void(std::string_view)> &sink);
    bool top(long message, long body_lines,
             const std::function<void(std::string_view)> &sink);

    bool dele(long message);
    bool rset();
    // QUIT + orderly close (POPByeBye/EndPOP).
    bool quit();

    const std::string &last_response() const { return last_response_; }
    const std::string &greeting() const { return greeting_; }
    NetError last_net_error() const { return transport_.last_error(); }

private:
    bool send_command(const std::string &line);
    // POPCmdGetReply/POPGetReplyLo: reads until a +/- status line arrives,
    // skipping echoes and stray blank lines (the errChar dance).
    bool command(const std::string &line);
    bool read_message_body(const std::function<void(std::string_view)> &sink);

    Transport &transport_;
    LineReceiver lines_;
    Pop3Capabilities caps_;
    bool caps_valid_ = false;
    std::string greeting_;
    std::string last_response_;
    bool connected_ = false;
};

} // namespace eudora
