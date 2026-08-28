// Charset -> UTF-8 conversion.
//
// The legacy client leaned on the Mac Text Encoding Converter for anything
// beyond MacRoman; this module ports the practical set as portable tables
// (Latin-1/9, the ISO-8859 family, the Windows code pages, KOI8-R, UTF-16)
// and, on Apple platforms, falls back to CoreFoundation for the long tail
// (Shift_JIS, EUC-*, GB2312, Big5, ISO-2022-*, everything else) so a real
// macOS build has full coverage without shipping multi-megabyte CJK tables.

#pragma once

#include <string>
#include <string_view>

namespace eudora {

// Convert `bytes` (in `charset`) to UTF-8 in `out`.  Returns false only when
// the charset is unrecognized on this platform (the caller then keeps the
// bytes verbatim, matching the legacy "punt" behavior).  An RFC 2231
// language suffix ("utf-8*en") is tolerated.
bool charset_to_utf8(std::string_view charset, std::string_view bytes,
                     std::string &out);

// Encode a single Unicode code point as UTF-8 onto `out`.
void append_utf8(char32_t cp, std::string &out);

} // namespace eudora
