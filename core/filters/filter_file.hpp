// The "Eudora Filters" text file — the modern filtmng.c persistence layer.
//
// One "keyword [value]" per line, keywords from the FILT_CMD_STRN table,
// '#' comments and blank lines skipped (GetFilterLine, filtmng.c:560).
// Writing is atomic (temp file + rename), like SaveFiltersLo.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "filters/filter_types.hpp"

namespace eudora {

// Keyword <-> string (FILT_CMD_STRN, FiltDefs.r resource 25200).
std::string_view filter_keyword_string(FilterKeyword keyword);
FilterKeyword filter_keyword_from_string(std::string_view s);

std::string_view filter_verb_string(FilterVerb verb);       // STR# 25400
std::optional<FilterVerb> filter_verb_from_string(std::string_view s);
std::string_view filter_conjunction_string(FilterConjunction c); // STR# 18300
std::optional<FilterConjunction> filter_conjunction_from_string(std::string_view s);

// ReadFilters: parse a filters file.  A missing file is vacuous success
// (empty list), as the original treated fnfErr.
std::optional<std::vector<Filter>> read_filters(const std::filesystem::path &file);
// Parse from memory (for tests / imports).
std::vector<Filter> parse_filters(std::string_view text);

// WriteFilter/SaveFiltersLo.
std::string serialize_filters(const std::vector<Filter> &filters);
bool write_filters(const std::vector<Filter> &filters,
                   const std::filesystem::path &file);

} // namespace eudora
