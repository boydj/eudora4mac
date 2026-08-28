// Network transport abstraction — the modern TransVector (mydefs.h:369-396).
//
// The legacy 11-slot function-pointer vector becomes an abstract class.
// Differences from the original, all deliberate:
//   - flush_input() is a first-class operation (pop.c:532 reached around the
//     vtable to call OTFlushInput directly; that leak is fixed here).
//   - Cancellation is a per-connection atomic token instead of the
//     thread-local CommandPeriod global.
//   - The event loop is NOT pumped inside reads; callers block on their own
//     thread (the original spun WaitNextEvent inside OTWaitForChars).

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace eudora {

enum class NetError : int {
    None = 0,
    Cancelled,     // userCancelled
    ConnectFailed, // openFailed / hostNotFound
    Closed,        // connectionClosed
    Timeout,       // commandTimeout
    IoError,       // everything else
    TlsError,
};

class Transport {
public:
    virtual ~Transport() = default;

    virtual NetError connect(const std::string &host, std::uint16_t port,
                             long timeout_seconds) = 0;
    // vSendTrans: send all bytes.
    virtual NetError send(std::string_view data) = 0;
    // vRecvTrans: receive up to max bytes; returns count, 0 on close,
    // negative on error (matching the classic driver's contract).
    virtual long recv(char *buffer, long max) = 0;
    // vDisTrans + vDestroyTrans collapse into one orderly shutdown.
    virtual NetError disconnect() = 0;
    // vTransError: last error on this stream.
    virtual NetError last_error() const = 0;
    // vWhoAmI: our name for HELO/EHLO.
    virtual std::string local_host_name() = 0;
    // Drain pending input (the Cyrus STARTTLS workaround, pop.c:532).
    virtual void flush_input(long timeout_seconds) = 0;

    // Cancellation token (replaces CommandPeriod).  The pointed-to flag may
    // be set from any thread; blocking operations return Cancelled soon
    // after.
    void set_cancel_flag(const std::atomic<bool> *flag) { cancel_ = flag; }
    bool cancelled() const { return cancel_ && cancel_->load(std::memory_order_relaxed); }

protected:
    const std::atomic<bool> *cancel_ = nullptr;
};

} // namespace eudora
