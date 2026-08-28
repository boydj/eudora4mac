// Eudora's message hashes (message.c:4551-4592).
//
// These must match the legacy implementation bit-for-bit so hashes stored in
// existing TOC files (msgIdHash, uidHash, fromHash) remain valid.

#pragma once

#include <cstdint>
#include <string_view>

namespace eudora {

// HashWithSeedLo: bitwise Karp-Rabin over the prime 2147483629, +1 at the
// end.  Hash(x) == kr_hash(x, 1).
std::uint32_t kr_hash(std::string_view text, std::uint32_t seed = 1);

// MIDHash: hash a Message-Id, stripping comments and <>'s first; returns
// kNoMessageId (0xFFFFFFFF) when no id can be extracted.
std::uint32_t mid_hash(std::string_view message_id);

} // namespace eudora
