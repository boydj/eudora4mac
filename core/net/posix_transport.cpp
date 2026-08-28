#include "net/posix_transport.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace eudora {

namespace {
constexpr int kPollSliceMs = 250; // cancellation latency bound
}

PosixTransport::~PosixTransport() {
    if (fd_ >= 0)
        ::close(fd_);
}

NetError PosixTransport::connect(const std::string &host, std::uint16_t port,
                                 long timeout_seconds) {
    disconnect();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    const std::string port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0)
        return last_error_ = NetError::ConnectFailed;

    NetError err = NetError::ConnectFailed;
    for (addrinfo *ai = res; ai; ai = ai->ai_next) {
        const int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

        int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            // Wait for completion in cancellable slices.
            long waited_ms = 0;
            const long limit_ms = timeout_seconds > 0 ? timeout_seconds * 1000 : 60000;
            for (;;) {
                if (cancelled()) {
                    err = NetError::Cancelled;
                    break;
                }
                pollfd p{fd, POLLOUT, 0};
                const int pr = ::poll(&p, 1, kPollSliceMs);
                if (pr > 0) {
                    int so_err = 0;
                    socklen_t len = sizeof(so_err);
                    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len);
                    rc = so_err == 0 ? 0 : -1;
                    break;
                }
                waited_ms += kPollSliceMs;
                if (waited_ms >= limit_ms) {
                    err = NetError::Timeout;
                    rc = -1;
                    break;
                }
            }
        }
        if (rc == 0) {
            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            fd_ = fd;
            ::freeaddrinfo(res);
            return last_error_ = NetError::None;
        }
        ::close(fd);
        if (err == NetError::Cancelled)
            break;
    }
    ::freeaddrinfo(res);
    return last_error_ = err;
}

NetError PosixTransport::send(std::string_view data) {
    if (fd_ < 0)
        return last_error_ = NetError::Closed;
    std::size_t sent = 0;
    while (sent < data.size()) {
        if (cancelled())
            return last_error_ = NetError::Cancelled;
        pollfd p{fd_, POLLOUT, 0};
        const int pr = ::poll(&p, 1, kPollSliceMs);
        if (pr < 0 && errno != EINTR)
            return last_error_ = NetError::IoError;
        if (pr <= 0)
            continue;
        const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent,
#ifdef MSG_NOSIGNAL
                                 MSG_NOSIGNAL
#else
                                 0
#endif
        );
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            return last_error_ = NetError::IoError;
        }
        sent += static_cast<std::size_t>(n);
    }
    return last_error_ = NetError::None;
}

long PosixTransport::recv(char *buffer, long max) {
    if (fd_ < 0) {
        last_error_ = NetError::Closed;
        return -1;
    }
    long waited_ms = 0;
    for (;;) {
        if (cancelled()) {
            last_error_ = NetError::Cancelled;
            return -1;
        }
        pollfd p{fd_, POLLIN, 0};
        const int pr = ::poll(&p, 1, kPollSliceMs);
        if (pr < 0 && errno != EINTR) {
            last_error_ = NetError::IoError;
            return -1;
        }
        if (pr <= 0) {
            waited_ms += kPollSliceMs;
            if (recv_timeout_ > 0 && waited_ms >= recv_timeout_ * 1000) {
                last_error_ = NetError::Timeout;
                return -1;
            }
            continue;
        }
        const ssize_t n = ::recv(fd_, buffer, static_cast<std::size_t>(max), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            last_error_ = NetError::IoError;
            return -1;
        }
        if (n == 0)
            last_error_ = NetError::Closed;
        return static_cast<long>(n);
    }
}

NetError PosixTransport::disconnect() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
    return NetError::None;
}

std::string PosixTransport::local_host_name() {
    // WhoAmI: prefer the reverse name of our side of the connection, fall
    // back to gethostname, then to a domain literal.
    if (fd_ >= 0) {
        sockaddr_storage ss{};
        socklen_t len = sizeof(ss);
        if (::getsockname(fd_, reinterpret_cast<sockaddr *>(&ss), &len) == 0) {
            char host[NI_MAXHOST];
            if (::getnameinfo(reinterpret_cast<sockaddr *>(&ss), len, host,
                              sizeof(host), nullptr, 0, NI_NAMEREQD) == 0)
                return host;
            if (::getnameinfo(reinterpret_cast<sockaddr *>(&ss), len, host,
                              sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0)
                return std::string("[") + host + "]";
        }
    }
    char name[256];
    if (::gethostname(name, sizeof(name)) == 0) {
        name[sizeof(name) - 1] = '\0';
        return name;
    }
    return "localhost";
}

void PosixTransport::flush_input(long timeout_seconds) {
    // Drain whatever arrives within the window (OTFlushInput).
    if (fd_ < 0)
        return;
    char scratch[512];
    long waited_ms = 0;
    const long limit_ms = timeout_seconds > 0 ? timeout_seconds * 1000 : 1000;
    while (waited_ms < limit_ms && !cancelled()) {
        pollfd p{fd_, POLLIN, 0};
        const int pr = ::poll(&p, 1, kPollSliceMs);
        if (pr > 0) {
            const ssize_t n = ::recv(fd_, scratch, sizeof(scratch), 0);
            if (n <= 0)
                return;
        } else {
            waited_ms += kPollSliceMs;
        }
    }
}

} // namespace eudora
