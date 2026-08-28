// MacRoman <-> UTF-8 conversion.
//
// Legacy TOC summaries store `from`/`subj` in MacRoman unless FLAG_UTF8 is
// set (Eudora 6.x wrote UTF-8 only when the strings could not be represented
// in MacRoman — see RemoveUTF8FromSum in buildtoc.c).  The modern core keeps
// all text as UTF-8 and converts at the serialization boundary.

#pragma once

#include <string>
#include <string_view>

namespace eudora {

// Convert MacRoman-encoded bytes to UTF-8.  Always succeeds (MacRoman maps
// every byte).
std::string macroman_to_utf8(std::string_view macroman);

// Convert UTF-8 to MacRoman.  Returns true when every code point mapped;
// on failure, `out` is left with unmappable characters replaced by '?'.
bool utf8_to_macroman(std::string_view utf8, std::string &out);

// True if the string is pure 7-bit ASCII.
bool is_ascii(std::string_view s);

} // namespace eudora
