// Outgoing message composer — the modern, de-Carbonized replacement for the
// MIME-generation half of sendmail.c (TransmitMessageText/Mixed,
// SendAnAttachment, Encode1342, the RFC 822 date formatter).
//
// Classic-Mac attachment conversions (BinHex/AppleDouble/AppleSingle,
// PICT/QuickTime flattening) are gone: attachments are plain MIME parts
// with base64 bodies, which is what every modern receiver expects.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace eudora {

struct Attachment {
    std::filesystem::path path;
    std::string content_type; // "" = guess from extension
    std::string filename;     // "" = path filename
};

class MessageComposer {
public:
    MessageComposer &from(std::string name, std::string address);
    MessageComposer &to(std::string address_list);
    MessageComposer &cc(std::string address_list);
    MessageComposer &bcc(std::string address_list);
    MessageComposer &reply_to(std::string address);
    MessageComposer &subject(std::string text); // UTF-8; RFC2047 on the wire
    MessageComposer &body(std::string utf8_text);
    // Styled alternative (the rich composer): when set, build() emits a
    // multipart/alternative with the plain body first and this text/html
    // second, so plain-text readers still see body().  UTF-8.
    MessageComposer &html_body(std::string utf8_html);
    MessageComposer &header(std::string name, std::string value); // extra
    MessageComposer &attach(Attachment att);
    // X-Priority: 1..5 (Prior2Display scale); 0/3 omit the header.
    MessageComposer &priority(int display_priority);

    // The RFC 822 message, CRLF line ends, ready for SmtpSession::data().
    // Bcc recipients are (correctly) not present in the headers.
    // Returns std::nullopt when an attachment can't be read.
    std::optional<std::string> build() const;

    // Envelope recipients: to + cc + bcc, parsed to bare addresses.
    std::vector<std::string> recipients() const;
    // Envelope sender address.
    std::string sender() const;

private:
    std::string from_name_, from_addr_;
    std::string to_, cc_, bcc_, reply_to_;
    std::string subject_, body_, html_body_;
    std::vector<std::pair<std::string, std::string>> extra_;
    std::vector<Attachment> attachments_;
    int priority_ = 0;
};

// RFC 822 date for "now" in the local zone ("Wed, 14 Jun 1989 12:36:18 -0500").
std::string rfc822_now();
// Deterministic variant for tests: explicit Unix time + zone offset seconds.
std::string rfc822_date(std::int64_t unix_seconds, long zone_seconds);

// A unique Message-Id: <hex.hex@host> (NewMessageId's modern equivalent).
std::string generate_message_id(const std::string &host);

// Content-type guess from a filename extension (small built-in table;
// application/octet-stream otherwise).
std::string guess_content_type(const std::filesystem::path &file);

} // namespace eudora
