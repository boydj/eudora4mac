// Pascal-string helpers.
//
// Eudora's on-disk formats store text as Pascal strings (length byte
// followed by up to 255 bytes) inside fixed-size buffers — e.g. the
// `from[48]` and `subj[60]` fields of a TOC message summary.  Modern code
// uses std::string (UTF-8 or MacRoman passthrough); these helpers only
// exist at the (de)serialization boundary.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace eudora {

// Decode a Pascal string held in a fixed-size buffer.  The length byte is
// clamped to the buffer capacity, mirroring the original CheckStringLen
// repair logic (toc.c) that fixed corrupted length bytes on load.
std::string pascal_to_string(std::span<const std::uint8_t> buffer);

// Encode `text` as a Pascal string into a fixed-size buffer, truncating to
// buffer.size()-1 bytes.  Unused trailing bytes are zeroed.
void string_to_pascal(const std::string &text, std::span<std::uint8_t> buffer);

} // namespace eudora
