#include "filters/regexp.hpp"

#include <regex.h>

namespace eudora {

struct Regexp::Impl {
    regex_t re;
    bool compiled = false;
    ~Impl() {
        if (compiled)
            regfree(&re);
    }
};

Regexp::Regexp() : impl_(std::make_unique<Impl>()) {}
Regexp::~Regexp() = default;

std::shared_ptr<Regexp> Regexp::compile(const std::string &pattern) {
    std::shared_ptr<Regexp> r(new Regexp());
    if (regcomp(&r->impl_->re, pattern.c_str(), REG_EXTENDED) != 0)
        return nullptr;
    r->impl_->compiled = true;
    return r;
}

std::optional<std::size_t> Regexp::search(std::string_view text,
                                          std::size_t offset) const {
    if (offset > text.size())
        return std::nullopt;
    std::string hay(text.substr(offset));
    for (auto &c : hay)
        if (c == '\0')
            c = ' ';
    regmatch_t m;
    if (regexec(&impl_->re, hay.c_str(), 1, &m, 0) != 0)
        return std::nullopt;
    return offset + static_cast<std::size_t>(m.rm_so);
}

} // namespace eudora
