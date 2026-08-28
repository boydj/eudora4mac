// TOC file I/O — the modern ReadDForkTOC / WriteTOC (toc.c).
//
// Only the data-fork sidecar file ("<mailbox>.toc") is supported; the
// resource-fork variant ('TOCF' 1001 in the mailbox's resource fork) cannot
// exist on a modern filesystem export.  Files migrated from a real Mac get
// their resource forks stripped, so the sidecar is the interoperable form.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "mailstore/summary.hpp"
#include "mailstore/toc_format.hpp"

namespace eudora {

// Box2TOCSpec: the sidecar path for a mailbox (appends ".toc").
std::filesystem::path toc_path_for_mailbox(const std::filesystem::path &mailbox);

// Read and validate a TOC file.  When `mailbox` exists its size feeds the
// original's cross-checks; the loaded TOC gets mailbox_path stamped, like
// ReadTOC re-stamped the FSSpec (toc.c:228).
std::optional<TableOfContents> read_toc(const std::filesystem::path &toc_file,
                                        const std::filesystem::path &mailbox,
                                        tocfmt::TocError *error = nullptr);

// Write a TOC beside its mailbox (atomically: temp file + rename).
// Stamps boxSize/writeDate from the current mailbox file, as WriteTOC did.
bool write_toc(const TableOfContents &toc, const std::filesystem::path &toc_file);

} // namespace eudora
