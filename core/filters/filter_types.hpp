// Filter data model — the modern Include/filters.h + Include/FiltDefs.h.
//
// FilterRecord/MatchTerm/FAction become plain structs.  The 19-opcode
// FActionProc vtable that fused engine, persistence, and editor UI is gone:
// actions are pure data here (keyword + parameter); executing UI-bound
// actions is the embedding application's job.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eudora {

// FilterKeywordEnum (Include/FiltDefs.h), with the dead dash separators
// dropped from the modern enum but the on-disk keyword strings preserved.
enum class FilterKeyword : std::uint8_t {
    None = 1,
    Status = 3,
    Priority = 4,
    Label = 5,
    Personality = 6,
    Subject = 7,
    Sound = 9,
    Speak = 10,
    OpenMessage = 11,
    Print = 12,
    AddHistory = 13,
    NotifyUser = 14,
    Forward = 16,
    Redirect = 17,
    Reply = 18,
    ServerOpts = 20,
    Copy = 22,
    Transfer = 23,
    Junk = 24,
    MoveAttach = 25,
    Stop = 27,
    Rule = 28,
    Incoming = 29,
    Outgoing = 30,
    Manual = 31,
    Header = 32,
    Verb = 33,
    Value = 34,
    Conjunction = 35,
    Name = 36,
    CopyInstead = 37,
    Raise = 38,
    Lower = 39,
    Id = 40,
    MiniMessage = 41,
    MiniMailbox = 42,
    Delivery = 64,
    Unknown = 0,
};

// MatchEnum (filters.h:74-91).
enum class FilterVerb : std::uint8_t {
    Contains = 1,
    NotContains,
    Is,
    Isnt,
    Starts,
    Ends,
    Appears,
    NotAppears,
    Intersects,
    NotIntersects,
    IntersectsFile,
    NotIntersectsFile,
    Regex,
    JunkLess,
    JunkMore,
};

// ConjunctionEnum (filters.h:93-100).
enum class FilterConjunction : std::uint8_t { Ignore = 1, And, Or, Unless };

class Regexp; // filters/regexp.hpp

// MatchTerm.
struct FilterTerm {
    std::string header; // "" or a header name (with or without colon), or the
                        // meta names «Body»/«Any Header» the UI wrote
    std::string value;
    FilterVerb verb = FilterVerb::Contains;

    // Compiled-regex cache (mt->regex).
    mutable std::shared_ptr<Regexp> regex_cache;
};

// FAction: keyword + raw parameter, round-tripped verbatim.
struct FilterAction {
    FilterKeyword keyword = FilterKeyword::None;
    std::string value;
};

// FilterRecord.
struct Filter {
    std::string name;
    std::int32_t id = 0; // FilterUse id
    bool incoming = false;
    bool outgoing = false;
    bool manual = false;
    FilterTerm terms[2];
    FilterConjunction conjunction = FilterConjunction::Ignore;
    std::vector<FilterAction> actions;
};

// FAPass (FiltDefs.c:74): execution ordering pass for an action, 0-9;
// transfers/junk run late, stop runs last.
int filter_action_pass(FilterKeyword keyword);
// True for keywords that are actions (FATable non-nil, FiltDefs.c:43).
bool filter_keyword_is_action(FilterKeyword keyword);

} // namespace eudora
