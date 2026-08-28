#include "filters/match_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "compat/macdate.hpp"
#include "filters/regexp.hpp"
#include "mail/address_parser.hpp"
#include "mail/header_parser.hpp"

namespace eudora {

namespace {

bool is_white(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

char lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

// PPtrMatchLWSP: case-insensitive match where any run of linear whitespace
// in either string matches any run in the other.  anchor_start/anchor_end
// select is/starts/ends behavior; neither anchored = contains.
bool lwsp_match(std::string_view needle, std::string_view hay, bool anchor_start,
                bool anchor_end) {
    // Canonicalize both sides: collapse whitespace runs to single spaces.
    const auto canon = [](std::string_view s) {
        std::string out;
        bool in_ws = false;
        for (char c : s) {
            if (is_white(c)) {
                in_ws = true;
            } else {
                if (in_ws && !out.empty())
                    out += ' ';
                in_ws = false;
                out += lower(c);
            }
        }
        return out;
    };
    const std::string n = canon(needle);
    const std::string h = canon(hay);
    if (n.empty())
        return h.empty() || !(anchor_start && anchor_end);
    if (anchor_start && anchor_end)
        return n == h;
    if (anchor_start)
        return h.compare(0, n.size(), n) == 0;
    if (anchor_end)
        return h.size() >= n.size() &&
               h.compare(h.size() - n.size(), n.size(), n) == 0;
    return h.find(n) != std::string::npos;
}

bool plain_ifind(std::string_view needle, std::string_view hay) {
    if (needle.empty())
        return true;
    if (hay.size() < needle.size())
        return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
        if (iequals(hay.substr(i, needle.size()), needle))
            return true;
    return false;
}

// The negative verbs invert after a positive match (TermMatch done-block).
bool is_negative_verb(FilterVerb v) {
    return v == FilterVerb::Isnt || v == FilterVerb::NotContains ||
           v == FilterVerb::NotAppears || v == FilterVerb::NotIntersects ||
           v == FilterVerb::NotIntersectsFile;
}

// TermPtrMatch (filtrun.c:1491): match a term's verb against a text span.
bool term_ptr_match(const FilterTerm &term, std::string_view text,
                    const FilterContext &ctx) {
    switch (term.verb) {
    case FilterVerb::Regex: {
        if (!term.regex_cache)
            term.regex_cache = Regexp::compile(term.value);
        if (!term.regex_cache)
            return false;
        return term.regex_cache->search(text).has_value();
    }
    case FilterVerb::Contains:
    case FilterVerb::NotContains:
        return ctx.lwsp_insensitive ? lwsp_match(term.value, text, false, false)
                                    : plain_ifind(term.value, text);
    case FilterVerb::Is:
    case FilterVerb::Isnt:
        return ctx.lwsp_insensitive
                   ? lwsp_match(term.value, text, true, true)
                   : (text.size() == term.value.size() &&
                      iequals(term.value, text));
    case FilterVerb::Starts:
        return ctx.lwsp_insensitive
                   ? lwsp_match(term.value, text, true, false)
                   : (text.size() >= term.value.size() &&
                      iequals(term.value, text.substr(0, term.value.size())));
    case FilterVerb::Ends:
        return ctx.lwsp_insensitive
                   ? lwsp_match(term.value, text, false, true)
                   : (text.size() >= term.value.size() &&
                      iequals(term.value,
                              text.substr(text.size() - term.value.size())));
    case FilterVerb::Appears:
    case FilterVerb::NotAppears:
        return true; // presence decided by the caller
    case FilterVerb::Intersects:
    case FilterVerb::NotIntersects: {
        // DoesIntersectNick without nickname expansion: the term's value is
        // itself an address list.
        auto want = parse_addresses(term.value, false);
        auto have = parse_addresses(text, false);
        if (!want || !have)
            return false;
        for (const auto &a : *have)
            for (const auto &b : *want)
                if (iequals(a, b))
                    return true;
        return false;
    }
    case FilterVerb::IntersectsFile:
    case FilterVerb::NotIntersectsFile: {
        if (!ctx.address_in_book)
            return false;
        auto have = parse_addresses(text, false);
        if (!have)
            return false;
        std::string_view file = term.value;
        for (const auto &a : *have)
            if (ctx.address_in_book(a, file))
                return true;
        return false;
    }
    case FilterVerb::JunkLess:
    case FilterVerb::JunkMore:
        return false; // only meaningful for the junk meta-header
    }
    return false;
}

// Special meta-header names the filter editor wrote.
bool is_body_header(std::string_view h) {
    return iequals(h, "«body»") || iequals(h, "<<body>>") || iequals(h, "Body");
}
bool is_any_header(std::string_view h) {
    return iequals(h, "«any header»") || iequals(h, "<<any header>>");
}
bool is_junk_header(std::string_view h) {
    return iequals(h, "«junk score»") || iequals(h, "<<junk score>>") ||
           iequals(h, "Junk");
}

std::string_view strip_colon(std::string_view h) {
    if (!h.empty() && h.back() == ':')
        h.remove_suffix(1);
    return h;
}

// TermJunkMatch (filtrun.c:1631).
bool junk_match(const FilterTerm &term, const MessageSummary &sum) {
    const long num = std::atol(term.value.c_str());
    switch (term.verb) {
    case FilterVerb::Is: return sum.spam_score == num;
    case FilterVerb::Isnt: return sum.spam_score != num;
    case FilterVerb::JunkMore: return sum.spam_score > num;
    case FilterVerb::JunkLess: return sum.spam_score < num;
    default: return false;
    }
}

// TermDateMatch: matches against the displayed local date string.
bool date_match(const FilterTerm &term, const MessageSummary &sum,
                const FilterContext &ctx) {
    const DateTimeParts p =
        mac_seconds_to_date(sum.seconds + static_cast<std::uint32_t>(
                                              sum.orig_zone * 60));
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d %d/%d/%d", p.hour, p.minute,
                  p.month, p.day, p.year % 100);
    bool m = term_ptr_match(term, buf, ctx);
    if (term.verb == FilterVerb::Isnt || term.verb == FilterVerb::NotContains)
        m = !m;
    return m;
}

} // namespace

bool filter_applies(const Filter &filter, FilterEvent event) {
    switch (event) {
    case FilterEvent::Incoming: return filter.incoming;
    case FilterEvent::Outgoing: return filter.outgoing;
    case FilterEvent::Manual: return filter.manual;
    }
    return false;
}

bool term_matches(const FilterTerm &term, const FilterContext &ctx) {
    const auto parts = split_message(ctx.raw_message);

    if (!term.header.empty() && !is_body_header(term.header)) {
        // Junk / date meta terms first (TermMatch, filtrun.c:1232-1246).
        if (is_junk_header(term.header)) {
            return ctx.summary ? junk_match(term, *ctx.summary) : false;
        }
        if (iequals(strip_colon(term.header), "date") && ctx.summary) {
            return date_match(term, *ctx.summary, ctx);
        }

        const HeaderSet hs = HeaderSet::parse(parts.header_block);
        const bool any = is_any_header(term.header);
        const std::string_view want = strip_colon(term.header);

        bool found = false;
        for (const auto &f : hs.fields()) {
            if (!any && !iequals(f.name, want))
                continue;
            found = true;
            if (term.verb == FilterVerb::Appears)
                return true;
            if (term.verb == FilterVerb::NotAppears)
                return false;
            bool m = term_ptr_match(term, f.value, ctx);
            if (m) {
                // Negative verbs invert a positive hit (filtrun.c:1327).
                return !is_negative_verb(term.verb);
            }
        }
        (void)found;
        // No instance matched: negatives succeed (filtrun.c:1341).
        return is_negative_verb(term.verb);
    }

    // Body (or unnamed) term.
    bool m = term_ptr_match(term, parts.body, ctx);
    if (is_negative_verb(term.verb))
        m = !m;
    return m;
}

bool filter_matches(const Filter &filter, const FilterContext &ctx) {
    const bool m0 = term_matches(filter.terms[0], ctx);
    switch (filter.conjunction) {
    case FilterConjunction::Ignore:
        return m0;
    case FilterConjunction::And:
        return m0 && term_matches(filter.terms[1], ctx);
    case FilterConjunction::Or:
        return m0 || term_matches(filter.terms[1], ctx);
    case FilterConjunction::Unless:
        return m0 && !term_matches(filter.terms[1], ctx);
    }
    return m0;
}

std::vector<FiredAction> run_filters(const std::vector<Filter> &filters,
                                     FilterEvent event,
                                     const FilterContext &ctx) {
    std::vector<FiredAction> fired;
    bool stopped = false;
    for (const auto &f : filters) {
        if (stopped)
            break;
        if (!filter_applies(f, event))
            continue;
        if (!filter_matches(f, ctx))
            continue;
        for (const auto &a : f.actions) {
            if (a.keyword == FilterKeyword::None)
                continue;
            fired.push_back({&f, a});
            if (a.keyword == FilterKeyword::Stop)
                stopped = true;
        }
    }
    // Execute in FAPass order, preserving filter order within a pass
    // (TakeFilterAction, filtrun.c:1965).
    std::stable_sort(fired.begin(), fired.end(),
                     [](const FiredAction &a, const FiredAction &b) {
                         return filter_action_pass(a.action.keyword) <
                                filter_action_pass(b.action.keyword);
                     });
    return fired;
}

} // namespace eudora
