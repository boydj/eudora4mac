// POSIX socket transport — the modern tcp.c (OTTCPTrans).
//
// Replaces ~2,600 lines of Open Transport endpoint management, async
// notifiers, and OT/PPP dial-up with getaddrinfo + blocking BSD sockets.
// Reads and writes poll in short slices so the cancellation token is honored
// (the original pumped WaitNextEvent inside OTWaitForChars for the same
// reason; here the UI stays on its own thread).

#pragma once

#include "net/transport.hpp"

namespace eudora {

class PosixTransport : public Transport {
public:
    PosixTransport() = default;
    ~PosixTransport() override;

    NetError connect(const std::string &host, std::uint16_t port,
                     long timeout_seconds) override;
    NetError send(std::string_view data) override;
    long recv(char *buffer, long max) override;
    NetError disconnect() override;
    NetError last_error() const override { return last_error_; }
    std::string local_host_name() override;
    void flush_input(long timeout_seconds) override;

    int fd() const { return fd_; }
    // Receive timeout for recv(); 0 disables (default).
    void set_recv_timeout(long seconds) { recv_timeout_ = seconds; }

private:
    int fd_ = -1;
    NetError last_error_ = NetError::None;
    long recv_timeout_ = 0;
};

} // namespace eudora
