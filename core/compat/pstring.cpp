#include "compat/pstring.hpp"

#include <algorithm>

namespace eudora {

std::string pascal_to_string(std::span<const std::uint8_t> buffer) {
    if (buffer.empty())
        return {};
    std::size_t len = buffer[0];
    len = std::min(len, buffer.size() - 1);
    return std::string(reinterpret_cast<const char *>(buffer.data() + 1), len);
}

void string_to_pascal(const std::string &text, std::span<std::uint8_t> buffer) {
    if (buffer.empty())
        return;
    std::fill(buffer.begin(), buffer.end(), std::uint8_t{0});
    const std::size_t cap = std::min<std::size_t>(buffer.size() - 1, 255);
    const std::size_t len = std::min(text.size(), cap);
    buffer[0] = static_cast<std::uint8_t>(len);
    std::copy_n(reinterpret_cast<const std::uint8_t *>(text.data()), len, buffer.begin() + 1);
}

} // namespace eudora
