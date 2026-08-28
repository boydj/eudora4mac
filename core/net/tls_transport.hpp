// TLS decorator transport — the modern ssl.c + OpenSSL.cp.
//
// Keeps the original architecture: a decorator wrapping any Transport, with
// OpenSSL doing its I/O through a custom BIO that calls back into the
// wrapped transport (BIO_s_otsocket, OpenSSL.cp:106-294).  All the CFM /
// Mach-O bridging, the CreateSSLBundle symlink hack, and the Keychain UI are
// gone — OpenSSL is linked directly.  Unlike the legacy single global
// ESSLSubTrans slot, each TlsTransport owns its own sub-transport reference.

#pragma once

#if defined(EUDORA_HAVE_TLS)

#include <memory>
#include <string>

#include "net/transport.hpp"

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct bio_st BIO;
typedef struct bio_method_st BIO_METHOD;

namespace eudora {

class TlsTransport : public Transport {
public:
    // Wraps (and does not own) the plain transport.
    explicit TlsTransport(Transport &sub) : sub_(sub) {}
    ~TlsTransport() override;

    // Plain passthrough until start_tls() succeeds (ESSLTrans semantics:
    // before esslSSLInUse, calls forward to the sub-transport).
    NetError connect(const std::string &host, std::uint16_t port,
                     long timeout_seconds) override;
    NetError send(std::string_view data) override;
    long recv(char *buffer, long max) override;
    NetError disconnect() override;
    NetError last_error() const override;
    std::string local_host_name() override { return sub_.local_host_name(); }
    void flush_input(long timeout_seconds) override { sub_.flush_input(timeout_seconds); }

    // Run the TLS handshake over the established connection (ESSLStartSSLLo).
    // `verify` enables certificate chain verification against the system
    // default store; `host` is used for SNI and hostname checking.
    NetError start_tls(const std::string &host, bool verify = true);
    bool tls_active() const { return ssl_ != nullptr; }
    std::string last_tls_error() const { return tls_error_; }

private:
    void teardown();

    Transport &sub_;
    SSL_CTX *ctx_ = nullptr;
    SSL *ssl_ = nullptr;
    std::string tls_error_;
    NetError last_error_ = NetError::None;
};

} // namespace eudora

#endif // EUDORA_HAVE_TLS
