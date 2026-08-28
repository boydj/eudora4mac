// Implementation of the C bridging interface (eudora/eudora_core.h).

#include "eudora/eudora_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "addressbook/nicknames.hpp"
#include "compat/macdate.hpp"
#include "filters/filter_file.hpp"
#include "filters/match_engine.hpp"
#include "mail/address_parser.hpp"
#include "mail/header_parser.hpp"
#include "mail/mime_codec.hpp"
#include "mailstore/compaction.hpp"
#include "mailstore/mbox_parser.hpp"
#include "mailstore/toc_io.hpp"
#include "net/posix_transport.hpp"
#if defined(EUDORA_HAVE_TLS)
#include "net/tls_transport.hpp"
#endif
#include "protocols/pop3.hpp"
#include "protocols/smtp.hpp"

using namespace eudora;

namespace {

thread_local std::string g_last_error;

void set_error(std::string msg) { g_last_error = std::move(msg); }

char *dup_string(std::string_view s) {
    char *out = static_cast<char *>(std::malloc(s.size() + 1));
    if (!out)
        return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

std::string read_file_range(const std::filesystem::path &path,
                            std::int64_t offset, std::int64_t length) {
    std::string out;
    std::FILE *f = std::fopen(path.string().c_str(), "rb");
    if (!f)
        return out;
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0 && length > 0) {
        out.resize(static_cast<std::size_t>(length));
        const std::size_t got = std::fread(out.data(), 1, out.size(), f);
        out.resize(got);
    }
    std::fclose(f);
    return out;
}

} // namespace

struct eudora_mailbox {
    TableOfContents toc;
    std::filesystem::path toc_file;
};

struct eudora_message {
    std::string raw;
    std::string body;
    HeaderSet headers;
    std::string boundary;
    std::string filename;
    // per-handle stable storage for returned decoded headers is malloc'd
};

struct eudora_filters {
    std::vector<Filter> filters;
};

struct eudora_addressbook {
    AddressBook book;
};

extern "C" {

const char *eudora_core_version(void) { return "0.1.0"; }

const char *eudora_last_error(void) { return g_last_error.c_str(); }

void eudora_string_free(char *s) { std::free(s); }

/* ---- mailboxes -------------------------------------------------------- */

eudora_mailbox *eudora_mailbox_open(const char *mbox_path) {
    if (!mbox_path) {
        set_error("mbox_path is null");
        return nullptr;
    }
    const std::filesystem::path box(mbox_path);
    std::error_code ec;
    if (!std::filesystem::exists(box, ec)) {
        set_error("mailbox not found: " + box.string());
        return nullptr;
    }

    auto mb = std::make_unique<eudora_mailbox>();
    mb->toc_file = toc_path_for_mailbox(box);

    // CheckTOC semantics: use the .toc when valid, else rebuild.
    tocfmt::TocError terr = tocfmt::TocError::None;
    if (auto toc = read_toc(mb->toc_file, box, &terr)) {
        mb->toc = std::move(*toc);
    } else {
        auto built = build_toc(box);
        if (!built) {
            set_error("cannot scan mailbox: " + box.string());
            return nullptr;
        }
        mb->toc = std::move(*built);
        write_toc(mb->toc, mb->toc_file);
    }
    return mb.release();
}

void eudora_mailbox_close(eudora_mailbox *mb) { delete mb; }

int32_t eudora_mailbox_count(const eudora_mailbox *mb) {
    return mb ? mb->toc.count() : 0;
}

int eudora_mailbox_summary(const eudora_mailbox *mb, int32_t index,
                           eudora_summary *out) {
    if (!mb || !out || index < 0 || index >= mb->toc.count()) {
        set_error("summary index out of range");
        return 0;
    }
    const MessageSummary &s = mb->toc.sums[static_cast<std::size_t>(index)];
    out->index = index;
    out->offset = s.offset;
    out->length = s.length;
    out->body_offset = s.body_offset;
    out->serial_num = s.serial_num;
    out->date_unix = mac_to_unix(s.seconds);
    out->orig_zone_minutes = s.orig_zone;
    out->flags = s.flags;
    out->opts = s.opts;
    out->state = static_cast<uint8_t>(s.state);
    out->spam_score = s.spam_score;
    out->priority_display = static_cast<uint8_t>(s.display_priority());
    out->uid_hash = s.uid_hash;
    out->msg_id_hash = s.msg_id_hash;
    out->from = s.from.c_str();
    out->subject = s.subject.c_str();
    return 1;
}

char *eudora_mailbox_read_message(const eudora_mailbox *mb, int32_t index) {
    if (!mb || index < 0 || index >= mb->toc.count()) {
        set_error("message index out of range");
        return nullptr;
    }
    const MessageSummary &s = mb->toc.sums[static_cast<std::size_t>(index)];
    if (s.offset < 0) {
        set_error("message not downloaded (IMAP placeholder)");
        return nullptr;
    }
    const std::string text =
        read_file_range(mb->toc.mailbox_path, s.offset, s.length);
    if (text.empty() && s.length > 0) {
        set_error("cannot read mailbox file");
        return nullptr;
    }
    return dup_string(text);
}

int eudora_mailbox_set_state(eudora_mailbox *mb, int32_t index, uint8_t state) {
    if (!mb || index < 0 || index >= mb->toc.count())
        return 0;
    mb->toc.sums[static_cast<std::size_t>(index)].state =
        static_cast<MessageState>(state);
    return 1;
}

int eudora_mailbox_delete(eudora_mailbox *mb, int32_t index) {
    if (!mb)
        return 0;
    return mb->toc.remove(index) ? 1 : 0;
}

int eudora_mailbox_compact(eudora_mailbox *mb) {
    if (!mb)
        return 0;
    if (!compact_mailbox(mb->toc)) {
        set_error("compaction failed");
        return 0;
    }
    return write_toc(mb->toc, mb->toc_file) ? 1 : 0;
}

int eudora_mailbox_save(eudora_mailbox *mb) {
    if (!mb)
        return 0;
    std::error_code ec;
    const auto size = std::filesystem::file_size(mb->toc.mailbox_path, ec);
    if (!ec)
        mb->toc.recalc_kbytes(static_cast<std::int64_t>(size));
    return write_toc(mb->toc, mb->toc_file) ? 1 : 0;
}

/* ---- message parsing --------------------------------------------------- */

eudora_message *eudora_message_parse(const char *raw, size_t len) {
    if (!raw) {
        set_error("raw message is null");
        return nullptr;
    }
    auto msg = std::make_unique<eudora_message>();
    msg->raw.assign(raw, len);
    const auto parts = split_message(msg->raw);
    msg->headers = HeaderSet::parse(parts.header_block);
    msg->body.assign(parts.body);
    msg->boundary = msg->headers.boundary();
    msg->filename = msg->headers.filename();
    return msg.release();
}

void eudora_message_free(eudora_message *msg) { delete msg; }

char *eudora_message_header(const eudora_message *msg, const char *name) {
    if (!msg || !name)
        return nullptr;
    if (auto v = msg->headers.get(name))
        return dup_string(*v);
    return nullptr;
}

char *eudora_message_header_decoded(const eudora_message *msg,
                                    const char *name) {
    if (!msg || !name)
        return dup_string("");
    return dup_string(msg->headers.get_decoded(name));
}

const char *eudora_message_body(const eudora_message *msg) {
    return msg ? msg->body.c_str() : "";
}

const char *eudora_message_content_type(const eudora_message *msg) {
    return msg ? msg->headers.content_type().c_str() : "";
}

const char *eudora_message_content_subtype(const eudora_message *msg) {
    return msg ? msg->headers.content_subtype().c_str() : "";
}

const char *eudora_message_boundary(const eudora_message *msg) {
    return msg ? msg->boundary.c_str() : "";
}

const char *eudora_message_filename(const eudora_message *msg) {
    return msg ? msg->filename.c_str() : "";
}

int eudora_message_transfer_encoding(const eudora_message *msg) {
    if (!msg)
        return 0;
    switch (msg->headers.transfer_encoding()) {
    case TransferEncoding::QuotedPrintable: return 1;
    case TransferEncoding::Base64: return 2;
    case TransferEncoding::Other: return 3;
    default: return 0;
    }
}

char *eudora_decode_body(const char *data, size_t len, int encoding,
                         size_t *out_len) {
    if (!data || !out_len)
        return nullptr;
    std::string out;
    const std::string_view in(data, len);
    bool ok = true;
    if (encoding == 1)
        ok = qp_decode(in, out);
    else if (encoding == 2)
        ok = base64_decode(in, out);
    else
        out.assign(in);
    if (!ok)
        set_error("body had decoding errors");
    char *buf = static_cast<char *>(std::malloc(out.size() + 1));
    if (!buf)
        return nullptr;
    std::memcpy(buf, out.data(), out.size());
    buf[out.size()] = '\0';
    *out_len = out.size();
    return buf;
}

char **eudora_parse_addresses(const char *header_value) {
    if (!header_value)
        return nullptr;
    auto parsed = parse_addresses(header_value, false);
    if (!parsed) {
        set_error("malformed address list");
        return nullptr;
    }
    char **arr = static_cast<char **>(
        std::calloc(parsed->size() + 1, sizeof(char *)));
    if (!arr)
        return nullptr;
    for (std::size_t i = 0; i < parsed->size(); ++i)
        arr[i] = dup_string((*parsed)[i]);
    return arr;
}

void eudora_addresses_free(char **addresses) {
    if (!addresses)
        return;
    for (char **p = addresses; *p; ++p)
        std::free(*p);
    std::free(addresses);
}

/* ---- POP3 -------------------------------------------------------------- */

namespace {

struct TransportBundle {
    PosixTransport plain;
#if defined(EUDORA_HAVE_TLS)
    std::unique_ptr<TlsTransport> tls;
#endif
    Transport *active = nullptr;

    // Returns nullptr and sets an error if the requested mode is impossible.
    Transport *setup(int tls_mode) {
#if defined(EUDORA_HAVE_TLS)
        if (tls_mode != EUDORA_TLS_NONE) {
            tls = std::make_unique<TlsTransport>(plain);
            active = tls.get();
        } else {
            active = &plain;
        }
        return active;
#else
        if (tls_mode != EUDORA_TLS_NONE) {
            set_error("TLS not built into this EudoraCore");
            return nullptr;
        }
        active = &plain;
        return active;
#endif
    }

    bool start_tls(const std::string &host) {
#if defined(EUDORA_HAVE_TLS)
        if (tls && tls->start_tls(host) == NetError::None)
            return true;
        set_error(tls ? "TLS handshake failed: " + tls->last_tls_error()
                      : "TLS not configured");
        return false;
#else
        (void)host;
        set_error("TLS not built into this EudoraCore");
        return false;
#endif
    }
};

// SumToFrom (buildtoc.c:1272): the envelope line for a stored message.
std::string envelope_from_line(const HeaderSet &hs) {
    std::string addr = "???@???";
    if (auto from = hs.get("From")) {
        const std::string s = short_address(*from);
        if (!s.empty())
            addr = s;
    }
    const DateTimeParts now =
        mac_seconds_to_date(mac_now_utc() +
                            static_cast<std::uint32_t>(local_zone_seconds()));
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    // Day of week via the civil-days computation (Mac epoch was a Friday,
    // but compute from Unix days: 1970-01-01 was a Thursday).
    const std::int64_t unix_days =
        (mac_to_unix(mac_now_utc()) + local_zone_seconds()) / 86400;
    const int dow = static_cast<int>(((unix_days % 7) + 11) % 7); // 1970-01-01 = Thu(4)
    char buf[128];
    std::snprintf(buf, sizeof(buf), "From %s %s %s %2d %02d:%02d:%02d %d\r",
                  addr.c_str(), days[dow], months[now.month - 1], now.day,
                  now.hour, now.minute, now.second, now.year);
    return buf;
}

} // namespace

int32_t eudora_pop3_fetch(const char *host, uint16_t port, int tls_mode,
                          const char *user, const char *password,
                          const char *mbox_path, int delete_from_server) {
    if (!host || !user || !password || !mbox_path) {
        set_error("missing argument");
        return -1;
    }

    TransportBundle bundle;
    Transport *transport = bundle.setup(tls_mode);
    if (!transport)
        return -1;

    Pop3Session pop(*transport);

    if (transport->connect(host, port, 45) != NetError::None) {
        set_error("cannot connect to " + std::string(host));
        return -1;
    }
    if (tls_mode == EUDORA_TLS_IMMEDIATE && !bundle.start_tls(host))
        return -1;
    if (!pop.begin_connected()) {
        set_error("no POP3 greeting");
        return -1;
    }
    pop.query_capabilities();
    if (tls_mode == EUDORA_TLS_STARTTLS) {
        if (!pop.request_stls()) {
            set_error("server refused STLS: " + pop.last_response());
            return -1;
        }
        if (!bundle.start_tls(host))
            return -1;
        pop.rescan_capabilities();
    }
    if (!pop.login(user, password)) {
        set_error("authentication failed: " + pop.last_response());
        return -1;
    }

    long count = 0, total = 0;
    if (!pop.stat(count, total)) {
        set_error("STAT failed: " + pop.last_response());
        return -1;
    }

    // Open (or create) the mailbox and its TOC.
    const std::filesystem::path box(mbox_path);
    {
        std::FILE *touch = std::fopen(box.string().c_str(), "ab");
        if (!touch) {
            set_error("cannot open mailbox for append");
            return -1;
        }
        std::fclose(touch);
    }
    std::unique_ptr<eudora_mailbox> mb(eudora_mailbox_open(mbox_path));
    if (!mb)
        return -1;

    std::FILE *out = std::fopen(box.string().c_str(), "ab");
    if (!out) {
        set_error("cannot append to mailbox");
        return -1;
    }

    int32_t fetched = 0;
    bool ok = true;
    for (long m = 1; m <= count && ok; ++m) {
        std::string message;
        ok = pop.retrieve(m, [&](std::string_view line) {
            message.append(line.data(), line.size());
        });
        if (!ok) {
            set_error("RETR failed: " + pop.last_response());
            break;
        }
        if (!message.empty() && message.back() != '\r')
            message += '\r';

        const auto parts = split_message(message);
        const HeaderSet hs = HeaderSet::parse(parts.header_block);
        const std::string envelope = envelope_from_line(hs);

        const std::int64_t start = std::ftell(out);
        ok = std::fwrite(envelope.data(), 1, envelope.size(), out) ==
                 envelope.size() &&
             std::fwrite(message.data(), 1, message.size(), out) ==
                 message.size();
        if (!ok) {
            set_error("cannot write to mailbox");
            break;
        }
        std::fflush(out);

        // Build the summary by scanning the appended region (BuildTOC-style).
        LineReader reader;
        if (reader.open(box) && reader.seek(start)) {
            MboxScanner scanner(reader, MboxParseOptions{});
            MessageSummary sum;
            if (scanner.next(sum)) {
                mb->toc.append(std::move(sum));
                ++fetched;
            }
        }

        if (delete_from_server && !pop.dele(m)) {
            set_error("DELE failed: " + pop.last_response());
            ok = false;
        }
    }
    std::fclose(out);
    pop.quit();

    std::error_code ec;
    const auto size = std::filesystem::file_size(box, ec);
    if (!ec)
        mb->toc.recalc_kbytes(static_cast<std::int64_t>(size));
    if (!write_toc(mb->toc, mb->toc_file)) {
        set_error("cannot write TOC");
        return -1;
    }
    return ok ? fetched : -1;
}

/* ---- SMTP -------------------------------------------------------------- */

int eudora_smtp_send(const char *host, uint16_t port, int tls_mode,
                     const char *user, const char *password,
                     const char *from, const char *recipients,
                     const char *message, size_t message_len) {
    if (!host || !from || !recipients || !message) {
        set_error("missing argument");
        return 601;
    }

    TransportBundle bundle;
    Transport *transport = bundle.setup(tls_mode);
    if (!transport)
        return 601;

    SmtpSession smtp(*transport);

    if (transport->connect(host, port, 45) != NetError::None) {
        set_error("cannot connect to " + std::string(host));
        return 601;
    }
    if (tls_mode == EUDORA_TLS_IMMEDIATE && !bundle.start_tls(host))
        return 601;
    if (!smtp.begin_connected()) {
        set_error("SMTP greeting/EHLO failed: " + smtp.last_reply());
        return smtp.last_code();
    }
    if (tls_mode == EUDORA_TLS_STARTTLS) {
        if (!smtp.request_starttls()) {
            set_error("server refused STARTTLS: " + smtp.last_reply());
            return smtp.last_code();
        }
        if (!bundle.start_tls(host))
            return 601;
        if (!smtp.ehlo_again()) {
            set_error("EHLO after STARTTLS failed");
            return smtp.last_code();
        }
    }
    if (user && *user && smtp.extensions().sasl != SaslMechanism::None) {
        if (!smtp.auth(user, password ? password : "")) {
            set_error("SMTP auth failed: " + smtp.last_reply());
            return smtp.last_code();
        }
    }
    if (!smtp.mail_from(from, static_cast<long>(message_len))) {
        set_error("MAIL FROM refused: " + smtp.last_reply());
        return smtp.last_code();
    }
    auto rcpts = parse_addresses(recipients, false);
    if (!rcpts || rcpts->empty()) {
        set_error("no valid recipients");
        return 550;
    }
    for (const auto &r : *rcpts) {
        if (r.empty() || r == ";" || r.back() == ':')
            continue; // group-syntax markers
        if (!smtp.rcpt_to(r)) {
            set_error("RCPT refused for " + r + ": " + smtp.last_reply());
            return smtp.last_code();
        }
    }
    if (!smtp.data(std::string_view(message, message_len))) {
        set_error("DATA failed: " + smtp.last_reply());
        return smtp.last_code();
    }
    const int code = smtp.last_code();
    smtp.quit();
    return code;
}

/* ---- address book ------------------------------------------------------ */

eudora_addressbook *eudora_addressbook_load(const char *path) {
    if (!path)
        return nullptr;
    auto loaded = AddressBook::load(path);
    if (!loaded) {
        set_error("cannot read address book");
        return nullptr;
    }
    auto ab = std::make_unique<eudora_addressbook>();
    ab->book = std::move(*loaded);
    return ab.release();
}

eudora_addressbook *eudora_addressbook_parse(const char *text) {
    if (!text)
        return nullptr;
    auto ab = std::make_unique<eudora_addressbook>();
    ab->book = AddressBook::parse(text);
    return ab.release();
}

void eudora_addressbook_free(eudora_addressbook *ab) { delete ab; }

int eudora_addressbook_save(const eudora_addressbook *ab, const char *path) {
    if (!ab || !path)
        return 0;
    return ab->book.save(path) ? 1 : 0;
}

int32_t eudora_addressbook_count(const eudora_addressbook *ab) {
    return ab ? static_cast<int32_t>(ab->book.nicknames().size()) : 0;
}

const char *eudora_addressbook_name(const eudora_addressbook *ab, int32_t i) {
    if (!ab || i < 0 || i >= eudora_addressbook_count(ab))
        return nullptr;
    return ab->book.nicknames()[static_cast<std::size_t>(i)].name.c_str();
}

const char *eudora_addressbook_addresses(const eudora_addressbook *ab,
                                         int32_t i) {
    if (!ab || i < 0 || i >= eudora_addressbook_count(ab))
        return nullptr;
    return ab->book.nicknames()[static_cast<std::size_t>(i)].addresses.c_str();
}

const char *eudora_addressbook_notes(const eudora_addressbook *ab, int32_t i) {
    if (!ab || i < 0 || i >= eudora_addressbook_count(ab))
        return nullptr;
    return ab->book.nicknames()[static_cast<std::size_t>(i)].notes.c_str();
}

int eudora_addressbook_set(eudora_addressbook *ab, const char *name,
                           const char *addresses, const char *notes) {
    if (!ab || !name || !*name)
        return 0;
    Nickname n;
    n.name = name;
    n.addresses = addresses ? addresses : "";
    n.notes = notes ? notes : "";
    auto parsed = parse_addresses(n.addresses, false);
    n.group = parsed && parsed->size() > 1;
    ab->book.set(std::move(n));
    return 1;
}

int eudora_addressbook_remove(eudora_addressbook *ab, const char *name) {
    if (!ab || !name)
        return 0;
    return ab->book.remove(name) ? 1 : 0;
}

char **eudora_addressbook_expand(const eudora_addressbook *ab,
                                 const char *address_list) {
    if (!ab || !address_list)
        return nullptr;
    const auto expanded = ab->book.expand(address_list);
    char **arr =
        static_cast<char **>(std::calloc(expanded.size() + 1, sizeof(char *)));
    if (!arr)
        return nullptr;
    for (std::size_t i = 0; i < expanded.size(); ++i)
        arr[i] = dup_string(expanded[i]);
    return arr;
}

int eudora_addressbook_contains(const eudora_addressbook *ab,
                                const char *address) {
    if (!ab || !address)
        return 0;
    return ab->book.contains_address(address) ? 1 : 0;
}

/* ---- filters ----------------------------------------------------------- */

eudora_filters *eudora_filters_load(const char *path) {
    if (!path)
        return nullptr;
    auto loaded = read_filters(path);
    if (!loaded) {
        set_error("cannot read filters file");
        return nullptr;
    }
    auto f = std::make_unique<eudora_filters>();
    f->filters = std::move(*loaded);
    return f.release();
}

eudora_filters *eudora_filters_parse(const char *text) {
    if (!text)
        return nullptr;
    auto f = std::make_unique<eudora_filters>();
    f->filters = parse_filters(text);
    return f.release();
}

void eudora_filters_free(eudora_filters *f) { delete f; }

int32_t eudora_filters_count(const eudora_filters *f) {
    return f ? static_cast<int32_t>(f->filters.size()) : 0;
}

int eudora_filters_save(const eudora_filters *f, const char *path) {
    if (!f || !path)
        return 0;
    return write_filters(f->filters, path) ? 1 : 0;
}

eudora_fired_action *eudora_filters_run(const eudora_filters *f, int event,
                                        const char *raw_message, size_t len,
                                        int32_t *out_count) {
    return eudora_filters_run_with_book(f, event, raw_message, len, nullptr,
                                        out_count);
}

eudora_fired_action *eudora_filters_run_with_book(
    const eudora_filters *f, int event, const char *raw_message, size_t len,
    const eudora_addressbook *book, int32_t *out_count) {
    if (!f || !raw_message || !out_count)
        return nullptr;
    *out_count = 0;

    FilterEvent ev = FilterEvent::Incoming;
    if (event == EUDORA_FILTER_OUTGOING)
        ev = FilterEvent::Outgoing;
    else if (event == EUDORA_FILTER_MANUAL)
        ev = FilterEvent::Manual;

    FilterContext ctx;
    ctx.raw_message = std::string_view(raw_message, len);
    if (book) {
        // Single-book model: the term's file name is not consulted.
        ctx.address_in_book = [book](std::string_view addr, std::string_view) {
            return book->book.contains_address(addr);
        };
    }
    const auto fired = run_filters(f->filters, ev, ctx);
    if (fired.empty())
        return nullptr;

    auto *arr = static_cast<eudora_fired_action *>(
        std::calloc(fired.size(), sizeof(eudora_fired_action)));
    if (!arr)
        return nullptr;
    for (std::size_t i = 0; i < fired.size(); ++i) {
        arr[i].filter_name = dup_string(fired[i].filter->name);
        arr[i].keyword = dup_string(filter_keyword_string(fired[i].action.keyword));
        arr[i].value = dup_string(fired[i].action.value);
    }
    *out_count = static_cast<int32_t>(fired.size());
    return arr;
}

void eudora_fired_actions_free(eudora_fired_action *actions, int32_t count) {
    if (!actions)
        return;
    for (int32_t i = 0; i < count; ++i) {
        std::free(const_cast<char *>(actions[i].filter_name));
        std::free(const_cast<char *>(actions[i].keyword));
        std::free(const_cast<char *>(actions[i].value));
    }
    std::free(actions);
}

} // extern "C"
