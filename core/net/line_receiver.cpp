#include "net/line_receiver.hpp"

namespace eudora {

NetError LineReceiver::recv_line(std::string &line, std::size_t max_len) {
    line.clear();
    NetError err = NetError::None;

    while (line.size() < max_len) {
        if (spot_ >= 0) {
            // Drain buffered characters (ph.c:3071-3092).
            bool hit_newline = false;
            while (spot_ < filled_ && line.size() < max_len) {
                const char c = buffer_[static_cast<std::size_t>(spot_++)];
                if (c == '\0' || c == '\r')
                    continue; // NULs and bare CRs dropped
                if (c == '\n') {
                    line += '\r'; // LF terminates; canonicalize to CR
                    hit_newline = true;
                    break;
                }
                line += c;
            }
            if (spot_ >= filled_)
                spot_ = -1; // buffer emptied
            if (hit_newline || line.size() >= max_len)
                return NetError::None;
        } else {
            if (transport_.cancelled())
                return NetError::Cancelled;
            const long count =
                transport_.recv(buffer_.data(), static_cast<long>(buffer_.size()));
            if (count <= 0) {
                err = count == 0 ? NetError::Closed : transport_.last_error();
                if (err == NetError::None)
                    err = NetError::IoError;
                break;
            }
            // TREAT_BODY_CR_AS_CRLF (ph.c:3105-3121): bare CR from broken
            // servers becomes a line break.  Sloppy at buffer boundaries,
            // exactly like the original ("ASK ME IF I CARE!!!!").
            if (bare_cr_is_newline_)
                for (long i = 0; i < count - 1; ++i)
                    if (buffer_[static_cast<std::size_t>(i)] == '\r' &&
                        buffer_[static_cast<std::size_t>(i + 1)] != '\n')
                        buffer_[static_cast<std::size_t>(i)] = '\n';
            filled_ = count;
            spot_ = 0;
        }
    }

    return line.empty() ? err : NetError::None;
}

NetError LineReceiver::recv_bytes(std::string &out, std::size_t count) {
    out.clear();
    // Do not pre-allocate the full announced size: a caller passes a
    // server-controlled literal length here, and reserving it up front lets
    // a bogus (but capped) count commit a large block before any data
    // arrives.  Grow organically — reserve at most a bounded starter so the
    // common small case still avoids reallocation churn.
    out.reserve(std::min<std::size_t>(count, 64 * 1024));
    while (out.size() < count) {
        if (spot_ >= 0) {
            // NOTE: the buffered bytes have already been through the bare-CR
            // rewrite; raw literal reads should normally start with an empty
            // buffer (the {n} marker ends a line), so this only matters for
            // pathological pipelining.
            while (spot_ < filled_ && out.size() < count)
                out += buffer_[static_cast<std::size_t>(spot_++)];
            if (spot_ >= filled_)
                spot_ = -1;
        } else {
            if (transport_.cancelled())
                return NetError::Cancelled;
            const long n = transport_.recv(buffer_.data(),
                                           static_cast<long>(std::min(
                                               buffer_.size(),
                                               count - out.size())));
            if (n <= 0)
                return n == 0 ? NetError::Closed : transport_.last_error();
            out.append(buffer_.data(), static_cast<std::size_t>(n));
        }
    }
    return NetError::None;
}

} // namespace eudora
