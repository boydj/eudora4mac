// RFC 822 header parser — the modern header.c.
//
// Replaces ReadHeader/HeaderDesc (header.c:44-, Include/header.h:49-89):
// splits a header block into unfolded fields and derives the MIME facts the
// engine cares about (content type/subtype, encoding, boundary, filename),
// plus the message-id hashes the TOC stores.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eudora {

struct HeaderField {
    std::string name;  // without the colon
    std::string value; // unfolded, leading space stripped, CR-free
};

// Content-Transfer-Encoding values the engine distinguishes.
enum class TransferEncoding { None, SevenBit, EightBit, Binary, QuotedPrintable, Base64, Other };

struct MimeAttribute {
    std::string name;  // lowercased
    std::string value; // unquoted
};

class HeaderSet {
public:
    // Parse a raw header block: everything up to the first blank line.
    // Accepts CR, LF, or CRLF line terminators.
    static HeaderSet parse(std::string_view raw);

    const std::vector<HeaderField> &fields() const { return fields_; }

    // First field with the given name (case-insensitive), unfolded raw value.
    std::optional<std::string_view> get(std::string_view name) const;
    // All values for a repeated header.
    std::vector<std::string_view> get_all(std::string_view name) const;
    // RFC 2047-decoded value, for display.
    std::string get_decoded(std::string_view name) const;

    // MIME facts (from Content-Type / Content-Transfer-Encoding /
    // Content-Disposition), lowercased.
    const std::string &content_type() const { return content_type_; }
    const std::string &content_subtype() const { return content_subtype_; }
    const std::vector<MimeAttribute> &content_attributes() const {
        return content_attributes_;
    }
    std::optional<std::string_view> content_attribute(std::string_view name) const;
    TransferEncoding transfer_encoding() const { return transfer_encoding_; }
    std::string boundary() const;  // multipart boundary, empty if none
    std::string filename() const;  // from Content-Disposition/Content-Type

    // Hashes the TOC stores (msgIdHash / fromHash).
    std::uint32_t message_id_hash() const;

private:
    std::vector<HeaderField> fields_;
    std::string content_type_;
    std::string content_subtype_;
    std::vector<MimeAttribute> content_attributes_;
    TransferEncoding transfer_encoding_ = TransferEncoding::None;
};

// Split a full raw message into header block and body (the blank line is
// consumed).  Works with CR, LF, or CRLF conventions.
struct MessageParts {
    std::string_view header_block;
    std::string_view body;
};
MessageParts split_message(std::string_view raw_message);

} // namespace eudora
