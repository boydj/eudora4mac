// mbox scanner / TOC builder — the modern buildtoc.c.
//
// Scans a sendmail-format ("From ") mailbox file and produces message
// summaries: envelope detection (IsFromLine), header extraction (ReadSum),
// date normalization (BeautifyDate/UnixDate2Secs), sender/subject cleanup
// (BeautifyFrom/BeautifySubj), and body-format sniffing.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "mailstore/line_reader.hpp"
#include "mailstore/summary.hpp"

namespace eudora {

struct MboxParseOptions {
    bool is_out = false;          // treat as the Out mailbox (To: is the "sender")
    bool look_priority = true;    // honor X-Priority/Importance (PREF_NO_IN_PRIOR off)
    bool believe_status = false;  // honor "Status: R" headers (PREF_BELIEVE_STATUS)
    bool outlook_fix = true;      // rewrite RE:/FW: to Re:/Fwd: (PREF_NO_OUTLOOK_FIX off)
    std::uint32_t default_out_flags = 0; // DefaultOutFlags() for outgoing messages
    std::uint32_t personality_id = 0;    // current personality id
};

// IsFromLine (buildtoc.c:746): is this a sendmail envelope line?  `line`
// must be the raw line, terminator included or not.
bool is_from_line(std::string_view line);

// BeautifyDate (buildtoc.c:956): parse an RFC 822 Date: value.  Returns
// seconds (Mac epoch, UTC) and sets zone_seconds to the message's original
// UTC offset in seconds.  Falls back to "now" when unparseable.
std::uint32_t beautify_date(std::string_view date, long &zone_seconds);

// UnixDate2Secs (buildtoc.c:875): parse the "Wed Jun 14 12:36:18 1989" date
// of an envelope line into wall-clock Mac seconds (no zone applied).
std::uint32_t unix_date_to_seconds(std::string_view date);

// BeautifyFrom (buildtoc.c:1206): reduce a From: value to a display name.
std::string beautify_from(std::string_view from);

// BeautifySubj (buildtoc.c:702): Outlook RE:/FW: fixups.
std::string beautify_subject(std::string_view subject, bool outlook_fix = true);

// Build a table of contents by scanning a mailbox file (BuildTOC).
std::optional<TableOfContents> build_toc(const std::filesystem::path &mailbox,
                                         const MboxParseOptions &options = {});

// Incremental form: parse the next message summary starting at the reader's
// position (ReadSum).  Returns false at end of mailbox.
class MboxScanner {
public:
    MboxScanner(LineReader &reader, const MboxParseOptions &options)
        : reader_(reader), options_(options) {}

    bool next(MessageSummary &out);

private:
    LineReader &reader_;
    MboxParseOptions options_;
    std::string pending_line_;
    std::int64_t pending_tell_ = 0;
    bool has_pending_ = false;
};

} // namespace eudora
