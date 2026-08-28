#include "net/apple_tls_transport.hpp"

#if defined(__APPLE__)

// SecureTransport is deprecated (see the header for why it is still the
// right tool here); silence the deprecation noise for this one file.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <Security/SecureTransport.h>

#include <cstdio>

namespace eudora {

namespace {

SSLContextRef ssl_ctx(void *p) { return static_cast<SSLContextRef>(p); }

// SSLReadFunc: on entry *dataLength is the requested count; report what was
// actually delivered.  Partial data (or none yet) is errSSLWouldBlock and
// SecureTransport calls again.
OSStatus tls_read(SSLConnectionRef connection, void *data, size_t *dataLength) {
    auto *sub = static_cast<Transport *>(const_cast<void *>(connection));
    const size_t requested = *dataLength;
    size_t got = 0;
    char *out = static_cast<char *>(data);
    while (got < requested) {
        const long n = sub->recv(out + got, static_cast<long>(requested - got));
        if (n > 0) {
            got += static_cast<size_t>(n);
            continue;
        }
        *dataLength = got;
        if (n == 0)
            return got ? errSSLWouldBlock : errSSLClosedGraceful;
        return sub->last_error() == NetError::Cancelled ? errSSLClosedAbort
               : got                                    ? errSSLWouldBlock
                                                        : errSSLClosedAbort;
    }
    *dataLength = got;
    return noErr;
}

OSStatus tls_write(SSLConnectionRef connection, const void *data,
                   size_t *dataLength) {
    auto *sub = static_cast<Transport *>(const_cast<void *>(connection));
    const std::string_view bytes(static_cast<const char *>(data), *dataLength);
    if (sub->send(bytes) != NetError::None) {
        *dataLength = 0;
        return errSSLClosedAbort;
    }
    return noErr;
}

std::string status_string(OSStatus status) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "SecureTransport error %d",
                  static_cast<int>(status));
    return buf;
}

} // namespace

AppleTlsTransport::~AppleTlsTransport() { teardown(); }

void AppleTlsTransport::teardown() {
    if (ctx_) {
        CFRelease(ssl_ctx(ctx_));
        ctx_ = nullptr;
    }
}

NetError AppleTlsTransport::connect(const std::string &host, std::uint16_t port,
                                    long timeout_seconds) {
    teardown();
    return sub_.connect(host, port, timeout_seconds);
}

NetError AppleTlsTransport::start_tls(const std::string &host) {
    teardown();
    SSLContextRef ctx = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
    if (!ctx) {
        tls_error_ = "SSLCreateContext failed";
        return last_error_ = NetError::TlsError;
    }
    ctx_ = ctx;

    OSStatus status = SSLSetIOFuncs(ctx, tls_read, tls_write);
    if (status == noErr)
        status = SSLSetConnection(ctx, static_cast<SSLConnectionRef>(
                                           static_cast<void *>(&sub_)));
    if (status == noErr)
        status = SSLSetPeerDomainName(ctx, host.c_str(), host.size());
    if (status == noErr)
        status = SSLSetProtocolVersionMin(ctx, kTLSProtocol12);

    if (status == noErr) {
        do {
            status = SSLHandshake(ctx);
        } while (status == errSSLWouldBlock);
    }

    if (status != noErr) {
        tls_error_ = "TLS handshake failed: " + status_string(status);
        teardown();
        return last_error_ = NetError::TlsError;
    }
    return last_error_ = NetError::None;
}

NetError AppleTlsTransport::send(std::string_view data) {
    if (!ctx_)
        return sub_.send(data); // passthrough before STARTTLS
    size_t sent = 0;
    while (sent < data.size()) {
        if (cancelled() || sub_.cancelled())
            return last_error_ = NetError::Cancelled;
        size_t processed = 0;
        const OSStatus status = SSLWrite(ssl_ctx(ctx_), data.data() + sent,
                                         data.size() - sent, &processed);
        sent += processed;
        if (status == noErr || status == errSSLWouldBlock)
            continue;
        tls_error_ = status_string(status);
        return last_error_ = NetError::TlsError;
    }
    return last_error_ = NetError::None;
}

long AppleTlsTransport::recv(char *buffer, long max) {
    if (!ctx_)
        return sub_.recv(buffer, max);
    for (;;) {
        if (cancelled() || sub_.cancelled()) {
            last_error_ = NetError::Cancelled;
            return -1;
        }
        size_t processed = 0;
        const OSStatus status =
            SSLRead(ssl_ctx(ctx_), buffer, static_cast<size_t>(max), &processed);
        if (processed > 0)
            return static_cast<long>(processed);
        if (status == errSSLWouldBlock)
            continue; // the blocking sub-transport will deliver bytes
        if (status == errSSLClosedGraceful || status == errSSLClosedNoNotify) {
            last_error_ = NetError::Closed;
            return 0;
        }
        tls_error_ = status_string(status);
        last_error_ = sub_.last_error() == NetError::Cancelled
                          ? NetError::Cancelled
                          : NetError::TlsError;
        return -1;
    }
}

NetError AppleTlsTransport::disconnect() {
    if (ctx_)
        SSLClose(ssl_ctx(ctx_));
    teardown();
    return sub_.disconnect();
}

NetError AppleTlsTransport::last_error() const {
    return last_error_ != NetError::None ? last_error_ : sub_.last_error();
}

} // namespace eudora

#pragma clang diagnostic pop

#endif // __APPLE__
