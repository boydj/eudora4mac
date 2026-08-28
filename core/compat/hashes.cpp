#include "compat/hashes.hpp"

#include <cctype>
#include <string>

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
    // The original ran the header through the address parser and hashed the
    // first address token; for a Message-Id that is the text inside the
    // first <...> with comments removed, else the first non-space token.
    std::string cleaned;
    int comment_depth = 0;
    for (char c : message_id) {
        if (c == '(') {
            ++comment_depth;
        } else if (c == ')') {
            if (comment_depth > 0)
                --comment_depth;
        } else if (comment_depth == 0) {
            cleaned += c;
        }
    }

    std::string id;
    const auto lt = cleaned.find('<');
    if (lt != std::string::npos) {
        const auto gt = cleaned.find('>', lt + 1);
        if (gt != std::string::npos)
            id = cleaned.substr(lt + 1, gt - lt - 1);
    }
    if (id.empty()) {
        std::size_t b = 0;
        while (b < cleaned.size() &&
               std::isspace(static_cast<unsigned char>(cleaned[b])))
            ++b;
        std::size_t e = b;
        while (e < cleaned.size() &&
               !std::isspace(static_cast<unsigned char>(cleaned[e])))
            ++e;
        id = cleaned.substr(b, e - b);
    }
    if (id.empty())
        return kNoMessageIdHash;
    // The legacy path went through a Str255, so at most 255 bytes are hashed.
    if (id.size() > 255)
        id.resize(255);
    return kr_hash(id);
}

} // namespace eudora
