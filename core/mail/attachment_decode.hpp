// Decoders for the pre-MIME attachment encodings classic Mac mail used and
// the modern MIME walker does not handle: uuencode, BinHex 4.0, and the
// AppleSingle / AppleDouble container (application/applefile).  Each pulls
// out the file name and the data-fork bytes so a saved attachment matches
// what the original Eudora would have written.

#pragma once

#include <string>
#include <string_view>

namespace eudora {

struct DecodedAttachment {
    bool ok = false;
    std::string filename;
    std::string data; // the data fork / file contents
};

// "begin <mode> <name>\n" ... "`\nend" — the classic uuencode wrapper.
DecodedAttachment decode_uuencode(std::string_view text);

// BinHex 4.0: the ":...:"-delimited, RLE90-compressed, 6-bit-encoded Mac
// file (name + type/creator + data fork + resource fork).  Returns the
// data fork.
DecodedAttachment decode_binhex(std::string_view text);

// AppleSingle (magic 0x00051600) / AppleDouble (0x00051607): extract the
// data fork (entry id 1) and real name (entry id 3) if present.
DecodedAttachment decode_applefile(std::string_view bytes);

// Detect and decode whichever of the above `text`/`bytes` looks like; ok
// is false when none matches.
DecodedAttachment decode_classic_attachment(std::string_view data);

} // namespace eudora
