#include "net/tls_transport.hpp"

#if defined(EUDORA_HAVE_TLS)

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

namespace eudora {

namespace {

// The custom BIO over a Transport — the modern BIO_s_otsocket
// (ot_read/ot_write, OpenSSL.cp:171-233), minus the CFM opcode thunks.

int transport_bio_write(BIO *bio, const char *data, int len) {
    auto *t = static_cast<Transport *>(BIO_get_data(bio));
    if (!t || len <= 0)
        return -1;
    return t->send(std::string_view(data, static_cast<std::size_t>(len))) ==
                   NetError::None
               ? len
               : -1;
}

int transport_bio_read(BIO *bio, char *data, int len) {
    auto *t = static_cast<Transport *>(BIO_get_data(bio));
    if (!t || len <= 0)
        return -1;
    return static_cast<int>(t->recv(data, len));
}

long transport_bio_ctrl(BIO *, int cmd, long, void *) {
    // ot_ctrl (OpenSSL.cp:236): flush is a no-op success, everything else 0.
    return cmd == BIO_CTRL_FLUSH ? 1 : 0;
}

int transport_bio_create(BIO *bio) {
    BIO_set_init(bio, 1);
    return 1;
}

BIO_METHOD *transport_bio_method() {
    static BIO_METHOD *method = [] {
        BIO_METHOD *m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK,
                                     "eudora_transport");
        BIO_meth_set_write(m, transport_bio_write);
        BIO_meth_set_read(m, transport_bio_read);
        BIO_meth_set_ctrl(m, transport_bio_ctrl);
        BIO_meth_set_create(m, transport_bio_create);
        return m;
    }();
    return method;
}

std::string collect_openssl_errors() {
    std::string out;
    unsigned long e;
    char buf[256];
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        if (!out.empty())
            out += "; ";
        out += buf;
    }
    return out;
}

} // namespace

TlsTransport::~TlsTransport() { teardown(); }

void TlsTransport::teardown() {
    if (ssl_) {
        SSL_free(ssl_); // frees the BIO too
        ssl_ = nullptr;
    }
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

NetError TlsTransport::connect(const std::string &host, std::uint16_t port,
                               long timeout_seconds) {
    teardown();
    return sub_.connect(host, port, timeout_seconds);
}

NetError TlsTransport::start_tls(const std::string &host, bool verify) {
    teardown();
    ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ctx_) {
        tls_error_ = collect_openssl_errors();
        return last_error_ = NetError::TlsError;
    }
    SSL_CTX_set_default_verify_paths(ctx_);
    // Never negotiate below TLS 1.2, matching the Apple decorator
    // (apple_tls_transport.cpp's SSLSetProtocolVersionMin(kTLSProtocol12));
    // otherwise the linked library's default could still permit 1.0/1.1.
    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx_, verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);

    ssl_ = SSL_new(ctx_);
    if (!ssl_) {
        tls_error_ = collect_openssl_errors();
        teardown();
        return last_error_ = NetError::TlsError;
    }

    BIO *bio = BIO_new(transport_bio_method());
    BIO_set_data(bio, &sub_);
    SSL_set_bio(ssl_, bio, bio);

    SSL_set_tlsext_host_name(ssl_, host.c_str());
    if (verify) {
        SSL_set1_host(ssl_, host.c_str());
        SSL_set_hostflags(ssl_, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    }

    if (SSL_connect(ssl_) != 1) {
        tls_error_ = collect_openssl_errors();
        if (tls_error_.empty())
            tls_error_ = "TLS handshake failed";
        teardown();
        return last_error_ = NetError::TlsError;
    }
    return last_error_ = NetError::None;
}

NetError TlsTransport::send(std::string_view data) {
    if (!ssl_)
        return sub_.send(data); // passthrough before STARTTLS
    std::size_t sent = 0;
    while (sent < data.size()) {
        if (cancelled() || sub_.cancelled())
            return last_error_ = NetError::Cancelled;
        const int n = SSL_write(ssl_, data.data() + sent,
                                static_cast<int>(data.size() - sent));
        if (n <= 0) {
            tls_error_ = collect_openssl_errors();
            return last_error_ = NetError::TlsError;
        }
        sent += static_cast<std::size_t>(n);
    }
    return last_error_ = NetError::None;
}

long TlsTransport::recv(char *buffer, long max) {
    if (!ssl_)
        return sub_.recv(buffer, max);
    const int n = SSL_read(ssl_, buffer, static_cast<int>(max));
    if (n > 0)
        return n;
    const int reason = SSL_get_error(ssl_, n);
    if (reason == SSL_ERROR_ZERO_RETURN) {
        last_error_ = NetError::Closed;
        return 0;
    }
    tls_error_ = collect_openssl_errors();
    last_error_ = sub_.last_error() == NetError::Cancelled ? NetError::Cancelled
                                                           : NetError::TlsError;
    return -1;
}

NetError TlsTransport::disconnect() {
    if (ssl_)
        SSL_shutdown(ssl_);
    teardown();
    return sub_.disconnect();
}

NetError TlsTransport::last_error() const {
    return last_error_ != NetError::None ? last_error_ : sub_.last_error();
}

} // namespace eudora

#endif // EUDORA_HAVE_TLS
