// TLS decorator over Apple's Security framework (SecureTransport).
//
// The dependency-free TLS path for macOS builds without OpenSSL (the
// SwiftPM build): same decorator architecture as net/tls_transport.* —
// the TLS engine's I/O callbacks (SSLSetIOFuncs) call back into the
// wrapped Transport, so both immediate TLS and STARTTLS upgrades work.
//
// SecureTransport is deprecated in favor of Network.framework, but
// Network.framework cannot run TLS over an existing connection (no
// STARTTLS), which this engine requires; SecureTransport remains the one
// Apple API with the custom-I/O shape.  Certificate chains and hostnames
// are always verified (the framework's default evaluation).

#pragma once

#if defined(__APPLE__)

#include <string>

#include "net/transport.hpp"

namespace eudora {

class AppleTlsTransport : public Transport {
public:
    explicit AppleTlsTransport(Transport &sub) : sub_(sub) {}
    ~AppleTlsTransport() override;

    // Plain passthrough until start_tls() succeeds.
    NetError connect(const std::string &host, std::uint16_t port,
                     long timeout_seconds) override;
    NetError send(std::string_view data) override;
    long recv(char *buffer, long max) override;
    NetError disconnect() override;
    NetError last_error() const override;
    std::string local_host_name() override { return sub_.local_host_name(); }
    void flush_input(long timeout_seconds) override {
        sub_.flush_input(timeout_seconds);
    }

    // Run the TLS handshake over the established connection; `host` is used
    // for SNI and hostname verification.
    NetError start_tls(const std::string &host);
    bool tls_active() const { return ctx_ != nullptr; }
    std::string last_tls_error() const { return tls_error_; }

private:
    void teardown();

    Transport &sub_;
    void *ctx_ = nullptr; // SSLContextRef, kept opaque to this header
    std::string tls_error_;
    NetError last_error_ = NetError::None;
};

} // namespace eudora

#endif // __APPLE__
