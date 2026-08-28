// Mailbox compaction — the modern squish.c (NOT compact.c, which despite its
// name was the composition window).
//
// Compacting copies each live message's byte range into a fresh file in TOC
// order, rewrites the summaries' offsets to the new positions, and atomically
// replaces the mailbox (CompactMailbox, squish.c:200).

#pragma once

#include <cstdint>

#include "mailstore/summary.hpp"

namespace eudora {

// Bytes in the mailbox file not referenced by any summary.
std::int64_t wasted_bytes(const TableOfContents &toc, std::int64_t mailbox_size);

// NeedsCompaction (squish.c:142): true when summaries don't tile the file
// from offset 0.
bool needs_compaction(const TableOfContents &toc, std::int64_t mailbox_size);

// CompactMailbox: rewrite toc.mailbox_path keeping only live ranges and
// update every summary's offset.  On success the caller should persist the
// TOC (the original called WriteTOC immediately, squish.c:322).
bool compact_mailbox(TableOfContents &toc);

} // namespace eudora
