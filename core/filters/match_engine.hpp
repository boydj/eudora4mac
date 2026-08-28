// Filter matching — the modern filtrun.c match cascade
// (FilterMatch -> TermMatch -> TermPtrMatch, filtrun.c:1140-1765).
//
// The engine matches a Filter against a message (raw text + its TOC
// summary) and reports which actions fire.  Executing actions is the
// embedding application's job; hooks cover the pieces that lived outside
// the engine (the address book for intersectsFile).

#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "filters/filter_types.hpp"
#include "mailstore/summary.hpp"

namespace eudora {

// What triggered this filtering pass (RightFilterType).
enum class FilterEvent { Incoming, Outgoing, Manual };

struct FilterContext {
    std::string_view raw_message; // headers + body as stored in the mailbox
    const MessageSummary *summary = nullptr; // for junk/priority/date terms

    // intersectsFile support: does `address` appear in the address book
    // `file` ("" = any file)?  Default: never (no address book in core).
    std::function<bool(std::string_view address, std::string_view file)>
        address_in_book;

    // Match ignoring linear-white-space runs (PREF_NO_FILT_LWSP off).
    bool lwsp_insensitive = true;
};

// Does the filter apply to this kind of pass? (RightFilterType)
bool filter_applies(const Filter &filter, FilterEvent event);

// One term against the message (TermMatch).
bool term_matches(const FilterTerm &term, const FilterContext &ctx);

// Whole filter: terms combined by the conjunction (FilterMatch).
bool filter_matches(const Filter &filter, const FilterContext &ctx);

// Run a filter list in order; returns the actions of every matching filter
// in FAPass execution order, stopping at a matching filter with a Stop
// action (TakeFilterAction ordering).
struct FiredAction {
    const Filter *filter;
    FilterAction action;
};
std::vector<FiredAction> run_filters(const std::vector<Filter> &filters,
                                     FilterEvent event, const FilterContext &ctx);

} // namespace eudora
