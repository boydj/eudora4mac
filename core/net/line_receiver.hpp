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
    explicit LineReceiver(Transport &transport, std::size_t buffer_size = 4096)
        : transport_(transport), buffer_(buffer_size, '\0') {}

    // Read one line into `line` (at most max_len bytes).  The terminator
    // appears as a trailing '\r'.  Returns NetError::None on success; a line
    // may still be returned on Closed (the final unterminated fragment).
    NetError recv_line(std::string &line, std::size_t max_len = 4096);

private:
    Transport &transport_;
    std::string buffer_;
    long filled_ = 0;
    long spot_ = -1; // -1: buffer empty (RcvSpot semantics)
};

} // namespace eudora
