#include "filters/filter_file.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace eudora {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

struct KeywordName {
    FilterKeyword keyword;
    std::string_view name;
};

// FILT_CMD_STRN (FiltDefs.r, STR# 25200) — the on-disk keywords.
constexpr KeywordName kKeywords[] = {
    {FilterKeyword::None, "none"},
    {FilterKeyword::Status, "status"},
    {FilterKeyword::Priority, "priority"},
    {FilterKeyword::Label, "label"},
    {FilterKeyword::Personality, "personality"},
    {FilterKeyword::Subject, "subject"},
    {FilterKeyword::Sound, "sound"},
    {FilterKeyword::Speak, "speak"},
    {FilterKeyword::OpenMessage, "open"},
    {FilterKeyword::Print, "print"},
    {FilterKeyword::AddHistory, "addhistory"},
    {FilterKeyword::NotifyUser, "notifyUser"},
    {FilterKeyword::Forward, "forward"},
    {FilterKeyword::Redirect, "redirect"},
    {FilterKeyword::Reply, "reply"},
    {FilterKeyword::ServerOpts, "serverOpt"},
    {FilterKeyword::Copy, "copy"},
    {FilterKeyword::Transfer, "transfer"},
    {FilterKeyword::Junk, "junk"},
    {FilterKeyword::MoveAttach, "mvattach"},
    {FilterKeyword::Stop, "stop"},
    {FilterKeyword::Rule, "rule"},
    {FilterKeyword::Incoming, "incoming"},
    {FilterKeyword::Outgoing, "outgoing"},
    {FilterKeyword::Manual, "manual"},
    {FilterKeyword::Header, "header"},
    {FilterKeyword::Verb, "verb"},
    {FilterKeyword::Value, "value"},
    {FilterKeyword::Conjunction, "conjunction"},
    {FilterKeyword::Name, "name"},
    {FilterKeyword::CopyInstead, "copyInstead"},
    {FilterKeyword::Raise, "raise"},
    {FilterKeyword::Lower, "lower"},
    {FilterKeyword::Id, "id"},
    {FilterKeyword::MiniMessage, "miniMessage"},
    {FilterKeyword::MiniMailbox, "miniMailbox"},
    {FilterKeyword::Delivery, "delivery"},
};

// STR# 25400 — verbs, in MatchEnum order.
constexpr std::string_view kVerbs[] = {
    "contains", "!contains", "is", "!is", "starts", "ends", "appears",
    "!appears", "intersects", "disjoint", "intersectsFile", "disjointFile",
    "regex", "less", "greater",
};

// STR# 18300 — conjunctions.
constexpr std::string_view kConjunctions[] = {"ignore", "and", "or", "unless"};

} // namespace

std::string_view filter_keyword_string(FilterKeyword keyword) {
    for (const auto &k : kKeywords)
        if (k.keyword == keyword)
            return k.name;
    return "";
}

FilterKeyword filter_keyword_from_string(std::string_view s) {
    for (const auto &k : kKeywords)
        if (iequals(k.name, s))
            return k.keyword;
    return FilterKeyword::Unknown;
}

std::string_view filter_verb_string(FilterVerb verb) {
    const int i = static_cast<int>(verb) - 1;
    if (i < 0 || i >= static_cast<int>(std::size(kVerbs)))
        return "";
    return kVerbs[i];
}

std::optional<FilterVerb> filter_verb_from_string(std::string_view s) {
    for (std::size_t i = 0; i < std::size(kVerbs); ++i)
        if (iequals(kVerbs[i], s))
            return static_cast<FilterVerb>(i + 1);
    return std::nullopt;
}

std::string_view filter_conjunction_string(FilterConjunction c) {
    const int i = static_cast<int>(c) - 1;
    if (i < 0 || i >= static_cast<int>(std::size(kConjunctions)))
        return "";
    return kConjunctions[i];
}

std::optional<FilterConjunction>
filter_conjunction_from_string(std::string_view s) {
    for (std::size_t i = 0; i < std::size(kConjunctions); ++i)
        if (iequals(kConjunctions[i], s))
            return static_cast<FilterConjunction>(i + 1);
    return std::nullopt;
}

int filter_action_pass(FilterKeyword k) {
    switch (k) {
    case FilterKeyword::Status: return 2;
    case FilterKeyword::Subject:
    case FilterKeyword::Forward:
    case FilterKeyword::Redirect:
    case FilterKeyword::Reply: return 1;
    case FilterKeyword::Copy:
    case FilterKeyword::MoveAttach: return 3;
    case FilterKeyword::Transfer:
    case FilterKeyword::Junk: return 4;
    case FilterKeyword::Stop: return 9;
    default: return 0;
    }
}

bool filter_keyword_is_action(FilterKeyword k) {
    switch (k) {
    case FilterKeyword::None:
    case FilterKeyword::Status:
    case FilterKeyword::Priority:
    case FilterKeyword::Label:
    case FilterKeyword::Personality:
    case FilterKeyword::Subject:
    case FilterKeyword::Sound:
    case FilterKeyword::Speak:
    case FilterKeyword::OpenMessage:
    case FilterKeyword::Print:
    case FilterKeyword::AddHistory:
    case FilterKeyword::NotifyUser:
    case FilterKeyword::Forward:
    case FilterKeyword::Redirect:
    case FilterKeyword::Reply:
    case FilterKeyword::ServerOpts:
    case FilterKeyword::Copy:
    case FilterKeyword::Transfer:
    case FilterKeyword::Junk:
    case FilterKeyword::MoveAttach:
    case FilterKeyword::Stop:
        return true;
    default:
        return false;
    }
}

std::vector<Filter> parse_filters(std::string_view text) {
    std::vector<Filter> filters;
    Filter fr;
    int term = 0;

    const auto flush = [&]() {
        if (!fr.name.empty())
            filters.push_back(fr);
        fr = Filter{};
        term = 0;
    };

    std::size_t pos = 0;
    while (pos <= text.size()) {
        // next line (CR, LF, or CRLF)
        std::size_t end = pos;
        while (end < text.size() && text[end] != '\r' && text[end] != '\n')
            ++end;
        std::string_view line = text.substr(pos, end - pos);
        if (end < text.size() && text[end] == '\r' && end + 1 < text.size() &&
            text[end + 1] == '\n')
            ++end;
        const bool at_end = end >= text.size();
        pos = end + 1;

        // GetFilterLine: skip blanks and '#' comments.
        if (!line.empty() && line.front() != '#') {
            std::size_t sp = 0;
            while (sp < line.size() && line[sp] != ' ')
                ++sp;
            const std::string_view keyStr = line.substr(0, sp);
            std::string value(sp < line.size() ? line.substr(sp + 1) : "");

            FilterKeyword key = filter_keyword_from_string(keyStr);

            // Obsolete keys (ReadFilters prescan, filtmng.c:225-235).
            if (key == FilterKeyword::Raise) {
                key = FilterKeyword::Priority;
                value = "7";
            } else if (key == FilterKeyword::Lower) {
                key = FilterKeyword::Priority;
                value = "8";
            }

            if (filter_keyword_is_action(key)) {
                fr.actions.push_back({key, value});
            } else {
                switch (key) {
                case FilterKeyword::Rule:
                    flush();
                    fr.name = value;
                    break;
                case FilterKeyword::Incoming:
                    fr.incoming = true;
                    break;
                case FilterKeyword::Outgoing:
                    fr.outgoing = true;
                    break;
                case FilterKeyword::Manual:
                    fr.manual = true;
                    break;
                case FilterKeyword::CopyInstead:
                    // Rewrites the preceding transfer into a copy.
                    if (!fr.actions.empty() &&
                        fr.actions.back().keyword == FilterKeyword::Transfer)
                        fr.actions.back().keyword = FilterKeyword::Copy;
                    break;
                case FilterKeyword::Id:
                    fr.id = std::atoi(value.c_str());
                    break;
                case FilterKeyword::Header:
                    fr.terms[term].header = value;
                    break;
                case FilterKeyword::Verb:
                    if (auto v = filter_verb_from_string(value))
                        fr.terms[term].verb = *v;
                    break;
                case FilterKeyword::Value:
                    fr.terms[term].value = value;
                    break;
                case FilterKeyword::Conjunction:
                    if (auto c = filter_conjunction_from_string(value))
                        fr.conjunction = *c;
                    if (term == 0)
                        ++term; // FILTER_OVERTERM warning otherwise
                    break;
                default:
                    break; // unknown keyword: skipped (with an alert, once)
                }
            }
        }
        if (at_end)
            break;
    }
    flush();
    return filters;
}

std::optional<std::vector<Filter>>
read_filters(const std::filesystem::path &file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return std::vector<Filter>{}; // fnfErr => vacuous success
    std::ifstream f(file, std::ios::binary);
    if (!f)
        return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    return parse_filters(text);
}

std::string serialize_filters(const std::vector<Filter> &filters) {
    // WriteFilter (filtmng.c:702), CR-terminated lines like the original.
    std::string out;
    std::int32_t next_id = 1;
    for (const auto &f : filters)
        if (f.id >= next_id)
            next_id = f.id + 1;

    const auto key_line = [&out](FilterKeyword k, std::string_view value) {
        if (value.empty())
            return;
        out += filter_keyword_string(k);
        out += ' ';
        out += value;
        out += '\r';
    };
    const auto bool_line = [&out](FilterKeyword k, bool value) {
        if (!value)
            return;
        out += filter_keyword_string(k);
        out += '\r';
    };

    for (const auto &f : filters) {
        key_line(FilterKeyword::Rule, f.name);
        const std::int32_t id = f.id ? f.id : next_id++;
        key_line(FilterKeyword::Id, std::to_string(id));
        bool_line(FilterKeyword::Incoming, f.incoming);
        bool_line(FilterKeyword::Outgoing, f.outgoing);
        bool_line(FilterKeyword::Manual, f.manual);
        key_line(FilterKeyword::Header, f.terms[0].header);
        key_line(FilterKeyword::Verb, filter_verb_string(f.terms[0].verb));
        key_line(FilterKeyword::Value, f.terms[0].value);
        if (f.conjunction != FilterConjunction::Ignore) {
            key_line(FilterKeyword::Conjunction,
                     filter_conjunction_string(f.conjunction));
            key_line(FilterKeyword::Header, f.terms[1].header);
            key_line(FilterKeyword::Verb, filter_verb_string(f.terms[1].verb));
            key_line(FilterKeyword::Value, f.terms[1].value);
        }
        for (const auto &a : f.actions) {
            if (a.keyword == FilterKeyword::None)
                continue;
            if (a.value.empty()) {
                out += filter_keyword_string(a.keyword);
                out += '\r';
            } else {
                key_line(a.keyword, a.value);
            }
        }
    }
    return out;
}

bool write_filters(const std::vector<Filter> &filters,
                   const std::filesystem::path &file) {
    const std::string text = serialize_filters(filters);
    std::filesystem::path tmp = file;
    tmp += ".temp"; // TEMP_SUFFIX
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f)
            return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec); // ExchangeAndDel
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace eudora
