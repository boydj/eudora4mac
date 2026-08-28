#include "mail/composer.hpp"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>

#include "compat/hashes.hpp"
#include "compat/macdate.hpp"
#include "mail/address_parser.hpp"
#include "mail/lex822.hpp"
#include "mail/mime_codec.hpp"
#include "mail/rfc2047.hpp"

namespace eudora {

namespace {

constexpr const char *kCrlf = "\r\n";

bool is_ascii_clean(std::string_view s) {
    for (unsigned char c : s)
        if (c >= 0x80)
            return false;
    return true;
}

std::string fold_address_header(const std::string &name,
                                const std::string &value) {
    // Fold after commas to keep lines under ~78 columns (the original wrote
    // one address per continuation line).
    std::string out = name + ": ";
    std::size_t col = out.size();
    auto parsed = parse_addresses(value, true);
    std::vector<std::string> items;
    if (parsed && !parsed->empty()) {
        items = *parsed;
    } else {
        items.push_back(value);
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::string piece = items[i];
        if (i + 1 < items.size())
            piece += ",";
        if (i > 0) {
            if (col + piece.size() > 76) {
                out += kCrlf;
                out += " ";
                col = 1;
            } else {
                out += " ";
                ++col;
            }
        }
        out += piece;
        col += piece.size();
    }
    out += kCrlf;
    return out;
}

// Body text normalized to CRLF.
std::string normalize_newlines(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            out += kCrlf;
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
        } else if (c == '\n') {
            out += kCrlf;
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace

std::string rfc822_date(std::int64_t unix_seconds, long zone_seconds) {
    const std::uint32_t mac =
        unix_to_mac(unix_seconds + zone_seconds); // wall clock
    const DateTimeParts p = mac_seconds_to_date(mac);
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::int64_t days_since_epoch = (unix_seconds + zone_seconds) / 86400;
    const int dow = static_cast<int>(((days_since_epoch % 7) + 11) % 7);
    const long zm = zone_seconds / 60;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s, %d %s %d %02d:%02d:%02d %c%02ld%02ld",
                  days[dow], p.day, months[p.month - 1], p.year, p.hour,
                  p.minute, p.second, zm < 0 ? '-' : '+', std::labs(zm) / 60,
                  std::labs(zm) % 60);
    return buf;
}

std::string rfc822_now() {
    return rfc822_date(static_cast<std::int64_t>(std::time(nullptr)),
                       local_zone_seconds());
}

std::string generate_message_id(const std::string &host) {
    static std::atomic<std::uint32_t> counter{0};
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    const std::uint32_t c = counter.fetch_add(1, std::memory_order_relaxed);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "<%llx.%x.%x@%s>",
                  static_cast<unsigned long long>(now), c,
                  kr_hash(host) & 0xFFFFFF, host.c_str());
    return buf;
}

std::string guess_content_type(const std::filesystem::path &file) {
    std::string ext = file.extension().string();
    for (auto &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    struct Map {
        const char *ext, *type;
    };
    static constexpr Map kMap[] = {
        {".txt", "text/plain"},       {".html", "text/html"},
        {".htm", "text/html"},        {".pdf", "application/pdf"},
        {".jpg", "image/jpeg"},       {".jpeg", "image/jpeg"},
        {".png", "image/png"},        {".gif", "image/gif"},
        {".tif", "image/tiff"},       {".tiff", "image/tiff"},
        {".zip", "application/zip"},  {".gz", "application/gzip"},
        {".mp3", "audio/mpeg"},       {".mp4", "video/mp4"},
        {".mov", "video/quicktime"},  {".csv", "text/csv"},
        {".json", "application/json"},{".xml", "application/xml"},
    };
    for (const auto &m : kMap)
        if (ext == m.ext)
            return m.type;
    return "application/octet-stream";
}

MessageComposer &MessageComposer::from(std::string name, std::string address) {
    from_name_ = std::move(name);
    from_addr_ = std::move(address);
    return *this;
}
MessageComposer &MessageComposer::to(std::string v) {
    to_ = std::move(v);
    return *this;
}
MessageComposer &MessageComposer::cc(std::string v) {
    cc_ = std::move(v);
    return *this;
}
MessageComposer &MessageComposer::bcc(std::string v) {
    bcc_ = std::move(v);
    return *this;
}
MessageComposer &MessageComposer::reply_to(std::string v) {
    reply_to_ = std::move(v);
    return *this;
}
MessageComposer &MessageComposer::subject(std::string v) {
    subject_ = std::move(v);
    return *this;
}
MessageComposer &MessageComposer::body(std::string v) {
    body_ = std::move(v);
    return *this;
}
MessageComposer &MessageComposer::header(std::string name, std::string value) {
    extra_.emplace_back(std::move(name), std::move(value));
    return *this;
}
MessageComposer &MessageComposer::attach(Attachment att) {
    attachments_.push_back(std::move(att));
    return *this;
}
MessageComposer &MessageComposer::priority(int display_priority) {
    priority_ = display_priority;
    return *this;
}

std::vector<std::string> MessageComposer::recipients() const {
    std::vector<std::string> out;
    for (const std::string *list : {&to_, &cc_, &bcc_}) {
        if (list->empty())
            continue;
        auto parsed = parse_addresses(*list, false);
        if (!parsed)
            continue;
        for (const auto &a : *parsed) {
            if (a.empty() || a == ";" || a.back() == ':')
                continue;
            out.push_back(a);
        }
    }
    return out;
}

std::string MessageComposer::sender() const { return short_address(from_addr_); }

std::optional<std::string> MessageComposer::build() const {
    std::string msg;

    // From: phrase <addr> — quote the phrase if it needs it.
    std::string from_value;
    if (!from_name_.empty()) {
        // Quote the phrase only when it contains specials; plain words
        // (including spaces) are legal atoms in a phrase.  An RFC 2047
        // encoded word must never be quoted (quotes suppress decoding).
        const std::string enc = encode_rfc2047(from_name_);
        from_value = (enc == from_name_) ? quote822(enc, false) : enc;
        from_value += " <" + from_addr_ + ">";
    } else {
        from_value = from_addr_;
    }

    msg += "From: " + from_value + kCrlf;
    if (!to_.empty())
        msg += fold_address_header("To", to_);
    if (!cc_.empty())
        msg += fold_address_header("Cc", cc_);
    // Bcc intentionally omitted from the headers (DoRcptTos handled the
    // envelope separately).
    if (!reply_to_.empty())
        msg += "Reply-To: " + reply_to_ + kCrlf;
    msg += "Subject: " + encode_rfc2047(subject_) + kCrlf;
    msg += "Date: " + rfc822_now() + kCrlf;
    msg += "Message-Id: " +
           generate_message_id(short_address(from_addr_).find('@') !=
                                       std::string::npos
                                   ? short_address(from_addr_).substr(
                                         short_address(from_addr_).find('@') + 1)
                                   : "localhost") +
           kCrlf;
    if (priority_ > 0 && priority_ != 3)
        msg += "X-Priority: " + std::to_string(priority_) + kCrlf;
    msg += "MIME-Version: 1.0" + std::string(kCrlf);
    for (const auto &[name, value] : extra_)
        msg += name + ": " + value + kCrlf;

    // Body part.
    const std::string body_crlf = normalize_newlines(body_);
    const bool body_ascii = is_ascii_clean(body_crlf);
    std::string body_part_headers;
    std::string body_part;
    if (body_ascii) {
        body_part_headers = "Content-Type: text/plain; charset=\"us-ascii\"";
        body_part_headers += kCrlf;
        body_part_headers += "Content-Transfer-Encoding: 7bit";
        body_part_headers += kCrlf;
        body_part = body_crlf;
    } else {
        body_part_headers = "Content-Type: text/plain; charset=\"utf-8\"";
        body_part_headers += kCrlf;
        body_part_headers += "Content-Transfer-Encoding: quoted-printable";
        body_part_headers += kCrlf;
        // The QP encoder works on CR-terminated text with CR newlines out.
        std::string cr_body;
        cr_body.reserve(body_crlf.size());
        for (std::size_t i = 0; i < body_crlf.size(); ++i) {
            if (body_crlf[i] == '\r' && i + 1 < body_crlf.size() &&
                body_crlf[i + 1] == '\n') {
                cr_body += '\r';
                ++i;
            } else {
                cr_body += body_crlf[i];
            }
        }
        std::string qp = qp_encode(cr_body, "\r");
        // Back to CRLF for the wire.
        body_part = normalize_newlines(qp);
    }
    if (!body_part.empty() &&
        body_part.compare(body_part.size() - 2, 2, kCrlf) != 0)
        body_part += kCrlf;

    if (attachments_.empty()) {
        msg += body_part_headers;
        msg += kCrlf;
        msg += body_part;
        return msg;
    }

    // multipart/mixed with base64 attachment parts (BuildBoundary's shape).
    const std::string boundary =
        "=====================_" +
        std::to_string(kr_hash(msg) ^ static_cast<std::uint32_t>(
                                          attachments_.size())) +
        "==_";
    msg += "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"" + kCrlf;
    msg += kCrlf;
    msg += "--" + boundary + kCrlf;
    msg += body_part_headers;
    msg += kCrlf;
    msg += body_part;

    for (const auto &att : attachments_) {
        std::ifstream f(att.path, std::ios::binary);
        if (!f)
            return std::nullopt;
        std::string data((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

        const std::string filename =
            att.filename.empty() ? att.path.filename().string() : att.filename;
        const std::string type =
            att.content_type.empty() ? guess_content_type(att.path)
                                     : att.content_type;

        msg += "--" + boundary + kCrlf;
        msg += "Content-Type: " + type + "; name=\"" + filename + "\"" + kCrlf;
        msg += "Content-Disposition: attachment; filename=\"" + filename +
               "\"" + kCrlf;
        msg += "Content-Transfer-Encoding: base64" + std::string(kCrlf);
        msg += kCrlf;
        // 68-column wrapping with CR, then to CRLF.
        msg += normalize_newlines(base64_encode(data, "\r"));
        msg += kCrlf;
    }
    msg += "--" + boundary + "--" + kCrlf;
    return msg;
}

} // namespace eudora
