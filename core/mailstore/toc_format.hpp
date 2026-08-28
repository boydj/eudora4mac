// The Eudora Mac ".toc" binary format.
//
// A TOC file (or the 'TOCF' resource in the mailbox's resource fork) is a
// byte-for-byte image of the legacy in-memory Handle:
//
//     +---------------------------+  offset 0
//     |  TOCType fixed header     |  278 bytes
//     +---------------------------+
//     |  MSumType sums[0]         |  220 bytes each
//     |  MSumType sums[1]         |
//     |  ...                      |
//     +---------------------------+  EOF
//
// There is no magic number and no checksum; validity is checked by the size
// invariant  file == 278 + max(1, count) * 220  (TOCSizeShouldBe, toc.c:32)
// plus per-summary range checks (InsaneTOC, toc.c:921).
//
// The layout below matches the SHIPPING Carbon (CFM) build: big-endian,
// mac68k 2-byte struct alignment, CodeWarrior MSB-first bitfields, and
// smallest-fit enums (StateEnum is one byte).  All offsets were derived
// from Include/mailbox.h under those rules.
//
// Two classes of field are NOT round-tripped:
//   - Live pointers/Handles the original serialized as garbage (refN,
//     mesgErrH, cache, messH, win, next, previewPTE, ...) — ignored on read
//     and written as zero, exactly as the original zeroed them (toc.c:227).
//   - Inert persisted data no modern feature reads: pluginKey, pluginValue,
//     profile[], ezOpenSerialNum, oldKValues, sumRandBytes, and the spare
//     fields.  These occupy real bytes in a legacy image but carry no
//     behavior we implement, so we decode them as zero and re-encode them
//     as zero rather than preserving them — a deliberate scope choice, not
//     a "only live pointers are dropped" guarantee.  A round-trip of a real
//     Eudora TOC therefore clears them.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mailstore/summary.hpp"

namespace eudora::tocfmt {

inline constexpr std::size_t kHeaderSize = 278;  // offsetof(TOCType, sums)
inline constexpr std::size_t kSummarySize = 220; // sizeof(MSumType), mac68k

// sizeof(TOCType): header plus the sums[1] flexible-array stub.
inline constexpr std::size_t kTocTypeSize = kHeaderSize + kSummarySize;

// TOCSizeShouldBe: byte size of a TOC image holding `count` summaries.
inline constexpr std::size_t image_size(int count) {
    return kHeaderSize + static_cast<std::size_t>(count < 1 ? 1 : count) * kSummarySize;
}

// --- TOCType field offsets (mac68k layout) ---------------------------------
namespace hdr {
inline constexpr std::size_t version = 0;        // short
inline constexpr std::size_t refN = 2;           // short (transient)
inline constexpr std::size_t spareStr = 4;       // Str31 (32 bytes)
inline constexpr std::size_t flagBits = 36;      // uLong bitfield block
inline constexpr std::size_t oldKValues = 40;    // uLong
inline constexpr std::size_t volumeFree = 44;    // long (transient)
inline constexpr std::size_t needRedo = 48;      // short (transient)
inline constexpr std::size_t maxValid = 50;      // short (transient)
inline constexpr std::size_t mailboxSpec = 52;   // FSSpec {vRefNum, parID, Str63}
inline constexpr std::size_t specVRefNum = 52;   // short
inline constexpr std::size_t specParID = 54;     // long
inline constexpr std::size_t specName = 58;      // Str63 (64 bytes)
inline constexpr std::size_t lastSort = 122;     // long
inline constexpr std::size_t sorts = 126;        // Byte[12]
inline constexpr std::size_t pluginKey = 138;    // long
inline constexpr std::size_t pluginValue = 142;  // long
inline constexpr std::size_t previewHi = 146;    // short
inline constexpr std::size_t spareShort = 148;   // short
inline constexpr std::size_t previewID = 150;    // uLong (transient)
inline constexpr std::size_t previewPTE = 154;   // pointer (garbage)
inline constexpr std::size_t lastSameTicks = 158;
inline constexpr std::size_t mouseTicks = 162;
inline constexpr std::size_t mouseSpot = 166;    // Point
inline constexpr std::size_t ezOpenSerialNum = 170;
inline constexpr std::size_t lastSortTicks = 174;
inline constexpr std::size_t nextSerialNum = 178; // long
inline constexpr std::size_t profile = 182;       // long[2]
inline constexpr std::size_t hFileView = 190;     // Handle (garbage)
inline constexpr std::size_t imapMBH = 194;       // Handle (garbage)
inline constexpr std::size_t internalUseOnly = 198;
inline constexpr std::size_t waste = 202;         // long[3]
inline constexpr std::size_t singlePreviewProfileHash = 214;
inline constexpr std::size_t multiPreviewProfileHash = 218;
inline constexpr std::size_t drawerWin = 222;     // pointer (garbage)
inline constexpr std::size_t unreadCount = 226;   // long
inline constexpr std::size_t usedK = 230;         // uLong
inline constexpr std::size_t totalK = 234;        // uLong
inline constexpr std::size_t unreadBase = 238;    // short
inline constexpr std::size_t minorVersion = 240;  // short
inline constexpr std::size_t expireBits = 242;    // uLong bitfield block
inline constexpr std::size_t writeDate = 246;     // uLong
inline constexpr std::size_t boxSize = 250;       // long
inline constexpr std::size_t temp = 254;          // Boolean
inline constexpr std::size_t unread = 255;        // Boolean
inline constexpr std::size_t resort = 256;        // Boolean
inline constexpr std::size_t reallyDirty = 257;   // Boolean
inline constexpr std::size_t spareLong = 258;     // uLong
inline constexpr std::size_t count = 262;         // short
inline constexpr std::size_t durty = 264;         // Boolean (transient)
inline constexpr std::size_t next = 266;          // Handle (garbage)
inline constexpr std::size_t win = 270;           // pointer (garbage)
inline constexpr std::size_t which = 274;         // short
inline constexpr std::size_t building = 276;      // Boolean (transient)
} // namespace hdr

// TOCType flag word (offset 36), MSB-first bit positions from the MSB.
namespace hdrbits {
inline constexpr unsigned unused_first = 0, unused_width = 14;
inline constexpr unsigned drawer = 14;
inline constexpr unsigned conConMultiScan = 15;
inline constexpr unsigned updateBoxSizes = 16;
inline constexpr unsigned hasFileView = 17;
inline constexpr unsigned fileView = 18;
inline constexpr unsigned analScanned = 19;
inline constexpr unsigned noInvalBox = 20;
inline constexpr unsigned searchFocus = 21;
inline constexpr unsigned beingWritten_first = 22, beingWritten_width = 4;
inline constexpr unsigned imapMessagesWaiting = 26;
inline constexpr unsigned imapTOC = 27;
inline constexpr unsigned virtualTOC = 28;
inline constexpr unsigned listFocus = 29;
inline constexpr unsigned userActive = 30;
inline constexpr unsigned laurence = 31;
} // namespace hdrbits

// --- MSumType field offsets (mac68k layout) --------------------------------
namespace sum {
inline constexpr std::size_t offset = 0;          // long
inline constexpr std::size_t length = 4;          // long
inline constexpr std::size_t bodyOffset = 8;      // int (4 bytes)
inline constexpr std::size_t state = 12;          // StateEnum (1 byte)
inline constexpr std::size_t spamBits = 14;       // long bitfield block
inline constexpr std::size_t arrivalSeconds = 18; // uLong
inline constexpr std::size_t mesgErrH = 22;       // Handle (garbage)
inline constexpr std::size_t fromHash = 26;       // uLong
inline constexpr std::size_t spare = 30;          // uLong[3]
inline constexpr std::size_t serialNum = 42;      // long
inline constexpr std::size_t seconds = 46;        // uLong
inline constexpr std::size_t flags = 50;          // uLong
inline constexpr std::size_t savedPos = 54;       // Rect (4 shorts)
inline constexpr std::size_t priority = 62;       // Byte
inline constexpr std::size_t origPriority = 63;   // Byte
inline constexpr std::size_t tableId = 64;        // short
inline constexpr std::size_t scoreBits = 66;      // short bitfield block
inline constexpr std::size_t spareShort2 = 68;    // short
inline constexpr std::size_t sumRandBytes = 70;   // short
inline constexpr std::size_t origZone = 72;       // short
inline constexpr std::size_t sigId = 74;          // uLong
inline constexpr std::size_t from = 78;           // Pascal str in char[48]
inline constexpr std::size_t fromCap = 48;
inline constexpr std::size_t popPersId = 126;     // uLong
inline constexpr std::size_t persId = 130;        // uLong
inline constexpr std::size_t msgIdHash = 134;     // long
inline constexpr std::size_t subjId = 138;        // short
inline constexpr std::size_t spareShort = 140;    // short
inline constexpr std::size_t subj = 142;          // Pascal str in char[60]
inline constexpr std::size_t subjCap = 60;
inline constexpr std::size_t opts = 202;          // uLong
inline constexpr std::size_t uidHash = 206;       // uLong
inline constexpr std::size_t cache = 210;         // Handle (garbage)
inline constexpr std::size_t selected = 214;      // Boolean (transient)
inline constexpr std::size_t messH = 216;         // Handle (garbage)
} // namespace sum

// MSumType spam word (offset 14), MSB-first.
namespace spambits {
inline constexpr unsigned spamScore_first = 0, spamScore_width = 8; // signed
inline constexpr unsigned spamBecause_first = 8, spamBecause_width = 3;
} // namespace spambits

// MSumType score word (16-bit unit at offset 66), MSB-first within 16 bits.
// Handled with explicit shifts in the codec (score:4 signed, outType:4).

// --- codec -----------------------------------------------------------------

enum class TocError {
    None,
    TooSmall,          // image smaller than one header + one summary
    SizeMismatch,      // euCorruptTOC: size invariant violated
    BadSummaryRange,   // euCorruptTOC: negative/overlapping offsets
    UnsupportedVersion // euBadVersion: version > CURRENT_TOC_VERS
};

// Decode a raw big-endian TOC image.  `mailbox_size` (if >= 0) enables the
// per-summary range checks the original performed against the mbox file.
// On success the transient fields are reset exactly as ReadTOC did.
std::optional<TableOfContents> decode(std::span<const std::uint8_t> image,
                                      TocError *error = nullptr,
                                      std::int64_t mailbox_size = -1);

// Encode a TableOfContents to the on-disk big-endian image.
// `mailbox_name` fills the legacy FSSpec name field (Str63, MacRoman);
// `mailbox_size` sets boxSize = size + 1 (toc.c:426) and `write_date`
// stamps writeDate.  Pass mailbox_size < 0 to keep the stored values.
std::vector<std::uint8_t> encode(const TableOfContents &toc,
                                 const std::string &mailbox_name,
                                 std::int64_t mailbox_size = -1,
                                 std::uint32_t write_date = 0);

} // namespace eudora::tocfmt
