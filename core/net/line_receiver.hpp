// Buffered network line reader — the modern NetRecvLine (ph.c:3055-3136).
//
// Reads CRLF-terminated protocol lines from a Transport.  Faithful to the
// original: NUL bytes and bare CRs are dropped, LF terminates a line and is
// rewritten as a single '\r', and (TREAT_BODY_CR_AS_CRLF, conf.h:150) a bare
// CR from broken servers is treated as a line terminator too.

#pragma once

#include <string>

#include "net/transport.hpp"

namespace eudora {

class LineReceiver {
public:
    // treat_bare_cr_as_newline: the TREAT_BODY_CR_AS_CRLF Exchange
    // workaround.  POP3/SMTP keep it on (as the original did); IMAP turns
    // it off because binary literals must pass through unmodified.
    explicit LineReceiver(Transport &transport, std::size_t buffer_size = 4096,
                          bool treat_bare_cr_as_newline = true)
        : transport_(transport), buffer_(buffer_size, '\0'),
          bare_cr_is_newline_(treat_bare_cr_as_newline) {}

    // Read one line into `line` (at most max_len bytes).  The terminator
    // appears as a trailing '\r'.  Returns NetError::None on success; a line
    // may still be returned on Closed (the final unterminated fragment).
    NetError recv_line(std::string &line, std::size_t max_len = 4096);

    // Read exactly `count` raw bytes (IMAP literals), draining any buffered
    // data first.  No newline translation is applied.
    NetError recv_bytes(std::string &out, std::size_t count);

private:
    Transport &transport_;
    std::string buffer_;
    bool bare_cr_is_newline_;
    long filled_ = 0;
    long spot_ = -1; // -1: buffer empty (RcvSpot semantics)
};

} // namespace eudora
