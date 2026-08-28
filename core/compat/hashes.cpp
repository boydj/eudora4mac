#include "compat/hashes.hpp"

#include <string>

#include "mail/address_parser.hpp"

namespace eudora {

namespace {
constexpr std::uint32_t kKRHashPrime = 2147483629u;
constexpr std::uint32_t kNoMessageIdHash = 0xFFFFFFFFu;
} // namespace

std::uint32_t kr_hash(std::string_view text, std::uint32_t seed) {
    std::uint32_t sum = seed - 1;
    for (unsigned char c : text) {
        for (int bit = 0x80; bit != 0; bit >>= 1) {
            sum += sum;
            if (sum >= kKRHashPrime)
                sum -= kKRHashPrime;
            if (c & bit)
                ++sum;
            if (sum >= kKRHashPrime)
                sum -= kKRHashPrime;
        }
    }
    return sum + 1;
}

std::uint32_t mid_hash(std::string_view message_id) {
    // MIDHash ran the header through the address parser and hashed the first
    // address token — for "<abc@def>" that is "abc@def" (message.c:4578).
    auto addrs = parse_addresses(message_id, false);
    if (!addrs || addrs->empty() || addrs->front().empty())
        return kNoMessageIdHash;
    std::string id = addrs->front();
    // The legacy path went through a Str255, so at most 255 bytes are hashed.
    if (id.size() > 255)
        id.resize(255);
    return kr_hash(id);
}

} // namespace eudora
