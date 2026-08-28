// MIME part walker — the part navigation the legacy POP decoder did inline
// while streaming (pop.c's attachment handling), rebuilt over the parsed
// message: split a raw message into its leaf parts by walking multipart
// boundaries, so attachments can be listed, decoded, and saved.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "mail/header_parser.hpp"

namespace eudora {

struct MimePart {
    std::string type;     // lowercased ("text", "image", ...)
    std::string subtype;  // lowercased ("plain", "png", ...)
    std::string filename; // Content-Disposition/Content-Type name, "" if none
    std::string charset;  // Content-Type charset (text parts), lowercased
    TransferEncoding encoding = TransferEncoding::None;
    std::size_t body_offset = 0; // into the raw message buffer
    std::size_t body_length = 0;
    int depth = 0;               // 0 = the whole-message body
    bool is_attachment = false;

    std::string_view body(std::string_view raw_message) const {
        return raw_message.substr(body_offset, body_length);
    }
};

// Walk the message and return its LEAF parts in document order; multipart
// containers are not emitted.  A non-multipart message yields exactly one
// part.  message/rfc822 is entered one level.  Boundary lines are
// recognized with CR, LF, or CRLF terminators (the mailbox convention is
// CR).  Offsets index into `raw_message`.
std::vector<MimePart> walk_mime(std::string_view raw_message,
                                int max_depth = 8);

// Decode a part's body per its transfer encoding (base64 decodes in text
// mode for text/* parts, exactly like the POP decoder's FIX_NL).  Raw bytes
// only — no charset conversion (use decode_text_part for display text).
std::string decode_part(std::string_view raw_message, const MimePart &part);

// Decode a text part and convert it from its charset to UTF-8 (for display).
// Non-text parts return their raw decoded bytes.
std::string decode_text_part(std::string_view raw_message, const MimePart &part);

} // namespace eudora
