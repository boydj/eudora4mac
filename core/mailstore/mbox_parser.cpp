#include "mailstore/mbox_parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "compat/hashes.hpp"
#include "compat/macdate.hpp"
#include "mail/rfc2047.hpp"
#include "mailstore/toc_format.hpp"

namespace eudora {

namespace {

// ---- small string helpers (Pascal-string idioms, rewritten) ---------------

bool is_white(char c) { return c == ' ' || c == '\t'; }

std::string_view trim_terminator(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.remove_suffix(1);
    return line;
}

std::string_view trim_white(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool istarts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty())
        return true;
    if (haystack.size() < needle.size())
        return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
        if (iequals(haystack.substr(i, needle.size()), needle))
            return true;
    return false;
}

bool all_digits(std::string_view s) {
    if (s.empty())
        return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

// CopyHeaderLine (buildtoc.c:1302): the value after the colon, trimmed.
std::string_view header_value(std::string_view line) {
    const auto colon = line.find(':');
    if (colon == std::string_view::npos)
        return {};
    std::string_view v = line.substr(colon + 1);
    while (!v.empty() && v.front() == ' ')
        v.remove_prefix(1);
    return trim_white(trim_terminator(v));
}

// ---- TOC header table (STR# 1300) -----------------------------------------

enum TocHeader {
    tchNone = 0,
    tchDate = 1,
    tchFrom = 2,
    tchStatus = 3,
    tchTo = 4,
    tchXPrior = 5,
    tchBcc = 6,
    tchSubject = 7,
    tchImportance = 8,
    tchPrecedence = 9,
    tchMessageId = 10,
};

constexpr std::array<std::string_view, 10> kTocHeaders = {
    "date:",       "from:",       "status:", "to:",        "x-priority:",
    "bcc:",        "subject:",    "Importance:", "Precedence:", "Message-Id:",
};

int find_toc_header(std::string_view name) {
    for (std::size_t i = 0; i < kTocHeaders.size(); ++i)
        if (iequals(name, kTocHeaders[i]))
            return static_cast<int>(i) + 1;
    return 0;
}

// SUM_SENDER_HEADS (STR# 24400): sender candidates, best first.
constexpr std::array<std::string_view, 3> kSenderHeads = {"from:", "reply-to:",
                                                          "sender:"};

int find_sender_head(std::string_view name) {
    for (std::size_t i = 0; i < kSenderHeads.size(); ++i)
        if (iequals(name, kSenderHeads[i]))
            return static_cast<int>(i) + 1;
    return 0;
}

// HEADER_STRN (STR# 1600) prefix used by the out-message heuristic:
// a message whose headers run To:,From:,Subject:,Cc:,Bcc:,X-Attachments:
// in exactly that order was written by Eudora's composer.
constexpr std::array<std::string_view, 6> kCompositionHeads = {
    "To:", "From:", "Subject:", "Cc:", "Bcc:", "X-Attachments:"};
constexpr int kAttachHead = 6;
constexpr int kToHead = 1;

// BulkHeadersStrn (STR# 19500): prefixes marking bulk mail.
constexpr std::array<std::string_view, 2> kBulkHeaderPrefixes = {"errors-to:",
                                                                "list-"};

// Daemon list (STR# 30900) for IsBulk.
constexpr std::array<std::string_view, 4> kDaemons = {"daemon", "listserv",
                                                      "listproc", "majordomo"};

// ImportanceStrn (STR# 28300).
constexpr std::array<std::string_view, 5> kImportance = {"Highest", "High",
                                                         "Normal", "Low", "Lowest"};

// Body-format tags (Enriched STR# 29500 + html): matched after "<" at the
// start of a body line (buildtoc.c:593-603).
constexpr std::string_view kHtmlTag = "html";
constexpr std::string_view kXHtmlTag = "x-html";
constexpr std::string_view kXRichTag = "x-rich";
constexpr std::string_view kXFlowedTag = "x-flowed";
constexpr std::string_view kXCharsetTag = "x-charset";

// IsBulk (buildtoc.c:637), simplified: the header's addresses are checked
// for the well-known daemon substrings.
bool is_bulk_sender(std::string_view header_line) {
    for (auto d : kDaemons)
        if (icontains(header_line, d))
            return true;
    return false;
}

} // namespace

// ---- IsFromLine ------------------------------------------------------------

bool is_from_line(std::string_view line0) {
    std::string_view line = line0;
    if (line.size() < 5 || line.compare(0, 4, "From") != 0)
        return false;
    line.remove_prefix(4);
    if (line.empty() || line.front() != ' ')
        return false;
    line.remove_prefix(1);

    // Skip the return address (quote-aware).
    bool quote = false;
    std::size_t i = 0;
    while (i < line.size() && (quote || line[i] != ' ')) {
        if (line[i] == '"')
            quote = !quote;
        ++i;
    }
    if (i >= line.size())
        return false;
    while (i < line.size() && line[i] == ' ')
        ++i;
    line = line.substr(i);

    // Tokenize the date part and count the pieces (buildtoc.c:779-832).
    int remote = 0, from = 0, weekDay = 0, day = 0, year = 0, tym = 0, month = 0,
        other = 0;
    std::string scratch(trim_terminator(line));
    if (scratch.size() > 254)
        return false;

    static constexpr std::string_view kWeekdays[] = {"mon", "tue", "wed", "thu",
                                                     "fri", "sat", "sun"};
    static constexpr std::string_view kMonths[] = {"jan", "feb", "mar", "apr",
                                                   "may", "jun", "jul", "aug",
                                                   "sep", "oct", "nov", "dec"};

    std::size_t pos = 0;
    const std::string delims = " \t\r,";
    while (pos < scratch.size()) {
        pos = scratch.find_first_not_of(delims, pos);
        if (pos == std::string::npos)
            break;
        std::size_t end = scratch.find_first_of(delims, pos);
        if (end == std::string::npos)
            end = scratch.size();
        const std::string_view cp{scratch.data() + pos, end - pos};
        pos = end;

        const int len = static_cast<int>(cp.size());
        const long num = std::atol(std::string(cp).c_str());
        const auto is_dig = [](char c) { return c >= '0' && c <= '9'; };
        const auto matches_any = [&](auto &&names) {
            for (auto n : names)
                if (iequals(cp, n))
                    return true;
            return false;
        };

        if (num < 24 && (len >= 5 && cp[2] == ':') &&
            (len == 5 || (len == 8 && cp[5] == ':'))) {
            if (tym++)
                return false;
        } else if (!year && day && len == 2 && is_dig(cp[len - 1])) {
            if (year++)
                return false;
        } else if (len <= 2 && num && num < 32) {
            if (day++)
                return false;
        } else if (len == 4 && num > 1900) {
            if (year++)
                return false;
        } else if (len == 6 && iequals(cp, "remote")) {
            if (remote++ || from)
                return false;
        } else if (len == 4 && iequals(cp, "from")) {
            if (!remote || from++)
                return false;
        } else if (len == 3 && matches_any(kWeekdays)) {
            if (weekDay++)
                return false;
        } else if (len == 3 && matches_any(kMonths)) {
            if (month++)
                return false;
        } else {
            ++other;
        }
    }
    return day && year && month && tym && other <= 2;
}

// ---- date parsing ----------------------------------------------------------

std::uint32_t unix_date_to_seconds(std::string_view date) {
    // UnixDate2Secs walks backwards: year, :ss, :mm, hour, day, month.
    std::string copy(trim_terminator(date));
    DateTimeParts dtr;

    auto rfind_and_split = [&](char sep) -> std::string {
        const auto p = copy.find_last_of(sep);
        if (p == std::string::npos)
            return {};
        std::string tail = copy.substr(p + 1);
        copy.resize(p);
        return tail;
    };

    dtr.year = std::atoi(rfind_and_split(' ').c_str());
    if (dtr.year > 0 && dtr.year < 1900)
        dtr.year += 1900;
    dtr.second = std::atoi(rfind_and_split(':').c_str());
    dtr.minute = std::atoi(rfind_and_split(':').c_str());
    dtr.hour = std::atoi(rfind_and_split(' ').c_str());
    dtr.day = std::atoi(rfind_and_split(' ').c_str());
    while (!copy.empty() && copy.back() == ' ')
        copy.pop_back();
    const auto p = copy.find_last_of(' ');
    dtr.month = month_number(p == std::string::npos ? copy : copy.substr(p + 1));

    if (dtr.year && dtr.month)
        return mac_date_to_seconds(dtr);
    return 0;
}

std::uint32_t beautify_date(std::string_view date, long &zone_seconds) {
    zone_seconds = -1;
    DateTimeParts dtr;
    bool in_comment = false;

    std::string s(trim_terminator(date));
    std::size_t pos = 0;
    const std::string delims = " \t,\r";
    bool bail = false;

    static constexpr std::string_view kWeekdays[] = {"Mon", "Tue", "Wed", "Thu",
                                                     "Fri", "Sat", "Sun"};
    static constexpr std::string_view kMonths[] = {"Jan", "Feb", "Mar", "Apr",
                                                   "May", "Jun", "Jul", "Aug",
                                                   "Sep", "Oct", "Nov", "Dec"};

    while (!bail && pos < s.size()) {
        pos = s.find_first_not_of(delims, pos);
        if (pos == std::string::npos)
            break;
        std::size_t end = s.find_first_of(delims, pos);
        if (end == std::string::npos)
            end = s.size();
        std::string token = s.substr(pos, end - pos);
        pos = end;
        if (token.empty())
            continue;

        // comments in dates?  it can happen... (buildtoc.c:977)
        if (token.front() == '(')
            in_comment = true;
        if (token.back() == ')') {
            in_comment = false;
            continue;
        }
        if (in_comment)
            continue;

        // Short token for name lookups (Str31 truncation kept 31 chars).
        std::string short_tok = token.size() > 31 ? token.substr(0, 31) : token;

        long numeric = -1;
        const bool signed_digits =
            token.size() >= 2 && (token[0] == '-' || token[0] == '+') &&
            all_digits(std::string_view(token).substr(1));
        if (signed_digits || all_digits(token))
            numeric = std::atol(token.c_str());

        const auto find_index = [&](auto &&names) {
            int idx = 0;
            for (auto n : names) {
                ++idx;
                if (iequals(short_tok, n))
                    return idx;
            }
            return 0;
        };

        if (find_index(kWeekdays)) {
            continue; // ignore weekdays
        } else if (const int m = find_index(kMonths)) {
            if (dtr.month) {
                bail = true;
                break;
            }
            dtr.month = m;
        } else if (iequals(token, "UT") || iequals(token, "GMT") ||
                   tz_name_to_offset(token) != 0) {
            if (zone_seconds != -1) {
                bail = true;
                break;
            }
            zone_seconds = tz_name_to_offset(token);
        } else if (numeric != -1) {
            if (numeric < 0 || (token.size() == 5 && (token[0] == '-' || token[0] == '+')) ||
                (token.size() == 4 && dtr.year > 0)) {
                if (zone_seconds != -1) {
                    bail = true;
                    break;
                }
                zone_seconds = zone_string_to_offset(token);
            } else if (numeric < 2050 &&
                       (numeric > 1904 || numeric > 31 ||
                        (numeric >= 0 && dtr.day > 0))) {
                if (dtr.year != 0) {
                    bail = true;
                    break;
                }
                if (numeric > 1904)
                    dtr.year = static_cast<int>(numeric);
                else if (numeric > 38)
                    dtr.year = static_cast<int>(numeric) + 1900;
                else
                    dtr.year = static_cast<int>(numeric) + 2000;
            } else if (numeric <= 31 && numeric > 0) {
                if (dtr.day != 0) {
                    bail = true;
                    break;
                }
                dtr.day = static_cast<int>(numeric);
            } else {
                bail = true;
                break;
            }
        } else if (token.size() < 31 && token.find(':') != std::string::npos) {
            // might have a time
            int fields[3] = {0, 0, 0};
            int n = 0;
            std::size_t tpos = 0;
            while (n < 3 && tpos <= token.size()) {
                std::size_t tend = token.find(':', tpos);
                if (tend == std::string::npos)
                    tend = token.size();
                const std::string piece = token.substr(tpos, tend - tpos);
                const bool first = n == 0;
                if (piece.empty() || !all_digits(piece) ||
                    (first ? piece.size() > 2 : piece.size() != 2)) {
                    bail = true;
                    break;
                }
                fields[n++] = std::atoi(piece.c_str());
                if (tend == token.size())
                    break;
                tpos = tend + 1;
            }
            if (bail)
                break;
            dtr.hour = fields[0];
            dtr.minute = fields[1];
            dtr.second = fields[2];
        }
    }

    if (!bail && dtr.day && dtr.month && dtr.year) {
        if (zone_seconds == -1)
            zone_seconds = local_zone_seconds();
        std::uint32_t secs = mac_date_to_seconds(dtr);
        secs -= static_cast<std::uint32_t>(zone_seconds);
        return secs;
    }

    // couldn't get it (buildtoc.c:1069)
    zone_seconds = local_zone_seconds();
    return mac_now_utc();
}

// ---- from/subject beautification -------------------------------------------

static std::string trim_wrap(std::string s, char open_c, char close_c) {
    if (s.size() > 1 && s.front() == open_c && s.back() == close_c)
        return s.substr(1, s.size() - 2);
    return s;
}

std::string beautify_from(std::string_view from0) {
    std::string from(trim_white(trim_terminator(from0)));
    const bool was_not_empty = !from.empty();

    // Elide everything after the last '<' unless it's the first character.
    if (!from.empty() && from.front() != '<') {
        const auto lt = from.find_last_of('<');
        if (lt != std::string::npos) {
            from = from.substr(0, lt);
        } else if (from.front() != '"') {
            // No phrase; prefer parenthesized text.
            const auto op = from.find('(');
            if (op != std::string::npos) {
                const auto cl = from.find(')', op + 1);
                if (cl != std::string::npos) {
                    const std::string inner = from.substr(op + 1, cl - op - 1);
                    // Skip 3-digit comments (looks like a phone extension).
                    if (!inner.empty() && !(inner.size() == 3 && all_digits(inner)))
                        from = inner;
                }
            }
        }
    }
    from = std::string(trim_white(from));
    from = trim_wrap(std::move(from), '(', ')');
    from = trim_wrap(std::move(from), '"', '"');
    if (from.empty() && was_not_empty)
        from = "Unspecified"; // SOME_BOZO
    return from;
}

std::string beautify_subject(std::string_view subject0, bool outlook_fix) {
    std::string subject(trim_terminator(subject0));
    if (!outlook_fix)
        return subject;
    // OUTLOOK_CRAP_FIND "RE:,FW:" -> OUTLOOK_CRAP_FIX "Re:,Fwd:".
    struct Fix {
        std::string_view crap, treasure;
    };
    static constexpr Fix kFixes[] = {{"RE:", "Re:"}, {"FW:", "Fwd:"}};
    for (const auto &f : kFixes) {
        // The original matched case-SENSITIVELY (StartsWith uses memcmp), so
        // only the all-caps Outlook forms are rewritten.
        if (subject.size() >= f.crap.size() &&
            subject.compare(0, f.crap.size(), f.crap) == 0)
            subject = std::string(f.treasure) + subject.substr(f.crap.size());
    }
    return subject;
}

// ---- ReadSum ---------------------------------------------------------------

namespace {

// GleanFrom (buildtoc.c:838).
void glean_from(std::string_view line, MessageSummary &sum) {
    std::string_view rest = trim_terminator(line);
    const auto sp1 = rest.find(' ');
    if (sp1 == std::string_view::npos)
        return;
    rest.remove_prefix(sp1 + 1);
    const auto sp2 = rest.find(' ');
    std::string_view addr = sp2 == std::string_view::npos ? rest : rest.substr(0, sp2);
    sum.from = std::string(addr.substr(0, 255));

    std::string_view date = sp2 == std::string_view::npos
                                ? std::string_view{}
                                : rest.substr(sp2 + 1);
    const long offset = local_zone_seconds();
    const std::uint32_t wall = unix_date_to_seconds(date);
    const std::uint32_t seconds = wall - static_cast<std::uint32_t>(offset);
    sum.seconds = seconds;
    sum.orig_zone = static_cast<std::int16_t>(offset / 60);
    sum.arrival_seconds = wall;
}

void init_summary(MessageSummary &sum, const MboxParseOptions &opt) {
    sum = MessageSummary{};
    sum.state = MessageState::Unread;
    sum.table_id = -1; // DEFAULT_TABLE
    sum.pers_id = sum.pop_pers_id = opt.personality_id;
    if (opt.is_out)
        sum.flags = opt.default_out_flags;
    // Modern core always keeps summaries as UTF-8 (HasUnicode() was true on
    // every OS X system the Carbon build ran on).
    sum.flags |= sumflag::kUtf8;
}

} // namespace

bool MboxScanner::next(MessageSummary &out) {
    enum class State { Begin, InHeader, InBody };
    State state = State::Begin;

    MessageSummary sum;
    init_summary(sum, options_);
    sum.spam_score = 0;

    int maybe_out = -1;
    std::string to_line;
    int sender_head = 1 << 14; // REAL_BIG
    int header_index = 0;
    std::string line;

    const std::uint32_t out_flags = options_.default_out_flags | sumflag::kUtf8;

    for (;;) {
        LineReader::Result type;
        std::int64_t line_tell;
        if (has_pending_) {
            line = pending_line_;
            line_tell = pending_tell_;
            type = LineReader::LineStart;
            has_pending_ = false;
        } else {
            type = reader_.get_line(line, 255);
            line_tell = reader_.tell();
            if (type == LineReader::Eof)
                break;
            if (type == LineReader::Error)
                return false;
        }

        if (type == LineReader::LineMiddle)
            continue;

        if (is_from_line(line)) {
            if (state != State::Begin) {
                // Start of the next message: push the line back and finish.
                pending_line_ = line;
                pending_tell_ = line_tell;
                has_pending_ = true;
                break;
            }
            init_summary(sum, options_);
            sum.spam_score = -1;
            glean_from(line, sum);
            sum.offset = static_cast<std::int32_t>(line_tell);
            sum.state = options_.is_out ? MessageState::Unsendable
                                        : MessageState::Unread;
            state = State::InHeader;
            maybe_out = -1;
            to_line.clear();
            continue;
        }

        if (state == State::Begin) {
            state = State::InHeader;
            // fall through to header handling for this same line
        }

        if (state == State::InHeader) {
            if (line.empty() || line.front() == '\r') {
                // Blank line: end of headers.  bodyOffset points at the blank
                // line itself (TellLine semantics, buildtoc.c:430).
                state = State::InBody;
                sum.body_offset =
                    static_cast<std::int32_t>(line_tell - sum.offset);
                if (!options_.is_out && maybe_out >= kAttachHead + 1) {
                    // Headers ran in exact composition order: this is an
                    // outgoing message stored in a regular mailbox.
                    sum.from = beautify_from(decode_rfc2047(header_value(to_line)));
                    if (!sum.from.empty())
                        sum.state = MessageState::Unsent;
                    sum.flags = out_flags;
                }
                continue;
            }

            const bool continuation = is_white(line.front());
            if (!continuation) {
                // Grab the header name (through the colon).
                std::string_view lv = trim_terminator(line);
                const auto colon = lv.find(':');
                std::string_view header_name;
                if (colon != std::string_view::npos && colon >= 2) {
                    header_name = lv.substr(0, colon + 1);
                    header_index = find_toc_header(header_name);
                } else {
                    header_name = {};
                    header_index = 0;
                }

                // Out-message heuristic: headers in composition order?
                if (!options_.is_out && maybe_out != 0) {
                    if (maybe_out > 0 && maybe_out <= kAttachHead) {
                        if (iequals(header_name,
                                    kCompositionHeads[static_cast<std::size_t>(
                                        maybe_out - 1)]))
                            ++maybe_out;
                        else
                            maybe_out = 0;
                    }
                }

                switch (header_index) {
                case tchDate: {
                    long zone = 0;
                    const std::uint32_t secs =
                        beautify_date(header_value(line), zone);
                    if (secs) {
                        sum.seconds = secs;
                        sum.orig_zone = static_cast<std::int16_t>(zone / 60);
                    }
                    break;
                }
                case tchTo:
                    if (options_.is_out) {
                        sum.from = beautify_from(decode_rfc2047(header_value(line)));
                        if (!sum.from.empty())
                            sum.state = MessageState::Sendable;
                    } else if (maybe_out != 0) {
                        maybe_out = kToHead + 1;
                        to_line = line;
                    }
                    break;
                case tchBcc:
                    if (options_.is_out || maybe_out > 0) {
                        if (options_.is_out &&
                            (sum.from.empty() || sum.from.front() == '?')) {
                            sum.from = beautify_from(decode_rfc2047(header_value(line)));
                            if (!sum.from.empty())
                                sum.state = MessageState::Sendable;
                        } else if (to_line.empty()) {
                            to_line = line;
                        }
                    }
                    break;
                case tchSubject:
                    sum.subject = beautify_subject(decode_rfc2047(header_value(line)),
                                                   options_.outlook_fix);
                    break;
                case tchStatus:
                    // "Status: R..." marks already-read mail (ALREADY_READ).
                    if (options_.believe_status &&
                        icontains(header_value(line), "R"))
                        sum.state = MessageState::Read;
                    break;
                case tchXPrior:
                    if (options_.look_priority) {
                        const long v =
                            std::atol(std::string(header_value(line)).c_str());
                        sum.priority = sum.orig_priority = display_to_priority(v);
                    }
                    break;
                case tchMessageId:
                    if (!valid_hash(sum.msg_id_hash)) {
                        sum.msg_id_hash = mid_hash(header_value(line));
                        if (!valid_hash(sum.uid_hash))
                            sum.uid_hash = sum.msg_id_hash;
                    }
                    break;
                case tchPrecedence:
                    if (!(sum.opts & sumopt::kBulk) &&
                        icontains(header_value(line), "bulk"))
                        sum.opts |= sumopt::kBulk;
                    break;
                case tchImportance:
                    if (options_.look_priority && sum.orig_priority == 0) {
                        const std::string_view v = header_value(line);
                        int idx = 0;
                        for (std::size_t i = 0; i < kImportance.size(); ++i)
                            if (iequals(v, kImportance[i])) {
                                idx = static_cast<int>(i) + 1;
                                break;
                            }
                        sum.priority = sum.orig_priority = display_to_priority(idx);
                    }
                    break;
                default:
                    if (!options_.is_out) {
                        const int sh = find_sender_head(header_name);
                        if (sh) {
                            if (!(sum.opts & sumopt::kBulk) && is_bulk_sender(line))
                                sum.opts |= sumopt::kBulk;
                            if (sh <= sender_head) {
                                sum.from = beautify_from(decode_rfc2047(header_value(line)));
                                sender_head = sh;
                            }
                            break;
                        }
                    }
                    if (!(sum.opts & sumopt::kBulk)) {
                        for (auto p : kBulkHeaderPrefixes)
                            if (istarts_with(header_name, p)) {
                                sum.opts |= sumopt::kBulk;
                                break;
                            }
                    }
                    break;
                }
            } else if (header_index == tchSubject) {
                // Continuation of a wrapped Subject: line.
                std::string cont(trim_white(trim_terminator(line)));
                std::replace(cont.begin(), cont.end(), '\t', ' ');
                sum.subject += beautify_subject(decode_rfc2047(cont), options_.outlook_fix);
            }
            continue;
        }

        // In body: sniff format tags on lines starting with '<'.
        if (state == State::InBody && line.size() > 1 && line.front() == '<') {
            const std::string_view tag = trim_terminator(line).substr(1);
            if (istarts_with(tag, kHtmlTag) || istarts_with(tag, kXHtmlTag))
                sum.opts |= sumopt::kHtml;
            else if (istarts_with(tag, kXFlowedTag))
                sum.opts |= sumopt::kFlowed;
            else if (istarts_with(tag, kXRichTag))
                sum.flags |= sumflag::kRich;
            else if (istarts_with(tag, kXCharsetTag))
                sum.opts |= sumopt::kCharset;
            if ((sum.opts & (sumopt::kHtml | sumopt::kFlowed)) ||
                (sum.flags & sumflag::kRich))
                sum.opts |= sumopt::kCharset;
        }
    }

    if (state == State::Begin)
        return false; // no message found (fnfErr)

    const std::int64_t end = has_pending_ ? pending_tell_ : reader_.position();
    sum.length = static_cast<std::int32_t>(end - sum.offset);
    // Cap the stored strings at their serialized capacity (char[48]/char[60]
    // Pascal strings leave 47/59 usable bytes).
    if (sum.from.size() > tocfmt::sum::fromCap - 1)
        sum.from.resize(tocfmt::sum::fromCap - 1);
    if (sum.subject.size() > tocfmt::sum::subjCap - 1)
        sum.subject.resize(tocfmt::sum::subjCap - 1);
    out = std::move(sum);
    return true;
}

std::optional<TableOfContents> build_toc(const std::filesystem::path &mailbox,
                                         const MboxParseOptions &options) {
    LineReader reader;
    if (!reader.open(mailbox))
        return std::nullopt;

    TableOfContents toc;
    toc.mailbox_path = mailbox;
    toc.next_serial_num = 1;

    const std::string name = mailbox.filename().string();
    if (iequals(name, "In"))
        toc.which = MailboxKind::In;
    else if (iequals(name, "Out"))
        toc.which = MailboxKind::Out;
    else if (iequals(name, "Trash"))
        toc.which = MailboxKind::Trash;
    else if (iequals(name, "Junk"))
        toc.which = MailboxKind::Junk;

    MboxParseOptions opts = options;
    if (toc.which == MailboxKind::Out)
        opts.is_out = true;

    MboxScanner scanner(reader, opts);
    MessageSummary sum;
    while (scanner.next(sum))
        toc.append(std::move(sum));

    std::error_code ec;
    const auto size = std::filesystem::file_size(mailbox, ec);
    toc.recalc_kbytes(ec ? 0 : static_cast<std::int64_t>(size));
    return toc;
}

} // namespace eudora
