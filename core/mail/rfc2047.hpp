// RFC 2047 (née 1342/1522) encoded-word decoding — the modern Fix1342 /
// Translate1342 / PseudoQP / DecodeB64String (lex822.c:508-707).
//
// The legacy code transliterated into MacRoman via resource tables; the
// modern core decodes into UTF-8.  Unknown charsets leave the encoded word
// untouched, exactly as Translate1342 refused unknown tables.

#pragma once

#include <string>
#include <string_view>

namespace eudora {

// Decode all =?charset?enc?text?= words in a header value.  Whitespace
// between adjacent encoded words is removed (per the RFC and Fix1342).
// Returns the decoded UTF-8 string.
std::string decode_rfc2047(std::string_view header);

// The "Q" encoding decoder ('_' means space) — PseudoQP.
std::string decode_q_encoding(std::string_view text);

} // namespace eudora
