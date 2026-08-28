// In-memory model of Eudora's mailbox table of contents.
//
// This replaces the legacy TOCType/MSumType Handle block (Include/mailbox.h)
// with plain C++ objects.  Field names and semantics follow the original;
// live pointers, window state, and Handle bookkeeping are gone.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace eudora {

// StateEnum (Include/mailbox.h:27-43).
enum class MessageState : std::uint8_t {
    Unread = 1,
    Read = 2,
    Replied = 3,
    Redistributed = 4,
    Unsendable = 5,
    Sendable = 6,
    Queued = 7,
    Forwarded = 8,
    Sent = 9,
    Unsent = 10,
    Timed = 11,
    BusySending = 12,
    Error = 13,
    Rebuilt = 14,
};

// FLAG_* bits stored in MessageSummary::flags (Include/mailbox.h:227-261).
namespace sumflag {
inline constexpr std::uint32_t kSkipWarn = 1u << 0;
inline constexpr std::uint32_t kOldSig = 1u << 1;
inline constexpr std::uint32_t kBinHexText = 1u << 2;
inline constexpr std::uint32_t kWrapOut = 1u << 3;
inline constexpr std::uint32_t kKeepCopy = 1u << 4;
inline constexpr std::uint32_t kTabs = 1u << 5;
inline constexpr std::uint32_t kAttachTypeLo = 1u << 6;
inline constexpr std::uint32_t kAttachTypeHi = 1u << 7;
inline constexpr std::uint32_t kEncodedBody = 1u << 8;
inline constexpr std::uint32_t kCanEncode = 1u << 9;
inline constexpr std::uint32_t kRich = 1u << 10;
inline constexpr std::uint32_t kShowAll = 1u << 11;
inline constexpr std::uint32_t kReturnReceipt = 1u << 12;
inline constexpr std::uint32_t kHasAttachments = 1u << 13;
// bits 14-17 hold the label color (SumColor).
inline constexpr std::uint32_t kFixedWidth = 1u << 18;
inline constexpr std::uint32_t kKnowsMe = 1u << 19;
inline constexpr std::uint32_t kSign = 1u << 21;
inline constexpr std::uint32_t kEncrypt = 1u << 22;
inline constexpr std::uint32_t kUnfiltered = 1u << 23;
inline constexpr std::uint32_t kFirstOfSplit = 1u << 24;
inline constexpr std::uint32_t kSubsequentOfSplit = 1u << 25;
inline constexpr std::uint32_t kSkipped = 1u << 26;
inline constexpr std::uint32_t kOutgoing = 1u << 27;
inline constexpr std::uint32_t kAddressError = 1u << 28;
inline constexpr std::uint32_t kZoomed = 1u << 29;
inline constexpr std::uint32_t kIconBar = 1u << 30;
inline constexpr std::uint32_t kUtf8 = 1u << 31;
} // namespace sumflag

// OPT_* bits stored in MessageSummary::opts (Include/mailbox.h:263-295).
namespace sumopt {
inline constexpr std::uint32_t kOpen = 1u << 0;
inline constexpr std::uint32_t kReport = 1u << 1;
inline constexpr std::uint32_t kNotify = 1u << 2;
inline constexpr std::uint32_t kWipe = 1u << 3;
inline constexpr std::uint32_t kWrite = 1u << 4;
inline constexpr std::uint32_t kWillSelect = 1u << 5;
inline constexpr std::uint32_t kAttachDelete = 1u << 6;
inline constexpr std::uint32_t kLocked = 1u << 7;
inline constexpr std::uint32_t kEdited = 1u << 8;
inline constexpr std::uint32_t kWeirdReply = 1u << 9;
inline constexpr std::uint32_t kBulk = 1u << 10;
inline constexpr std::uint32_t kHtml = 1u << 11;
inline constexpr std::uint32_t kReceipt = 1u << 12;
inline constexpr std::uint32_t kCompToolbarVisible = 1u << 13;
inline constexpr std::uint32_t kAutoOpened = 1u << 14;
inline constexpr std::uint32_t kHasSpool = 1u << 15;
inline constexpr std::uint32_t kBloat = 1u << 16;
inline constexpr std::uint32_t kStrip = 1u << 17;
inline constexpr std::uint32_t kDeleted = 1u << 18;
inline constexpr std::uint32_t kFlowed = 1u << 19;
inline constexpr std::uint32_t kFetchAttachments = 1u << 20;
inline constexpr std::uint32_t kJustExcerpt = 1u << 21;
inline constexpr std::uint32_t kOrphanAttachments = 1u << 22;
inline constexpr std::uint32_t kCharset = 1u << 23;
inline constexpr std::uint32_t kRedirected = 1u << 25;
inline constexpr std::uint32_t kImapSent = 1u << 26;
inline constexpr std::uint32_t kSendRegInfo = 1u << 27;
inline constexpr std::uint32_t kInlineSig = 1u << 28;
inline constexpr std::uint32_t kTranslatorDelete = 1u << 29;
inline constexpr std::uint32_t kDelSp = 1u << 30;
} // namespace sumopt

// Hash sentinels (Include/mailbox.h:142-144).
inline constexpr std::uint32_t kNeverHashed = 0;
inline constexpr std::uint32_t kNoMessageId = 0xFFFFFFFFu;
inline constexpr bool valid_hash(std::uint32_t h) {
    return h != kNeverHashed && h != kNoMessageId;
}

// Special-mailbox tags stored in TableOfContents::which.  The values are the
// legacy STR# resource ids that named the boxes (StringDefs.h).
enum class MailboxKind : std::int16_t {
    Regular = 0,
    In = 6220,
    Out = 6805,
    Trash = 7412,
    Junk = 32468,
    InTemp = 9520,
    OutTemp = 9701,
};

// Legacy QuickDraw Rect (saved window position).
struct SavedRect {
    std::int16_t top = 0, left = 0, bottom = 0, right = 0;
};

// One message summary — the modern MSumType.
struct MessageSummary {
    std::int32_t offset = 0;      // byte offset of the message in the mbox file
    std::int32_t length = 0;      // total length in bytes
    std::int32_t body_offset = 0; // body start, relative to offset
    MessageState state = MessageState::Unread;
    std::int8_t spam_score = 0;      // signed 8-bit (spamScore:8)
    std::uint8_t spam_because = 0;   // 3-bit provenance (spamBecause:3)
    std::uint32_t arrival_seconds = 0; // Mac epoch, when it arrived here
    std::uint32_t from_hash = 0;
    std::int32_t serial_num = 0;
    std::uint32_t seconds = 0; // Mac epoch UTC of the Date: header
    std::uint32_t flags = 0;   // FLAG_* bits
    SavedRect saved_pos;       // saved window position
    std::uint8_t priority = 0; // 1-200 internally, 1-5 for display
    std::uint8_t orig_priority = 0;
    std::int16_t table_id = -1; // translation table (DEFAULT_TABLE)
    std::int8_t score = 0;      // 4-bit mood score
    std::uint8_t out_type = 0;  // OutTypeEnum
    std::int16_t orig_zone = 0; // original UTC offset, minutes
    std::uint32_t sig_id = 0;
    std::string from; // UTF-8; at most 47 bytes when serialized
    std::uint32_t pop_pers_id = 0;
    std::uint32_t pers_id = 0;
    std::uint32_t msg_id_hash = 0;
    std::int16_t subj_id = 0;
    std::string subject; // UTF-8; at most 59 bytes when serialized
    std::uint32_t opts = 0; // OPT_* bits
    std::uint32_t uid_hash = 0;

    bool is_queued() const {
        return state == MessageState::Queued || state == MessageState::Timed;
    }
    int label_color() const { return static_cast<int>((flags >> 14) & 0xF); }
    // Prior2Display: 1-200 internal priority to the 1-5 display scale.
    int display_priority() const {
        if (priority == 0)
            return 3;
        const int p = priority > 200 ? 200 : priority;
        return (p + 20) / 40;
    }
};

// Display2Prior (boxact.h:58): 1-5 display priority back to the 1-200
// internal scale (inverse of MessageSummary::display_priority).
inline std::uint8_t display_to_priority(long display) {
    const long p = display * 40;
    if (p <= 0)
        return 0;
    return static_cast<std::uint8_t>(p > 200 ? 200 : p);
}

// TOC versioning (Include/mailbox.h:130-137).
inline constexpr std::int16_t kCurrentTocVersion = 1;
inline constexpr std::int16_t kCurrentTocMinorVersion = 9;

// Expiration units (expUnits bitfield).
enum class ExpireUnits : std::uint8_t { Days = 0, Weeks = 1, Months = 2, Years = 3 };

// The modern TOCType: metadata plus the summary array.
struct TableOfContents {
    std::int16_t version = kCurrentTocVersion;
    std::int16_t minor_version = kCurrentTocMinorVersion;
    MailboxKind which = MailboxKind::Regular;
    bool imap = false;    // imapTOC bit
    bool virtual_box = false; // virtualTOC bit
    std::int32_t last_sort = 0;
    std::uint8_t sorts[12] = {}; // per-column sort indicators
    std::int16_t preview_hi = 0; // % of window used for preview
    std::int32_t next_serial_num = 1;
    std::int32_t unread_count = 0;
    std::uint32_t used_k = 0;  // Kbytes referenced by summaries
    std::uint32_t total_k = 0; // Kbytes in the mailbox file
    bool does_expire = false;
    ExpireUnits expire_units = ExpireUnits::Days;
    std::uint32_t expire_interval = 0;
    std::uint32_t write_date = 0; // mailbox mod date when TOC was written
    std::int32_t box_size = 0;    // mailbox size + 1 at last write (toc.c:426)
    std::vector<MessageSummary> sums;

    // Not serialized: where the mailbox lives now (replaces the FSSpec).
    std::filesystem::path mailbox_path;

    int count() const { return static_cast<int>(sums.size()); }

    // Append a summary, assigning it the next serial number
    // (SaveMessageSum, mailbox.c:598).
    void append(MessageSummary sum);

    // Remove the summary at index (DeleteSum, mailbox.c:1204).
    bool remove(int index);

    // FindSumByHash / FindSumBySerialNum (mailbox.c).  Return -1 if absent.
    int find_by_hash(std::uint32_t uid_hash) const;
    int find_by_serial(std::int32_t serial_num) const;

    // GetTOCK (toc.c:1105): recompute used/total K from the summaries and
    // the given mailbox file size.
    void recalc_kbytes(std::int64_t mailbox_size);

    // FindTOCSpot (buildtoc.c:1355): end of the last live message — where a
    // new message would be appended.
    std::int32_t append_spot() const;
};

} // namespace eudora
